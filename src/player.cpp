#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tracker {

namespace {
constexpr int CHANNEL_DISPLAY_WIDTH = 24;

std::vector<std::string> read_instrument_names(openmpt::module &module) {
    auto instruments = module.get_instrument_names();
    if (!instruments.empty()) {
        return instruments;
    }
    auto samples = module.get_sample_names();
    if (!samples.empty()) {
        return samples;
    }
    auto channels = module.get_channel_names();
    return channels;
}

std::string sanitize_name(const std::string &name) {
    if (name.empty()) {
        return "<unnamed>";
    }
    if (name.size() <= CHANNEL_DISPLAY_WIDTH) {
        return name;
    }
    return name.substr(0, CHANNEL_DISPLAY_WIDTH - 1) + "…";
}

std::vector<std::string> split_lines(const std::string &text, std::size_t max_lines = 128) {
    std::vector<std::string> lines;
    lines.reserve(std::min<std::size_t>(max_lines, 32));
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (lines.size() >= max_lines) {
            break;
        }
    }
    return lines;
}

}

Player::Player(const std::string &path, int sample_rate, int buffer_size)
    : sample_rate_(sample_rate), buffer_size_(buffer_size) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Unable to open module file: " + path);
    }

    module_ = std::make_unique<openmpt::module>(file);

    instrument_names_ = read_instrument_names(*module_);
    for (auto &name : instrument_names_) {
        name = sanitize_name(name);
    }

    sample_names_ = module_->get_sample_names();
    for (auto &name : sample_names_) {
        name = sanitize_name(name);
    }

    tracker_name_ = module_->get_metadata("tracker");
    if (tracker_name_.empty()) {
        tracker_name_ = "Unknown";
    }

    std::string message = module_->get_metadata("message");
    if (message.empty()) {
        message = module_->get_metadata("comment");
    }
    if (message.empty()) {
        message = module_->get_metadata("message_text");
    }
    if (!message.empty()) {
        module_message_lines_ = split_lines(message, 256);
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        throw std::runtime_error(std::string("Failed to initialize PortAudio: ") + Pa_GetErrorText(err));
    }
    pa_initialized_ = true;

    err = Pa_OpenDefaultStream(&stream_, 0, 2, paFloat32, sample_rate_, buffer_size_, nullptr, nullptr);
    if (err != paNoError) {
        Pa_Terminate();
        pa_initialized_ = false;
        throw std::runtime_error(std::string("Failed to open PortAudio stream: ") + Pa_GetErrorText(err));
    }

    title_ = module_->get_metadata("title");
    if (title_.empty()) {
        title_ = path;
    }
    duration_seconds_ = module_->get_duration_seconds();

    state_.channels.resize(static_cast<std::size_t>(module_->get_num_channels()));
    update_state_locked();
}

Player::~Player() {
    stop();
    if (stream_) {
        Pa_CloseStream(stream_);
        stream_ = nullptr;
    }
    if (pa_initialized_) {
        Pa_Terminate();
        pa_initialized_ = false;
    }
}

void Player::start() {
    if (running_) {
        return;
    }
    PaError err = Pa_StartStream(stream_);
    if (err != paNoError) {
        throw std::runtime_error(std::string("Failed to start PortAudio stream: ") + Pa_GetErrorText(err));
    }
    running_ = true;
    stream_running_ = true;
    stop_requested_ = false;
    playback_thread_ = std::thread(&Player::playback_loop, this);
}

void Player::stop() {
    if (!running_) {
        return;
    }
    {
        std::lock_guard lock(state_mutex_);
        stop_requested_ = true;
        paused_ = false;
    }
    pause_cv_.notify_all();
    if (playback_thread_.joinable()) {
        playback_thread_.join();
    }
    Pa_StopStream(stream_);
    stream_running_ = false;
    running_ = false;
}

void Player::toggle_pause() {
    {
        std::lock_guard lock(state_mutex_);
        paused_ = !paused_;
        state_.paused = paused_;
    }
    pause_cv_.notify_all();
}

void Player::jump_to_order(int delta) {
    {
        std::lock_guard module_lock(module_mutex_);
        if (!module_) {
            return;
        }
        int current_order = module_->get_current_order();
        int target = std::clamp(current_order + delta, 0, module_->get_num_orders() - 1);
        module_->set_position_order_row(target, 0);
    }

    std::lock_guard state_lock(state_mutex_);
    finished_ = false;
    state_.finished = false;
    update_state_locked();
}

void Player::jump_rows(int delta_rows) {
    if (delta_rows == 0) {
        return;
    }

    int target_order = 0;
    int target_row = 0;

    {
        std::lock_guard module_lock(module_mutex_);
        if (!module_) {
            return;
        }

        int current_order = module_->get_current_order();
        int current_row = module_->get_current_row();
        int total_orders = module_->get_num_orders();

        if (total_orders <= 0) {
            return;
        }

        auto advance_forward = [&](int ord, int row, int remaining) {
            while (remaining > 0 && ord < total_orders) {
                int pattern_index = module_->get_order_pattern(ord);
                if (pattern_index < 0) {
                    ++ord;
                    row = 0;
                    continue;
                }
                int pattern_rows = module_->get_pattern_num_rows(pattern_index);
                if (pattern_rows <= 0) {
                    ++ord;
                    row = 0;
                    continue;
                }
                int rows_left = pattern_rows - row - 1;
                if (remaining <= rows_left) {
                    row += remaining;
                    remaining = 0;
                    break;
                }
                remaining -= rows_left + 1;
                ++ord;
                row = 0;
            }
            if (ord >= total_orders) {
                ord = total_orders - 1;
                int pattern_index = module_->get_order_pattern(ord);
                int pattern_rows = pattern_index >= 0 ? module_->get_pattern_num_rows(pattern_index) : 0;
                row = std::max(0, pattern_rows - 1);
            }
            return std::pair<int, int>{ord, row};
        };

        auto advance_backward = [&](int ord, int row, int remaining) {
            while (remaining > 0 && ord >= 0) {
                if (row > 0) {
                    int step = std::min(row, remaining);
                    row -= step;
                    remaining -= step;
                    if (remaining == 0) {
                        break;
                    }
                }
                --ord;
                if (ord < 0) {
                    ord = 0;
                    row = 0;
                    break;
                }
                int pattern_index = module_->get_order_pattern(ord);
                int pattern_rows = pattern_index >= 0 ? module_->get_pattern_num_rows(pattern_index) : 0;
                if (pattern_rows <= 0) {
                    row = 0;
                    continue;
                }
                row = pattern_rows - 1;
                --remaining;
            }
            return std::pair<int, int>{ord, row};
        };

        if (delta_rows > 0) {
            auto result = advance_forward(current_order, current_row, delta_rows);
            target_order = result.first;
            target_row = result.second;
        } else {
            auto result = advance_backward(current_order, current_row, -delta_rows);
            target_order = result.first;
            target_row = result.second;
        }

        module_->set_position_order_row(target_order, target_row);
    }

    std::lock_guard state_lock(state_mutex_);
    finished_ = false;
    state_.finished = false;
    update_state_locked();
}

TransportState Player::snapshot() const {
    std::lock_guard lock(state_mutex_);
    return state_;
}

void Player::playback_loop() {
    std::vector<float> buffer(static_cast<std::size_t>(buffer_size_) * 2);

    while (true) {
        bool stop_now = false;
        bool paused_now = false;
        {
            std::lock_guard lock(state_mutex_);
            stop_now = stop_requested_;
            paused_now = paused_;
        }

        if (stop_now) {
            break;
        }

        if (paused_now) {
            bool was_running = false;
            {
                std::lock_guard lock(state_mutex_);
                was_running = stream_running_;
            }

            if (was_running) {
                PaError err = Pa_StopStream(stream_);
                if (err != paNoError && err != paStreamIsStopped) {
                    std::cerr << "PortAudio stop error: " << Pa_GetErrorText(err) << std::endl;
                    break;
                }
                {
                    std::lock_guard lock(state_mutex_);
                    stream_running_ = false;
                    for (auto &channel : state_.channels) {
                        channel.vu_left = 0.0;
                        channel.vu_right = 0.0;
                    }
                }
            }

            std::unique_lock lock(state_mutex_);
            pause_cv_.wait(lock, [&] { return !paused_ || stop_requested_; });
            if (stop_requested_) {
                break;
            }
            continue;
        }

        bool need_start = false;
        {
            std::lock_guard lock(state_mutex_);
            need_start = !stream_running_;
        }
        if (need_start) {
            PaError err = Pa_StartStream(stream_);
            if (err != paNoError && err != paStreamIsNotStopped) {
                std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
                break;
            }
            {
                std::lock_guard lock(state_mutex_);
                stream_running_ = true;
            }
            continue;
        }

        long frames_rendered = 0;
        {
            std::lock_guard module_lock(module_mutex_);
            frames_rendered = module_->read_interleaved_stereo(sample_rate_, buffer_size_, buffer.data());
        }

        if (frames_rendered <= 0) {
            std::lock_guard lock(state_mutex_);
            finished_ = true;
            state_.finished = true;
            break;
        }

        PaError err = Pa_WriteStream(stream_, buffer.data(), frames_rendered);
        if (err != paNoError) {
            std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
            break;
        }

        {
            std::lock_guard lock(state_mutex_);
            update_state_locked();
        }
    }
}

void Player::update_state_locked() {
    if (!module_) {
        return;
    }

    std::lock_guard module_lock(module_mutex_);

    state_.paused = paused_;
    state_.finished = finished_;

    state_.order = module_->get_current_order();
    state_.pattern = module_->get_current_pattern();
    state_.row = module_->get_current_row();
    state_.speed = module_->get_current_speed();
    state_.position_seconds = module_->get_position_seconds();

    auto channels = module_->get_num_channels();
    if (state_.channels.size() != static_cast<std::size_t>(channels)) {
        state_.channels.resize(static_cast<std::size_t>(channels));
    }

    for (int ch = 0; ch < channels; ++ch) {
        ChannelStatus &status = state_.channels[static_cast<std::size_t>(ch)];
    status.vu_left = module_->get_current_channel_vu_left(ch);
    status.vu_right = module_->get_current_channel_vu_right(ch);

        try {
            status.line = module_->format_pattern_row_channel(state_.pattern, state_.row, ch);
        } catch (...) {
            status.line = "--- .. .. ...";
        }
    }

    state_.preview_rows.clear();
    constexpr int PREVIEW_LIMIT = 32;
    if (state_.order >= 0 && state_.pattern >= 0 && state_.row >= 0 && channels > 0) {
        int preview_remaining = PREVIEW_LIMIT;
        int order_index = state_.order;
        int pattern_index = state_.pattern;
        int row_index = state_.row + 1;
        auto total_orders = module_->get_num_orders();

        while (preview_remaining > 0 && order_index < total_orders) {
            if (pattern_index < 0) {
                ++order_index;
                row_index = 0;
                if (order_index >= total_orders) {
                    break;
                }
                pattern_index = module_->get_order_pattern(order_index);
                continue;
            }

            int pattern_rows = module_->get_pattern_num_rows(pattern_index);
            if (pattern_rows <= 0) {
                ++order_index;
                row_index = 0;
                if (order_index >= total_orders) {
                    break;
                }
                pattern_index = module_->get_order_pattern(order_index);
                continue;
            }

            for (; row_index < pattern_rows && preview_remaining > 0; ++row_index) {
                PatternRowPreview preview;
                preview.order = order_index;
                preview.pattern = pattern_index;
                preview.row = row_index;
                preview.channels.resize(static_cast<std::size_t>(channels));
                for (int ch = 0; ch < channels; ++ch) {
                    try {
                        preview.channels[static_cast<std::size_t>(ch)] =
                            module_->format_pattern_row_channel(pattern_index, row_index, ch);
                    } catch (...) {
                        preview.channels[static_cast<std::size_t>(ch)] = "--- .. .. ...";
                    }
                }
                state_.preview_rows.push_back(std::move(preview));
                --preview_remaining;
            }

            if (preview_remaining <= 0) {
                break;
            }

            ++order_index;
            row_index = 0;
            if (order_index >= total_orders) {
                break;
            }
            pattern_index = module_->get_order_pattern(order_index);
        }
    }
}
}

