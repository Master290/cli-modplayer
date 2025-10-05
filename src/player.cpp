#include "player.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unistd.h>
#include <fcntl.h>

namespace tracker {

namespace {

class SuppressStderr {
public:
    SuppressStderr() {
        fflush(stderr);
        old_stderr_ = dup(STDERR_FILENO);
        int devnull = open("/dev/null", O_WRONLY);
        dup2(devnull, STDERR_FILENO);
        close(devnull);
    }
    
    ~SuppressStderr() {
        fflush(stderr);
        dup2(old_stderr_, STDERR_FILENO);
        close(old_stderr_);
    }
private:
    int old_stderr_;
};

void fft(std::vector<std::complex<float>>& data) {
    const std::size_t n = data.size();
    if (n <= 1) return;

    std::vector<std::complex<float>> even(n / 2);
    std::vector<std::complex<float>> odd(n / 2);
    for (std::size_t i = 0; i < n / 2; ++i) {
        even[i] = data[i * 2];
        odd[i] = data[i * 2 + 1];
    }

    fft(even);
    fft(odd);

    for (std::size_t k = 0; k < n / 2; ++k) {
        auto t = std::polar(1.0f, -2.0f * std::numbers::pi_v<float> * static_cast<float>(k) / static_cast<float>(n)) * odd[k];
        data[k] = even[k] + t;
        data[k + n / 2] = even[k] - t;
    }
}

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
    : sample_rate_(sample_rate), 
      buffer_size_(buffer_size),
      audio_effects_(std::make_unique<AudioEffects>(sample_rate)),
      fft_buffer_(kFFTSize),
      fft_write_pos_(0),
      spectrum_bands_(kSpectrumBands, 0.0),
      waveform_buffer_left_(kWaveformSize, 0.0f),
      waveform_buffer_right_(kWaveformSize, 0.0f),
      waveform_write_pos_(0) {
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

    PaError err;
    {
        SuppressStderr suppress;
        err = Pa_Initialize();
        if (err != paNoError) {
            throw std::runtime_error(std::string("Failed to initialize PortAudio: ") + Pa_GetErrorText(err));
        }
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
    
    artist_ = module_->get_metadata("artist");
    if (artist_.empty()) {
        artist_ = "Unknown";
    }
    
    module_type_ = module_->get_metadata("type");
    if (module_type_.empty()) {
        module_type_ = module_->get_metadata("type_long");
    }
    if (module_type_.empty()) {
        module_type_ = "Unknown";
    }
    
    date_ = module_->get_metadata("date");
    
    num_channels_ = module_->get_num_channels();
    num_instruments_ = module_->get_num_instruments();
    num_samples_ = module_->get_num_samples();
    num_patterns_ = module_->get_num_patterns();
    num_orders_ = module_->get_num_orders();
    duration_seconds_ = module_->get_duration_seconds();

    state_.channels.resize(static_cast<std::size_t>(num_channels_));
    state_.spectrum_bands.resize(kSpectrumBands, 0.0);
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

void Player::set_volume(double volume) {
    std::lock_guard lock(state_mutex_);
    volume_ = std::clamp(volume, 0.0, 1.0);
}

double Player::get_volume() const noexcept {
    std::lock_guard lock(state_mutex_);
    return volume_;
}

void Player::set_effect(AudioEffect effect) {
    std::lock_guard lock(state_mutex_);
    current_effect_ = effect;
}

AudioEffect Player::get_effect() const noexcept {
    std::lock_guard lock(state_mutex_);
    return current_effect_;
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

        double current_volume = 0.0;
        AudioEffect current_effect = AudioEffect::None;
        {
            std::lock_guard lock(state_mutex_);
            current_volume = volume_;
            current_effect = current_effect_;
        }
        if (current_volume != 1.0) {
            for (long i = 0; i < frames_rendered * 2; ++i) {
                buffer[static_cast<std::size_t>(i)] *= static_cast<float>(current_volume);
            }
        }
        
        if (current_effect != AudioEffect::None && audio_effects_) {
            audio_effects_->apply_effects(buffer.data(), static_cast<std::size_t>(frames_rendered), current_effect);
        }

        update_spectrum(buffer.data(), static_cast<std::size_t>(frames_rendered * 2));
        update_waveform(buffer.data(), static_cast<std::size_t>(frames_rendered * 2));

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
    
    if (channel_instruments_.size() != static_cast<std::size_t>(channels)) {
        channel_instruments_.resize(static_cast<std::size_t>(channels), -1);
    }

    for (int ch = 0; ch < channels; ++ch) {
        ChannelStatus &status = state_.channels[static_cast<std::size_t>(ch)];
        status.vu_left = module_->get_current_channel_vu_left(ch);
        status.vu_right = module_->get_current_channel_vu_right(ch);

        try {
            status.line = module_->format_pattern_row_channel(state_.pattern, state_.row, ch);
            
            int detected_ins = -1;
            
            if (status.line.length() >= 6) {
                std::string ins_str = status.line.substr(4, 2);
                
                ins_str.erase(std::remove(ins_str.begin(), ins_str.end(), ' '), ins_str.end());
                
                if (!ins_str.empty() && ins_str != ".." && ins_str != "." && ins_str != "-") {
                    try {
                        try {
                            detected_ins = std::stoi(ins_str, nullptr, 16);
                        } catch (...) {
                            try {
                                detected_ins = std::stoi(ins_str, nullptr, 10);
                            } catch (...) {
                                detected_ins = -1;
                            }
                        }
                        
                        if (detected_ins > 0 && detected_ins <= static_cast<int>(instrument_names_.size())) {
                            channel_instruments_[static_cast<std::size_t>(ch)] = detected_ins - 1;
                        }
                    } catch (...) {
                    }
                }
            }
            
            double vu_level = std::max(std::abs(status.vu_left), std::abs(status.vu_right));
            if (vu_level > 0.01 && channel_instruments_[static_cast<std::size_t>(ch)] >= 0) {
                int ins_idx = channel_instruments_[static_cast<std::size_t>(ch)];
                if (ins_idx < static_cast<int>(instrument_names_.size())) {
                    status.instrument_index = ins_idx;
                    status.instrument_name = instrument_names_[static_cast<std::size_t>(ins_idx)];
                } else {
                    status.instrument_index = -1;
                    status.instrument_name.clear();
                }
            } else {
                status.instrument_index = -1;
                status.instrument_name.clear();
            }
        } catch (...) {
            status.line = "--- .. .. ...";
            status.instrument_index = -1;
            status.instrument_name.clear();
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

void Player::update_spectrum(const float* audio_data, std::size_t sample_count) {
    std::lock_guard lock(spectrum_mutex_);

    for (std::size_t i = 0; i < sample_count; i += 2) {
        if (fft_write_pos_ < kFFTSize) {
            float mono_sample = (audio_data[i] + audio_data[i + 1]) * 0.5f;
            fft_buffer_[fft_write_pos_++] = std::complex<float>(mono_sample, 0.0f);
        }
    }

    if (fft_write_pos_ < kFFTSize) {
        return;
    }

    fft_write_pos_ = 0;

    std::vector<std::complex<float>> windowed_buffer(kFFTSize);
    for (std::size_t i = 0; i < kFFTSize; ++i) {
        float window = 0.5f * (1.0f - std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kFFTSize - 1)));
        windowed_buffer[i] = fft_buffer_[i] * window;
    }

    fft(windowed_buffer);

    std::vector<float> magnitudes(kFFTSize / 2);
    for (std::size_t i = 0; i < kFFTSize / 2; ++i) {
        magnitudes[i] = std::abs(windowed_buffer[i]);
    }

    spectrum_bands_.resize(kSpectrumBands);
    const float freq_per_bin = static_cast<float>(sample_rate_) / static_cast<float>(kFFTSize);
    const float min_freq = 20.0f;
    const float max_freq = 20000.0f;
    const float log_min = std::log10(min_freq);
    const float log_max = std::log10(max_freq);
    const float log_range = log_max - log_min;

    for (std::size_t band = 0; band < kSpectrumBands; ++band) {
        float band_start_log = log_min + (log_range * static_cast<float>(band) / static_cast<float>(kSpectrumBands));
        float band_end_log = log_min + (log_range * static_cast<float>(band + 1) / static_cast<float>(kSpectrumBands));
        float freq_start = std::pow(10.0f, band_start_log);
        float freq_end = std::pow(10.0f, band_end_log);

        std::size_t bin_start = static_cast<std::size_t>(freq_start / freq_per_bin);
        std::size_t bin_end = static_cast<std::size_t>(freq_end / freq_per_bin);
        bin_start = std::min(bin_start, static_cast<std::size_t>(kFFTSize / 2 - 1));
        bin_end = std::min(bin_end, static_cast<std::size_t>(kFFTSize / 2));
        
        if (bin_end <= bin_start) {
            bin_end = bin_start + 1;
        }

        float sum = 0.0f;
        std::size_t count = 0;
        for (std::size_t bin = bin_start; bin < bin_end; ++bin) {
            sum += magnitudes[bin];
            ++count;
        }
        float avg_magnitude = count > 0 ? sum / static_cast<float>(count) : 0.0f;

        float normalized = avg_magnitude / static_cast<float>(kFFTSize);
        normalized *= 2.5f;
        float db_scale = normalized > 0.0f ? (20.0f * std::log10(normalized + 1e-6f) + 50.0f) / 50.0f : 0.0f;
        db_scale = std::clamp(db_scale, 0.0f, 1.0f);

        spectrum_bands_[band] = db_scale;
    }

    {
        std::lock_guard state_lock(state_mutex_);
        state_.spectrum_bands = spectrum_bands_;
    }
}

void Player::update_waveform(const float* audio_data, std::size_t sample_count) {
    std::lock_guard lock(waveform_mutex_);
    
    const std::size_t decimation = std::max<std::size_t>(1, sample_count / (kWaveformSize * 4));
    
    for (std::size_t i = 0; i < sample_count; i += decimation * 2) {
        if (waveform_write_pos_ >= kWaveformSize) {
            waveform_write_pos_ = 0;
        }
        
        waveform_buffer_left_[waveform_write_pos_] = audio_data[i];
        waveform_buffer_right_[waveform_write_pos_] = audio_data[i + 1];
        ++waveform_write_pos_;
        
        if (waveform_write_pos_ >= kWaveformSize) {
            break;
        }
    }
    
    {
        std::lock_guard state_lock(state_mutex_);
        state_.waveform_left = waveform_buffer_left_;
        state_.waveform_right = waveform_buffer_right_;
    }
}

bool Player::export_to_file(const ExportOptions& options, std::string& error_message) {
    try {
        bool was_playing = false;
        {
            std::lock_guard lock(state_mutex_);
            was_playing = running_ && !paused_;
            if (was_playing) {
                paused_ = true;
            }
        }
        
        double saved_position;
        double duration;
        std::size_t total_samples;
        
        {
            std::lock_guard module_lock(module_mutex_);
            saved_position = module_->get_position_seconds();
            duration = module_->get_duration_seconds();
            total_samples = static_cast<std::size_t>(duration * options.sample_rate * options.channels);
            module_->set_position_seconds(0.0);
        }
        
        std::vector<float> audio_buffer;
        audio_buffer.reserve(total_samples);
        
        const std::size_t chunk_size = 4096 * options.channels;
        std::vector<float> chunk_buffer(chunk_size);
        std::size_t samples_rendered = 0;
        
        while (samples_rendered < total_samples) {
            std::size_t to_read = std::min(chunk_size, total_samples - samples_rendered);
            std::size_t frames_to_read = to_read / options.channels;
            
            std::size_t frames_read;
            {
                std::lock_guard module_lock(module_mutex_);
                frames_read = module_->read_interleaved_stereo(
                    options.sample_rate, 
                    frames_to_read, 
                    chunk_buffer.data()
                );
            }
            
            if (frames_read == 0) {
                break;
            }
            
            for (std::size_t i = 0; i < frames_read * options.channels; ++i) {
                chunk_buffer[i] *= static_cast<float>(volume_);
            }
            
            if (current_effect_ != AudioEffect::None && audio_effects_) {
                audio_effects_->apply_effects(chunk_buffer.data(), frames_read, current_effect_);
            }
            
            audio_buffer.insert(audio_buffer.end(), 
                              chunk_buffer.begin(), 
                              chunk_buffer.begin() + frames_read * options.channels);
            
            samples_rendered += frames_read * options.channels;
            
            if (options.progress_callback) {
                if (!options.progress_callback(samples_rendered, total_samples)) {
                    error_message = "Export cancelled by user";
                    
                    {
                        std::lock_guard module_lock(module_mutex_);
                        module_->set_position_seconds(saved_position);
                    }
                    if (was_playing) {
                        std::lock_guard lock(state_mutex_);
                        paused_ = false;
                        pause_cv_.notify_all();
                    }
                    return false;
                }
            }
        }
        
        AudioExporter exporter;
        bool success = exporter.export_audio(audio_buffer, options, error_message);
        
        {
            std::lock_guard module_lock(module_mutex_);
            module_->set_position_seconds(saved_position);
        }
        if (was_playing) {
            std::lock_guard lock(state_mutex_);
            paused_ = false;
            pause_cv_.notify_all();
        }
        
        return success;
        
    } catch (const std::exception& e) {
        error_message = std::string("Export failed: ") + e.what();
        return false;
    }
}

} 

