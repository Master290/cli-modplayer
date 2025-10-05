#include "ui.hpp"
#include "audio_effects.hpp"
#include "audio_exporter.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

namespace tracker {
namespace {

struct Theme {
    ftxui::Color background;
    ftxui::Color panel;
    ftxui::Color panel_alt;
    ftxui::Color accent;
    ftxui::Color accent_soft;
    ftxui::Color border;
    ftxui::Color text;
    ftxui::Color text_dim;
    ftxui::Color success;
    ftxui::Color warning;
    ftxui::Color danger;
};

const Theme kTheme{ftxui::Color::RGB(16, 18, 26),    ftxui::Color::RGB(26, 28, 38),
                   ftxui::Color::RGB(32, 34, 46),    ftxui::Color::RGB(129, 200, 190),
                   ftxui::Color::RGB(54, 57, 70),    ftxui::Color::RGB(118, 92, 199),
                   ftxui::Color::RGB(230, 230, 230), ftxui::Color::RGB(160, 164, 182),
                   ftxui::Color::RGB(124, 200, 146), ftxui::Color::RGB(230, 196, 84),
                   ftxui::Color::RGB(232, 125, 104)};

constexpr int kMasterVisualizerBars = 20;
constexpr int kMasterVisualizerHeight = 12;

std::string channel_placeholder() {
    return "--- .. .. ...";
}

std::string format_channel_label(int index) {
    std::ostringstream oss;
    oss << "CH" << std::setw(2) << std::setfill('0') << index;
    return oss.str();
}

std::string format_order_row_label(int order, int row) {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << std::max(0, order) << ':'
        << std::setw(2) << std::setfill('0') << std::max(0, row);
    return oss.str();
}

std::string format_two_digit(int value) {
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << std::max(0, value);
    return oss.str();
}

std::string format_time(double seconds) {
    if (!std::isfinite(seconds)) {
        seconds = 0.0;
    }
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    int total = static_cast<int>(std::lround(seconds));
    int hours = total / 3600;
    int minutes = (total % 3600) / 60;
    int secs = total % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << ':' << std::setw(2) << std::setfill('0') << minutes << ':'
            << std::setw(2) << std::setfill('0') << secs;
    } else {
        oss << minutes << ':' << std::setw(2) << std::setfill('0') << secs;
    }
    return oss.str();
}

ftxui::Element apply_decorators(ftxui::Element element, const std::vector<ftxui::Decorator> &decorators) {
    for (const auto &decorator : decorators) {
        element = element | decorator;
    }
    return element;
}

ftxui::Color amplitude_to_color(double amplitude) {
    if (amplitude > 0.75) {
        return kTheme.danger;
    }
    if (amplitude > 0.45) {
        return kTheme.warning;
    }
    if (amplitude > 0.2) {
        return kTheme.accent;
    }
    return kTheme.text_dim;
}

std::string format_status_position(const TransportState &state, double total_duration) {
    std::ostringstream oss;
    oss << format_time(state.position_seconds) << " / " << format_time(total_duration);
    return oss.str();
}

} 

Ui::Ui(Player &player, Config &config, const std::string& module_filename)
    : player_(player), config_(config), export_filename_(module_filename) {}

Ui::~Ui() = default;

void Ui::reset_ui_state() {
    running_ = true;
    info_overlay_ = false;
    status_message_.clear();
    status_message_until_ = std::chrono::steady_clock::now();
    history_.clear();
    history_capacity_ = 100;
    last_order_ = -1;
    last_row_ = -1;
    channel_peaks_.clear();
    master_levels_.assign(static_cast<std::size_t>(kMasterVisualizerBars), 0.0);
    master_peaks_.assign(static_cast<std::size_t>(kMasterVisualizerBars), 0.0);
    master_overall_level_ = 0.0;
    last_frame_time_ = std::chrono::steady_clock::now();
    last_frame_seconds_ = 0.0;
    channel_offset_ = 0;
    page_columns_ = 4;
    last_state_ = TransportState{};
}

void Ui::run() {
    using namespace std::chrono_literals;

    reset_ui_state();
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    std::atomic<bool> loop_running{true};

    auto renderer = ftxui::Renderer([&] {
        auto now = std::chrono::steady_clock::now();
        if (last_frame_time_.time_since_epoch().count() != 0) {
            last_frame_seconds_ = std::chrono::duration<double>(now - last_frame_time_).count();
        }
        last_frame_time_ = now;

        last_state_ = player_.snapshot();
        update_history(last_state_);
        update_visualizer_peaks(last_state_, static_cast<int>(last_state_.channels.size()));

        if (last_state_.finished && running_) {
            running_ = false;
            loop_running = false;
            screen.Exit();
        }

        return render(last_state_);
    });

    auto component = ftxui::CatchEvent(renderer, [&](ftxui::Event event) {
        const auto refresh = [&] {
            screen.PostEvent(ftxui::Event::Custom);
        };

        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q') ||
            event == ftxui::Event::Escape) {
            running_ = false;
            loop_running = false;
            screen.Exit();
            return true;
        }

        if (event == ftxui::Event::Character(' ')) {
            player_.toggle_pause();
            auto state = player_.snapshot();
            set_status_message(state.paused ? "Paused" : "Playing");
            refresh();
            return true;
        }

        if (event == ftxui::Event::ArrowLeft || event == ftxui::Event::Character('h') || event == ftxui::Event::Character('H')) {
            player_.jump_to_order(-1);
            auto state = player_.snapshot();
            std::ostringstream oss;
            oss << "Order → " << std::setw(2) << std::setfill('0') << std::max(0, state.order);
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (event == ftxui::Event::ArrowRight || event == ftxui::Event::Character('l') ||
            event == ftxui::Event::Character('L')) {
            player_.jump_to_order(1);
            auto state = player_.snapshot();
            std::ostringstream oss;
            oss << "Order → " << std::setw(2) << std::setfill('0') << std::max(0, state.order);
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (event == ftxui::Event::PageDown || event == ftxui::Event::Character('d') ||
            event == ftxui::Event::Character('D')) {
            channel_offset_ += 1;
            set_status_message("Scroll channels →");
            refresh();
            return true;
        }

        if (event == ftxui::Event::PageUp || event == ftxui::Event::Character('u') ||
            event == ftxui::Event::Character('U')) {
            channel_offset_ = std::max(0, channel_offset_ - 1);
            set_status_message("Scroll channels ←");
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('[')) {
            player_.jump_rows(-8);
            auto state = player_.snapshot();
            std::ostringstream oss;
            oss << "Row ← " << std::setw(2) << std::setfill('0') << std::max(0, state.row);
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character(']')) {
            player_.jump_rows(8);
            auto state = player_.snapshot();
            std::ostringstream oss;
            oss << "Row → " << std::setw(2) << std::setfill('0') << std::max(0, state.row);
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('n') || event == ftxui::Event::Character('N')) {
            info_overlay_ = !info_overlay_;
            about_overlay_ = false;
            set_status_message(info_overlay_ ? "Overlay opened" : "Overlay closed");
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('a') || event == ftxui::Event::Character('A')) {
            about_overlay_ = !about_overlay_;
            info_overlay_ = false;
            info_scroll_position_ = 0;
            set_status_message(about_overlay_ ? "About opened" : "About closed");
            refresh();
            return true;
        }

        if (info_overlay_ && (event == ftxui::Event::ArrowUp || event == ftxui::Event::Character('k'))) {
            info_scroll_position_ = std::max(0, info_scroll_position_ - 1);
            refresh();
            return true;
        }

        if (info_overlay_ && (event == ftxui::Event::ArrowDown || event == ftxui::Event::Character('j'))) {
            info_scroll_position_++;
            refresh();
            return true;
        }

        if (info_overlay_ && event == ftxui::Event::PageUp) {
            info_scroll_position_ = std::max(0, info_scroll_position_ - 10);
            refresh();
            return true;
        }

        if (info_overlay_ && event == ftxui::Event::PageDown) {
            info_scroll_position_ += 10;
            refresh();
            return true;
        }

        if (!info_overlay_ && !about_overlay_ && 
            (event == ftxui::Event::Character('+') || event == ftxui::Event::Character('=') ||
             event == ftxui::Event::ArrowUp)) {
            double volume = player_.get_volume();
            volume = std::min(1.0, volume + 0.05);
            player_.set_volume(volume);
            config_.set_volume(volume);
            std::ostringstream oss;
            oss << "Volume: " << static_cast<int>(std::round(volume * 100)) << "%";
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (!info_overlay_ && !about_overlay_ &&
            (event == ftxui::Event::Character('-') || event == ftxui::Event::Character('_') ||
             event == ftxui::Event::ArrowDown)) {
            double volume = player_.get_volume();
            volume = std::max(0.0, volume - 0.05);
            player_.set_volume(volume);
            config_.set_volume(volume);
            std::ostringstream oss;
            oss << "Volume: " << static_cast<int>(std::round(volume * 100)) << "%";
            set_status_message(oss.str());
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('m') || event == ftxui::Event::Character('M')) {
            double volume = player_.get_volume();
            if (volume > 0.0) {
                last_volume_ = volume;
                player_.set_volume(0.0);
                config_.set_volume(0.0);
                set_status_message("Muted");
            } else {
                double new_volume = last_volume_ > 0.0 ? last_volume_ : 1.0;
                player_.set_volume(new_volume);
                config_.set_volume(new_volume);
                std::ostringstream oss;
                oss << "Volume: " << static_cast<int>(std::round(player_.get_volume() * 100)) << "%";
                set_status_message(oss.str());
            }
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('e') || event == ftxui::Event::Character('E')) {
            AudioEffect current = player_.get_effect();
            AudioEffect next;
            std::string effect_name;
            
            switch (current) {
                case AudioEffect::None:
                    next = AudioEffect::BassBoost;
                    effect_name = "Bass Boost";
                    break;
                case AudioEffect::BassBoost:
                    next = AudioEffect::Echo;
                    effect_name = "Echo";
                    break;
                case AudioEffect::Echo:
                    next = AudioEffect::Reverb;
                    effect_name = "Reverb";
                    break;
                case AudioEffect::Reverb:
                    next = AudioEffect::Flanger;
                    effect_name = "Flanger";
                    break;
                case AudioEffect::Flanger:
                    next = AudioEffect::Phaser;
                    effect_name = "Phaser";
                    break;
                case AudioEffect::Phaser:
                    next = AudioEffect::Chorus;
                    effect_name = "Chorus";
                    break;
                case AudioEffect::Chorus:
                default:
                    next = AudioEffect::None;
                    effect_name = "Off";
                    break;
            }
            
            player_.set_effect(next);
            set_status_message("Effect: " + effect_name);
            refresh();
            return true;
        }

        if (event == ftxui::Event::Character('x') || event == ftxui::Event::Character('X')) {
            if (!export_in_progress_) {
                export_dialog_ = !export_dialog_;
                set_status_message(export_dialog_ ? "Export dialog opened" : "Export dialog closed");
                refresh();
            }
            return true;
        }

        if (export_dialog_ && !export_in_progress_) {
            if (event == ftxui::Event::Tab || event == ftxui::Event::ArrowDown) {
                export_format_selection_ = (export_format_selection_ + 1) % 3;
                refresh();
                return true;
            }
            if (event == ftxui::Event::TabReverse || event == ftxui::Event::ArrowUp) {
                export_format_selection_ = (export_format_selection_ + 2) % 3;
                refresh();
                return true;
            }
            if (event == ftxui::Event::Return) {
                export_in_progress_ = true;
                export_current_ = 0;
                export_total_ = 0;
                export_error_.clear();
                
                std::thread([this, &refresh]() {
                    ExportOptions options;
                    options.sample_rate = 48000;
                    options.channels = 2;
                    
                    switch (export_format_selection_) {
                        case 0:
                            options.format = ExportFormat::WAV;
                            break;
                        case 1:
                            options.format = ExportFormat::MP3;
                            options.mp3_bitrate = 320;
                            break;
                        case 2:
                            options.format = ExportFormat::FLAC;
                            options.flac_compression_level = 5;
                            break;
                    }
                    
                    options.output_path = export_filename_ + AudioExporter::get_extension(options.format);
                    
                    options.progress_callback = [this](std::size_t current, std::size_t total) {
                        export_current_ = current;
                        export_total_ = total;
                        return true;
                    };
                    
                    std::string error_message;
                    bool success = player_.export_to_file(options, error_message);
                    
                    export_in_progress_ = false;
                    if (success) {
                        set_status_message("Export complete: " + options.output_path);
                        export_dialog_ = false;
                    } else {
                        export_error_ = error_message;
                    }
                }).detach();
                
                return true;
            }
        }

        return false;
    });

    std::thread ticker([&] {
        using namespace std::chrono_literals;
        while (loop_running.load()) {
            std::this_thread::sleep_for(50ms);
            screen.PostEvent(ftxui::Event::Custom);
        }
    });

    screen.Loop(component);

    loop_running = false;
    if (ticker.joinable()) {
        ticker.join();
    }

    running_ = false;
}

void Ui::update_history(const TransportState &state) {
    if (state.order < 0 || state.row < 0 || state.channels.empty()) {
        return;
    }

    if (!history_.empty()) {
        const RowRender &back = history_.back();
        if (state.order == back.order && state.row == back.row) {
            return;
        }
        if (state.order < back.order || (state.order == back.order && state.row < back.row)) {
            history_.clear();
        }
    }

    RowRender row;
    row.order = state.order;
    row.pattern = state.pattern;
    row.row = state.row;
    row.channels.reserve(state.channels.size());
    for (const auto &ch : state.channels) {
        row.channels.push_back(ch.line);
    }

    history_.push_back(std::move(row));
    if (static_cast<int>(history_.size()) > history_capacity_) {
        history_.pop_front();
    }

    last_order_ = state.order;
    last_row_ = state.row;
}

ftxui::Element Ui::render(const TransportState &state) {
    using namespace ftxui;

    auto playback = render_playback_info(state);
    auto instruments = render_active_instruments(state);
    auto visualizer = render_header_visualizer(state);
    auto oscilloscope = render_oscilloscope(state);
    auto header = hbox({playback | xflex, instruments, visualizer, oscilloscope});
    
    auto pattern = render_pattern_grid(state) | flex;
    auto status = render_status_bar();
    auto footer = render_footer();

    auto layout = vbox({header, pattern, separator(), status, footer}) |
                  bgcolor(kTheme.background) | color(kTheme.text) | flex;

    if (export_dialog_) {
        auto dimmed_bg = filler() | bgcolor(Color::RGB(0, 0, 0)) | dim;
        return dbox({layout, dimmed_bg, render_export_dialog()});
    }
    if (info_overlay_) {
        auto dimmed_bg = filler() | bgcolor(Color::RGB(0, 0, 0)) | dim;
        return dbox({layout, dimmed_bg, render_info_overlay(state)});
    }
    if (about_overlay_) {
        auto dimmed_bg = filler() | bgcolor(Color::RGB(0, 0, 0)) | dim;
        return dbox({layout, dimmed_bg, render_about_overlay()});
    }
    return layout;
}

ftxui::Element Ui::render_playback_info(const TransportState &state) const {
    using namespace ftxui;

    auto title_line = hbox({text("cli-modplayer v1.3.0") | bold | color(kTheme.accent), filler(),
                            text("github.com/Master290/cli-modplayer") | color(kTheme.text_dim) | dim});

    std::string format_info = player_.module_type();
    if (player_.num_channels() > 0) {
        format_info += " • " + std::to_string(player_.num_channels()) + "ch";
    }
    if (player_.num_patterns() > 0) {
        format_info += " • " + std::to_string(player_.num_patterns()) + "pat";
    }
    
    std::string order_info = format_two_digit(state.order) + "/" + std::to_string(player_.num_orders() - 1);
    
    std::vector<std::vector<Element>> grid_rows;
    grid_rows.push_back({text("Title") | color(kTheme.text_dim), text(player_.title()) | bold | color(kTheme.accent)});
    
    if (!player_.artist().empty() && player_.artist() != "Unknown") {
        grid_rows.push_back({text("Artist") | color(kTheme.text_dim), text(player_.artist()) | bold});
    }
    
    grid_rows.push_back({text("Format") | color(kTheme.text_dim), text(format_info)});
    grid_rows.push_back({text("Tracker") | color(kTheme.text_dim), text(player_.tracker_name())});
    
    std::string stats_info = std::to_string(player_.num_instruments()) + " ins, " + 
                            std::to_string(player_.num_samples()) + " smp";
    grid_rows.push_back({text("Stats") | color(kTheme.text_dim), text(stats_info)});
    
    grid_rows.push_back({text("Order") | color(kTheme.text_dim), text(order_info)});
    grid_rows.push_back({text("Pattern") | color(kTheme.text_dim), text(format_two_digit(state.pattern))});
    grid_rows.push_back({text("Row") | color(kTheme.text_dim), text(format_two_digit(state.row))});
    
    grid_rows.push_back({text("Speed") | color(kTheme.text_dim), text(format_two_digit(state.speed))});
    
    auto info_grid = gridbox(grid_rows);

    auto content = vbox({title_line, separatorLight(), info_grid}) |
                bgcolor(kTheme.panel) | color(kTheme.text);

    return window(text(" Playback ") | color(kTheme.accent), content) | color(kTheme.border);
}

ftxui::Element Ui::render_header_visualizer(const TransportState &state) const {
    using namespace ftxui;

    auto percent_string = [](double value) {
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill(' ') << static_cast<int>(std::round(value * 100.0));
        return oss.str();
    };

    const int bucket_count = std::max<int>(1, static_cast<int>(master_levels_.size()));
    const int bar_width = 2;
    const int separator_width = 1;
    const int total_width = bucket_count * bar_width + (bucket_count - 1) * separator_width;

    Elements bars_elements;
    bars_elements.reserve(static_cast<std::size_t>(bucket_count * 2));

    bool has_signal = false;

    for (int index = 0; index < bucket_count; ++index) {
        double level = index < static_cast<int>(master_levels_.size()) ? std::clamp(master_levels_[static_cast<std::size_t>(index)], 0.0, 1.0) : 0.0;
        if (level > 0.02) {
            has_signal = true;
        }

        auto bar = gaugeUp(level) | 
                  size(WIDTH, EQUAL, bar_width) | 
                  size(HEIGHT, EQUAL, kMasterVisualizerHeight) |
                  color(amplitude_to_color(level)) |
                  bgcolor(kTheme.panel_alt);

        bars_elements.push_back(bar);
        if (index + 1 < bucket_count) {
            bars_elements.push_back(separator() | size(WIDTH, EQUAL, separator_width) | color(kTheme.panel));
        }
    }

    auto bars = hbox(std::move(bars_elements)) | bgcolor(kTheme.panel) |
                size(HEIGHT, EQUAL, kMasterVisualizerHeight) | border | color(kTheme.border);

    double max_peak = 0.0;
    for (double peak : master_peaks_) {
        max_peak = std::max(max_peak, std::clamp(peak, 0.0, 1.0));
        if (peak > 0.02) {
            has_signal = true;
        }
    }

    double avg_level = std::clamp(master_overall_level_, 0.0, 1.0);

    auto stats = hbox({text("Avg ") | color(kTheme.text_dim),
                       text(percent_string(avg_level) + "%") | bold | color(amplitude_to_color(avg_level)),
                       text("   Max ") | color(kTheme.text_dim),
                       text(percent_string(max_peak) + "%") | color(amplitude_to_color(max_peak))}) |
                 size(WIDTH, GREATER_THAN, total_width);

    Elements content = {
        bars,
    };

    if (has_signal || !state.channels.empty()) {
        content.push_back(separatorLight());
        content.push_back(stats);
    } else {
        content.push_back(separatorLight());
        content.push_back(text("Waiting for signal") | color(kTheme.text_dim) | dim);
    }

    auto body = vbox(std::move(content)) | bgcolor(kTheme.panel) | color(kTheme.text);
    return window(text(" Spectrum Analyzer ") | color(kTheme.accent), body) | 
           color(kTheme.border) | size(WIDTH, EQUAL, total_width + 6);
}

ftxui::Element Ui::render_oscilloscope(const TransportState &state) const {
    using namespace ftxui;
    
    constexpr int scope_width = 50;
    constexpr int scope_height = 12;
    
    if (state.waveform_left.empty() || state.waveform_right.empty()) {
        auto content = vbox({
            text("No waveform data") | color(kTheme.text_dim) | dim | center
        }) | size(WIDTH, EQUAL, scope_width) | size(HEIGHT, EQUAL, scope_height) | bgcolor(kTheme.panel);
        return window(text(" Oscilloscope ") | color(kTheme.accent), content) | 
               color(kTheme.border);
    }
    
    const int canvas_width = scope_width * 2;
    const int canvas_height = scope_height * 4;
    auto canvas = Canvas(canvas_width, canvas_height);
    
    const std::size_t num_samples = state.waveform_left.size();
    
    const int mid_left = canvas_height / 4;
    const int amplitude_left = canvas_height / 4 - 2;
    
    for (std::size_t i = 1; i < num_samples; ++i) {
        float prev_sample = std::clamp(state.waveform_left[i - 1], -1.0f, 1.0f);
        float curr_sample = std::clamp(state.waveform_left[i], -1.0f, 1.0f);
        
        int x1 = static_cast<int>((i - 1) * canvas_width / num_samples);
        int x2 = static_cast<int>(i * canvas_width / num_samples);
        
        int y1 = mid_left - static_cast<int>(prev_sample * amplitude_left);
        int y2 = mid_left - static_cast<int>(curr_sample * amplitude_left);
        
        canvas.DrawPointLine(x1, y1, x2, y2, Color::RGB(129, 200, 190));
    }
    
    const int mid_right = canvas_height * 3 / 4;
    const int amplitude_right = canvas_height / 4 - 2;
    
    for (std::size_t i = 1; i < num_samples; ++i) {
        float prev_sample = std::clamp(state.waveform_right[i - 1], -1.0f, 1.0f);
        float curr_sample = std::clamp(state.waveform_right[i], -1.0f, 1.0f);
        
        int x1 = static_cast<int>((i - 1) * canvas_width / num_samples);
        int x2 = static_cast<int>(i * canvas_width / num_samples);
        
        int y1 = mid_right - static_cast<int>(prev_sample * amplitude_right);
        int y2 = mid_right - static_cast<int>(curr_sample * amplitude_right);
        
        canvas.DrawPointLine(x1, y1, x2, y2, Color::RGB(124, 200, 146));
    }
    
    canvas.DrawPointLine(0, mid_left, canvas_width - 1, mid_left, Color::RGB(54, 57, 70));
    canvas.DrawPointLine(0, mid_right, canvas_width - 1, mid_right, Color::RGB(54, 57, 70));
    
    const int separator_y = canvas_height / 2;
    for (int x = 0; x < canvas_width; x += 4) {
        canvas.DrawPoint(x, separator_y, true, Color::RGB(54, 57, 70));
    }
    
    auto labels = hbox({
        text("L") | color(Color::RGB(129, 200, 190)),
        text(" │ ") | color(kTheme.text_dim),
        text("R") | color(Color::RGB(124, 200, 146))
    }) | center | bgcolor(kTheme.panel_alt);
    
    auto canvas_element = ftxui::canvas(std::move(canvas)) | 
                         size(WIDTH, EQUAL, scope_width) | 
                         size(HEIGHT, EQUAL, scope_height) | 
                         bgcolor(kTheme.panel_alt);
    
    auto content = vbox({
        labels,
        separatorLight() | color(kTheme.border),
        canvas_element
    }) | bgcolor(kTheme.panel_alt) | 
        size(WIDTH, EQUAL, scope_width) | 
        size(HEIGHT, EQUAL, scope_height + 2);
    
    return window(text(" Oscilloscope ") | color(kTheme.accent), content) | 
           color(kTheme.border);
}

ftxui::Element Ui::render_active_instruments(const TransportState &state) const {
    using namespace ftxui;

    std::map<int, std::pair<std::string, double>> active_instruments;
    for (const auto &ch : state.channels) {
        if (ch.instrument_index >= 0 && !ch.instrument_name.empty()) {
            double activity = std::max(std::abs(ch.vu_left), std::abs(ch.vu_right));
            
            auto it = active_instruments.find(ch.instrument_index);
            if (it != active_instruments.end()) {
                it->second.second = std::max(it->second.second, activity);
            } else {
                active_instruments[ch.instrument_index] = {ch.instrument_name, activity};
            }
        }
    }

    if (active_instruments.empty()) {
        auto content = vbox({
            text("No instruments playing") | color(kTheme.text_dim) | dim | center
        }) | bgcolor(kTheme.panel);
        return window(text(" Instruments ") | color(kTheme.accent), content) | 
               color(kTheme.border) | size(WIDTH, EQUAL, 35);
    }

    Elements instrument_lines;

    for (const auto &[index, name_activity] : active_instruments) {
        const auto &[name, activity] = name_activity;
        
        std::string display_name = name;
        if (display_name.length() > 20) {
            display_name = display_name.substr(0, 19) + "…";
        }

        auto activity_gauge = gaugeRight(activity) | 
                             color(amplitude_to_color(activity)) | 
                             bgcolor(kTheme.panel_alt) | 
                             size(WIDTH, EQUAL, 8);

        auto line = hbox({
            text(format_two_digit(index + 1)) | color(kTheme.accent) | bold | size(WIDTH, EQUAL, 3),
            text("♪") | color(kTheme.success) | size(WIDTH, EQUAL, 2),
            text(display_name) | color(kTheme.text) | size(WIDTH, EQUAL, 21),
            activity_gauge
        });
        
        instrument_lines.push_back(line);
    }

    auto content = vbox(std::move(instrument_lines)) | bgcolor(kTheme.panel) | color(kTheme.text);
    return window(text(" Instruments ") | color(kTheme.accent), content) | 
           color(kTheme.border) | size(WIDTH, EQUAL, 35);
}

ftxui::Element Ui::render_pattern_grid(const TransportState &state) {
    using namespace ftxui;

    int total_channels = static_cast<int>(state.channels.size());
    if (total_channels == 0 && !history_.empty()) {
        total_channels = static_cast<int>(history_.back().channels.size());
    }

    if (total_channels == 0) {
        return window(text(" Pattern "), paragraph("No channel data") | color(kTheme.text_dim) | center);
    }

    auto terminal = ftxui::Terminal::Size();
    int terminal_width = terminal.dimx > 0 ? terminal.dimx : 80;
    constexpr int kLabelWidth = 8;
    constexpr int kMinColumnWidth = 18;
    constexpr int kOuterPadding = 6;

    int available_width = std::max(40, terminal_width - kOuterPadding);
    
    int possible_columns = std::max(1, (available_width - kLabelWidth) / kMinColumnWidth);
    possible_columns = std::min(possible_columns, std::max(1, total_channels));
    page_columns_ = std::max(1, possible_columns);

    int max_offset = std::max(0, total_channels - page_columns_);
    channel_offset_ = std::clamp(channel_offset_, 0, max_offset);

    int visible_columns = std::max(1, std::min(page_columns_, total_channels - channel_offset_));
    
    int remaining_width = available_width - kLabelWidth;
    int column_width = std::max(kMinColumnWidth, remaining_width / visible_columns);
    
    const int label_width = kLabelWidth;
    const std::string placeholder = channel_placeholder();

    std::vector<std::vector<Element>> grid_rows;
    std::vector<Element> header_cells;
    header_cells.push_back(text("ROW") | bold | center | color(kTheme.accent) |
                           size(WIDTH, EQUAL, label_width) | bgcolor(kTheme.panel_alt));
    for (int col = 0; col < visible_columns; ++col) {
        header_cells.push_back(text(format_channel_label(channel_offset_ + col + 1)) | bold | center |
                                size(WIDTH, EQUAL, column_width) | bgcolor(kTheme.panel_alt) | color(kTheme.accent));
    }
    grid_rows.push_back(std::move(header_cells));

    const RowRender *current_row = nullptr;
    RowRender fallback_row;
    if (!history_.empty()) {
        current_row = &history_.back();
    } else if (!state.channels.empty()) {
        fallback_row.order = state.order;
        fallback_row.pattern = state.pattern;
        fallback_row.row = state.row;
        fallback_row.channels.reserve(state.channels.size());
        for (const auto &ch : state.channels) {
            fallback_row.channels.push_back(ch.line);
        }
        current_row = &fallback_row;
    }

    int history_available = history_.empty() ? 0 : static_cast<int>(history_.size()) - 1;
    int future_available = static_cast<int>(state.preview_rows.size());
    
    int terminal_height = terminal.dimy > 0 ? terminal.dimy : 24;
    
    int ui_overhead = 18;
    int available_height = std::max(10, terminal_height - ui_overhead);
    
    constexpr int kPatternOverhead = 12;
    int total_context_lines = std::max(10, available_height - kPatternOverhead);
    
    int ideal_history = total_context_lines / 2;
    int ideal_future = total_context_lines - ideal_history;
    
    int history_real = std::min(history_available, ideal_history);
    int future_real = std::min(future_available, ideal_future);
    
    int history_shortage = ideal_history - history_real;
    int future_shortage = ideal_future - future_real;
    
    if (history_shortage > 0 && future_available > future_real) {
        int can_compensate = std::min(history_shortage, future_available - future_real);
        future_real += can_compensate;
    }
    
    if (future_shortage > 0 && history_available > history_real) {
        int can_compensate = std::min(future_shortage, history_available - history_real);
        history_real += can_compensate;
    }
    
    int history_total = ideal_history;
    int future_total = ideal_future;
    int history_placeholders = history_total - history_real;
    int future_placeholders = future_total - future_real;

    int total_span = history_total + future_total + 1;
    int history_start = std::max(0, history_available - history_real);

    auto make_row = [&](const std::string &label, const std::vector<std::string> &channels, bool highlight,
                        const std::vector<ftxui::Decorator> &decorators) {
        std::vector<Element> cells;
        auto background = highlight ? kTheme.accent_soft : (grid_rows.size() % 2 == 0 ? kTheme.panel : kTheme.panel_alt);

        Element label_cell = text(label) | center | size(WIDTH, EQUAL, label_width) | bgcolor(background) |
                              color(highlight ? kTheme.text : kTheme.text_dim);
        label_cell = apply_decorators(std::move(label_cell), decorators);
        cells.push_back(std::move(label_cell));

        for (int col = 0; col < visible_columns; ++col) {
            int channel_index = channel_offset_ + col;
            std::string content = placeholder;
            if (channel_index < static_cast<int>(channels.size())) {
                content = channels[static_cast<std::size_t>(channel_index)];
            }

            Element cell = text(content) | size(WIDTH, EQUAL, column_width) | center | bgcolor(background);
            cell = cell | color(highlight ? kTheme.text : color_for_note(content));
            if (highlight) {
                cell = cell | bold;
            }
            cell = apply_decorators(std::move(cell), decorators);
            cells.push_back(std::move(cell));
        }

        grid_rows.push_back(std::move(cells));
    };

    const std::vector<std::string> empty_channels;

    for (int i = 0; i < history_placeholders; ++i) {
        int offset_from_center = -(history_total - i);
        auto decorators = decorators_for_row_index(offset_from_center, total_span);
        make_row("", empty_channels, false, decorators);
    }

    for (int i = 0; i < history_real; ++i) {
        int slot_index = history_placeholders + i;
        int offset_from_center = -(history_total - slot_index);
        const RowRender &row = history_[static_cast<std::size_t>(history_start + i)];
        auto decorators = decorators_for_row_index(offset_from_center, total_span);
        make_row(format_order_row_label(row.order, row.row), row.channels, false, decorators);
    }

    if (current_row) {
        make_row(format_order_row_label(current_row->order, current_row->row), current_row->channels, true, {});
    }

    for (int i = 0; i < future_real; ++i) {
        int offset_from_center = i + 1;
        const PatternRowPreview &preview = state.preview_rows[static_cast<std::size_t>(i)];
        auto decorators = decorators_for_row_index(offset_from_center, total_span);
        make_row(format_order_row_label(preview.order, preview.row), preview.channels, false, decorators);
    }

    for (int i = 0; i < future_placeholders; ++i) {
        int slot_index = future_real + i;
        int offset_from_center = slot_index + 1;
        auto decorators = decorators_for_row_index(offset_from_center, total_span);
        make_row("", empty_channels, false, decorators);
    }

    auto grid = gridbox(grid_rows) | frame;

    auto navigation = [&] {
        bool has_left = channel_offset_ > 0;
        bool has_right = channel_offset_ + visible_columns < total_channels;
        return hbox({text(has_left ? "◀ PgUp" : "       ") | color(kTheme.text_dim), filler(),
                     text(has_right ? "PgDn ▶" : "       ") | color(kTheme.text_dim)}) | bgcolor(kTheme.panel) |
               color(kTheme.text_dim);
    };

    auto visualizers = render_visualizers(state, visible_columns, column_width);

    auto content = vbox({navigation(), separatorLight(), visualizers, separatorLight(), grid}) |
                   bgcolor(kTheme.panel) | color(kTheme.text);

    return window(text(" Pattern ") | color(kTheme.accent), content) | color(kTheme.border);
}

ftxui::Element Ui::render_status_bar() {
    using namespace ftxui;

    auto now = std::chrono::steady_clock::now();
    if (!status_message_.empty() && now >= status_message_until_) {
        status_message_.clear();
    }

    std::string message = status_message_.empty() ? "Ready" : status_message_;
    std::string playback_state = last_state_.paused ? "Paused" : (running_ ? "Playing" : "Stopped");
    auto playback_color = last_state_.paused ? kTheme.warning : kTheme.success;

    double duration = std::max(0.0, player_.duration_seconds());
    double position = std::clamp(last_state_.position_seconds, 0.0, duration > 0.0 ? duration : std::numeric_limits<double>::max());
    double progress_ratio = duration > 0.0 ? std::clamp(position / duration, 0.0, 1.0) : 0.0;
    std::string time_label = format_status_position(last_state_, duration);
    
    double volume = player_.get_volume();
    int volume_percent = static_cast<int>(std::round(volume * 100));
    std::string volume_icon = volume == 0.0 ? "🔇" : (volume < 0.33 ? "🔈" : (volume < 0.66 ? "🔉" : "🔊"));
    std::string volume_label = volume_icon + " " + std::to_string(volume_percent) + "%";

    auto left = text(message) | color(kTheme.text_dim);
    auto progress_bar = gaugeRight(progress_ratio) | color(kTheme.accent) | bgcolor(kTheme.panel_alt) | flex;
    auto center = hbox({progress_bar, text("  "), text(time_label) | color(kTheme.text_dim)}) | flex;
    auto right = hbox({
        text(volume_label) | color(kTheme.text_dim),
        text("  "),
        text(playback_state) | color(playback_color) | bold
    });

    return hbox({left, text("  "), center, text("  "), right}) | bgcolor(kTheme.panel_alt) | color(kTheme.text);
}

ftxui::Element Ui::render_footer() const {
    using namespace ftxui;
    auto shortcuts = text("Space: Play/Pause  [ / ] ±8 rows  ←/→ Orders  PgUp/PgDn Channels  +/- Volume  M Mute  E Effects  X Export  N Info  A About  Q Quit") |
                     color(kTheme.text_dim) | dim;
    return hbox({shortcuts}) | bgcolor(kTheme.background) | color(kTheme.text);
}

ftxui::Element Ui::render_export_dialog() {
    using namespace ftxui;

    Elements dialog_content;
    
    dialog_content.push_back(text("Export Audio") | bold | color(kTheme.accent));
    dialog_content.push_back(separatorLight());
    
    dialog_content.push_back(hbox({
        text("Filename: ") | color(kTheme.text),
        text(export_filename_) | color(kTheme.text) | bold
    }));
    
    dialog_content.push_back(separatorLight());
    
    dialog_content.push_back(text("Format:") | color(kTheme.text));
    
    std::vector<std::tuple<std::string, std::string, bool>> formats = {
        {"WAV", "Uncompressed PCM (best quality, large file)", AudioExporter::is_format_supported(ExportFormat::WAV)},
        {"MP3", "Lossy compression (320 kbps)", AudioExporter::is_format_supported(ExportFormat::MP3)},
        {"FLAC", "Lossless compression (smaller file)", AudioExporter::is_format_supported(ExportFormat::FLAC)}
    };
    
    for (size_t i = 0; i < formats.size(); ++i) {
        auto [name, desc, supported] = formats[i];
        
        std::string prefix = (i == static_cast<size_t>(export_format_selection_)) ? "▶ " : "  ";
        std::string suffix = !supported ? " (not available)" : "";
        
        auto format_line = hbox({
            text(prefix) | color(kTheme.accent),
            text(name) | (supported ? color(kTheme.text) | bold : color(kTheme.text_dim)),
            text(" - " + desc + suffix) | color(kTheme.text_dim)
        });
        
        if (i == static_cast<size_t>(export_format_selection_) && supported) {
            format_line = format_line | bgcolor(kTheme.accent_soft);
        }
        
        dialog_content.push_back(format_line);
    }
    
    dialog_content.push_back(separatorLight());
    
    if (export_in_progress_) {
        dialog_content.push_back(text("Exporting...") | color(kTheme.warning) | bold);
        
        if (export_total_ > 0) {
            float progress = static_cast<float>(export_current_) / static_cast<float>(export_total_);
            int percent = static_cast<int>(progress * 100.0f);
            
            dialog_content.push_back(hbox({
                text(std::to_string(percent) + "%") | color(kTheme.text),
                text(" ") | flex,
                text(std::to_string(export_current_ / 48000) + "s / " + 
                     std::to_string(export_total_ / 48000) + "s") | color(kTheme.text_dim)
            }));
            
            int bar_width = 40;
            int filled = static_cast<int>(progress * bar_width);
            std::string bar_str;
            for (int i = 0; i < filled; ++i) bar_str += "█";
            for (int i = filled; i < bar_width; ++i) bar_str += "░";
            dialog_content.push_back(text(bar_str) | color(kTheme.accent));
        }
    } else if (!export_error_.empty()) {
        dialog_content.push_back(text("Export failed!") | color(kTheme.danger) | bold);
        dialog_content.push_back(text(export_error_) | color(kTheme.text_dim));
        dialog_content.push_back(separatorLight());
        dialog_content.push_back(text("Press X to close") | color(kTheme.text_dim) | dim);
    } else {
        dialog_content.push_back(text("Controls:") | color(kTheme.text_dim) | dim);
        dialog_content.push_back(text("  Tab/↑↓: Select format") | color(kTheme.text_dim) | dim);
        dialog_content.push_back(text("  Enter: Start export") | color(kTheme.text_dim) | dim);
        dialog_content.push_back(text("  X: Close dialog") | color(kTheme.text_dim) | dim);
    }
    
    auto dialog = window(text("Export Audio") | bold,
                        vbox(dialog_content) | size(WIDTH, GREATER_THAN, 60));
    
    return dialog | center | bgcolor(kTheme.background);
}

ftxui::Element Ui::render_info_overlay(const TransportState &state) {
    using namespace ftxui;

    Elements info_lines = {
        text("Title: " + player_.title()) | color(kTheme.text),
        text("Tracker: " + player_.tracker_name()) | color(kTheme.text_dim),
        text("Duration: " + format_time(player_.duration_seconds())) | color(kTheme.text_dim),
        text("Channels: " + std::to_string(state.channels.size())) | color(kTheme.text_dim),
    };

    const auto &message_lines = player_.module_message_lines();
    if (!message_lines.empty()) {
        info_lines.push_back(separatorLight());
        info_lines.push_back(text("Message:") | color(kTheme.accent) | bold);
        for (const auto& line : message_lines) {
            info_lines.push_back(text("  " + line) | color(kTheme.text_dim));
        }
    }

    const auto &instruments = player_.instrument_names();
    if (!instruments.empty()) {
        info_lines.push_back(separatorLight());
        info_lines.push_back(text("Instruments:") | color(kTheme.accent) | bold);
        for (std::size_t i = 0; i < instruments.size(); ++i) {
            std::ostringstream oss;
            oss << std::setw(2) << std::setfill('0') << i + 1 << "  " << instruments[i];
            info_lines.push_back(text(oss.str()) | color(kTheme.text_dim));
        }
    }

    info_lines.push_back(separatorLight());
    info_lines.push_back(text("Press N to close | ↑↓ j/k PgUp/PgDn to scroll") | color(kTheme.text_dim) | dim);

    int visible_height = 28;
    int total_lines = info_lines.size();
    int max_scroll = std::max(0, total_lines - visible_height);
    info_scroll_position_ = std::clamp(info_scroll_position_, 0, max_scroll);

    Elements visible_lines;
    for (int i = info_scroll_position_; i < std::min(total_lines, info_scroll_position_ + visible_height); ++i) {
        visible_lines.push_back(info_lines[i]);
    }

    int actual_height = std::min(total_lines, visible_height);
    
    auto content = vbox(visible_lines) | 
                   bgcolor(kTheme.panel) | color(kTheme.text) | 
                   size(WIDTH, GREATER_THAN, 60) | size(WIDTH, LESS_THAN, 72) | 
                   size(HEIGHT, LESS_THAN, actual_height + 1);
    auto overlay = window(text(" Module Info ") | color(kTheme.accent), content) | color(kTheme.border);

    return overlay | clear_under | center | vcenter;
}

ftxui::Element Ui::render_about_overlay() {
    using namespace ftxui;

    std::string ascii_art = R"(
      ___                   __     __                 
 ____/ (_)_____ _  ___  ___/ /__  / /__ ___ _____ ____
/ __/ / /___/  ' \/ _ \/ _  / _ \/ / _ `/ // / -_) __/
\__/_/_/   /_/_/_/\___/\_,_/ .__/_/\_,_/\_, /\__/_/   
                          /_/          /___/          
)";

    Elements about_lines;
    std::istringstream iss(ascii_art);
    std::string line;
    while (std::getline(iss, line)) {
        about_lines.push_back(text(line) | color(kTheme.accent));
    }

    about_lines.push_back(separator());
    about_lines.push_back(text("Version: 1.3.0") | color(kTheme.text) | bold | center);
    about_lines.push_back(text("") | color(kTheme.text));
    about_lines.push_back(text("by Master290 (daniar@dev.tatar)") | color(kTheme.text_dim) | center);
    about_lines.push_back(text("© 2025 | Licensed under MIT License") | color(kTheme.text_dim) | center);
    about_lines.push_back(text("https://github.com/Master290/cli-modplayer") | color(kTheme.text_dim) | center);
    about_lines.push_back(separator());
    about_lines.push_back(text("A terminal-based MOD/XM/S3M/IT tracker player") | color(kTheme.text_dim) | center);
    about_lines.push_back(text("with real-time visualization") | color(kTheme.text_dim) | center);
    about_lines.push_back(text("") | color(kTheme.text));
    about_lines.push_back(text("Press A to close") | color(kTheme.text_dim) | dim | center);

    auto content = vbox(about_lines) | bgcolor(kTheme.panel) | color(kTheme.text) | 
                   size(WIDTH, LESS_THAN, 82);
    auto overlay = window(text(" About ") | color(kTheme.accent), content) | color(kTheme.border);

    return overlay | clear_under | center | vcenter;
}

ftxui::Elements Ui::render_history_rows(const TransportState &state, int columns, int column_width) {
    using namespace ftxui;
    Elements rows;

    const RowRender *current_row = nullptr;
    RowRender fallback_row;
    if (!history_.empty()) {
        current_row = &history_.back();
    } else if (!state.channels.empty()) {
        fallback_row.order = state.order;
        fallback_row.pattern = state.pattern;
        fallback_row.row = state.row;
        fallback_row.channels.reserve(state.channels.size());
        for (const auto &ch : state.channels) {
            fallback_row.channels.push_back(ch.line);
        }
        current_row = &fallback_row;
    }

    int history_available = history_.empty() ? 0 : static_cast<int>(history_.size()) - 1;
    int future_available = static_cast<int>(state.preview_rows.size());
    
    auto terminal = ftxui::Terminal::Size();
    int terminal_height = terminal.dimy > 0 ? terminal.dimy : 24;
    int available_height = std::max(10, terminal_height - 12);
    int total_desired_lines = available_height - 1;
    int ideal_history = total_desired_lines / 2;
    int ideal_future = total_desired_lines - ideal_history;
    
    int history_total = ideal_history;
    int future_total = ideal_future;

    int history_real = std::min(history_available, history_total);
    int future_real = std::min(future_available, future_total);

    int balanced_real = std::min(history_real, future_real);
    history_real = balanced_real;
    future_real = balanced_real;

    int extra_capacity = std::min(history_total - history_real, future_total - future_real);
    int extra_available = std::min(history_available - history_real, future_available - future_real);
    int extra_real = std::max(0, std::min(extra_capacity, extra_available));
    history_real += extra_real;
    future_real += extra_real;

    int history_placeholders = history_total - history_real;
    int future_placeholders = future_total - future_real;

    int total_span = history_total + future_total + 1;
    int history_start = std::max(0, history_available - history_real);

    const std::string placeholder = channel_placeholder();
    constexpr int kLabelWidth = 8;

    auto make_cells = [&](const std::string &label, const std::vector<std::string> &channels, bool highlight,
                          const std::vector<ftxui::Decorator> &decorators) {
        Elements cells;
        auto background = highlight ? kTheme.accent_soft : (rows.size() % 2 == 0 ? kTheme.panel : kTheme.panel_alt);

        auto label_cell = text(label) | size(WIDTH, EQUAL, kLabelWidth) | center | bgcolor(background) |
                          color(highlight ? kTheme.text : kTheme.text_dim);
        label_cell = apply_decorators(std::move(label_cell), decorators);
        cells.push_back(label_cell);

        for (int col = 0; col < columns; ++col) {
            int channel_index = channel_offset_ + col;
            std::string content = placeholder;
            if (channel_index < static_cast<int>(channels.size())) {
                content = channels[static_cast<std::size_t>(channel_index)];
            }
            auto cell = text(content) | size(WIDTH, EQUAL, column_width) | center | bgcolor(background) |
                        color(highlight ? kTheme.text : color_for_note(content));
            if (highlight) {
                cell = cell | bold;
            }
            cell = apply_decorators(std::move(cell), decorators);
            cells.push_back(cell);
        }

        rows.push_back(hbox(std::move(cells)));
    };

    const std::vector<std::string> empty_channels;

    for (int i = 0; i < history_placeholders; ++i) {
        int offset_from_center = -(history_total - i);
        make_cells("", empty_channels, false,
                   decorators_for_row_index(offset_from_center, total_span));
    }

    for (int i = 0; i < history_real; ++i) {
        int slot_index = history_placeholders + i;
        int offset_from_center = -(history_total - slot_index);
        const RowRender &row = history_[static_cast<std::size_t>(history_start + i)];
        make_cells(format_order_row_label(row.order, row.row), row.channels, false,
                   decorators_for_row_index(offset_from_center, total_span));
    }

    if (current_row) {
        make_cells(format_order_row_label(current_row->order, current_row->row), current_row->channels, true, {});
    }

    for (int i = 0; i < future_real; ++i) {
        int offset_from_center = i + 1;
        const PatternRowPreview &preview = state.preview_rows[static_cast<std::size_t>(i)];
        make_cells(format_order_row_label(preview.order, preview.row), preview.channels, false,
                   decorators_for_row_index(offset_from_center, total_span));
    }

    for (int i = 0; i < future_placeholders; ++i) {
        int slot_index = future_real + i;
        int offset_from_center = slot_index + 1;
        make_cells("", empty_channels, false,
                   decorators_for_row_index(offset_from_center, total_span));
    }

    return rows;
}

ftxui::Element Ui::render_visualizers(const TransportState &state, int columns, int column_width) {
    using namespace ftxui;

    Elements bars;
    constexpr int kLabelWidth = 8;
    bars.push_back(text("VU") | size(WIDTH, EQUAL, kLabelWidth) | center | bgcolor(kTheme.panel_alt) |
                   color(kTheme.text_dim));

    for (int col = 0; col < columns; ++col) {
        int channel_index = channel_offset_ + col;
        double amplitude = 0.0;
        if (channel_index < static_cast<int>(state.channels.size())) {
            const ChannelStatus &status = state.channels[static_cast<std::size_t>(channel_index)];
            amplitude = std::max(std::abs(status.vu_left), std::abs(status.vu_right));
        }
        amplitude = std::clamp(amplitude, 0.0, 1.0);

        double peak = amplitude;
        if (channel_index < static_cast<int>(channel_peaks_.size())) {
            peak = std::max(peak, channel_peaks_[static_cast<std::size_t>(channel_index)]);
        }
        peak = std::clamp(peak, 0.0, 1.0);

        auto gauge = gaugeRight(amplitude) | bgcolor(kTheme.panel_alt) | color(amplitude_to_color(amplitude)) |
                      size(WIDTH, EQUAL, column_width);
        std::ostringstream oss;
        oss << std::setw(3) << std::setfill(' ') << static_cast<int>(std::round(amplitude * 100.0)) << "%  pk "
            << std::setw(3) << static_cast<int>(std::round(peak * 100.0)) << '%';
        auto caption = text(oss.str()) | color(kTheme.text_dim) | center;

        bars.push_back(vbox({gauge, caption}) | bgcolor(kTheme.panel_alt));
    }

    return hbox(std::move(bars));
}

ftxui::Color Ui::color_for_note(const std::string &cell) const {
    if (cell.size() < 3) {
        return kTheme.text_dim;
    }

    char n0 = static_cast<char>(std::toupper(static_cast<unsigned char>(cell[0])));
    char n1 = cell.size() > 1 ? cell[1] : '-';
    if ((n0 == '-' && n1 == '-') || n0 == ' ') {
        return kTheme.text_dim;
    }

    int note_index = -1;
    switch (n0) {
    case 'C': note_index = 0; break;
    case 'D': note_index = 2; break;
    case 'E': note_index = 4; break;
    case 'F': note_index = 5; break;
    case 'G': note_index = 7; break;
    case 'A': note_index = 9; break;
    case 'B': note_index = 11; break;
    default: break;
    }

    if (note_index == -1) {
        return kTheme.text_dim;
    }
    if (n1 == '#') {
        note_index = (note_index + 1) % 12;
    }

    static const std::array<ftxui::Color, 12> palette = {
        ftxui::Color::RGB(239, 71, 111),  ftxui::Color::RGB(255, 182, 99),  ftxui::Color::RGB(255, 213, 153),
        ftxui::Color::RGB(6, 214, 160),  ftxui::Color::RGB(17, 138, 178),  ftxui::Color::RGB(239, 71, 111),
        ftxui::Color::RGB(255, 182, 99), ftxui::Color::RGB(255, 213, 153), ftxui::Color::RGB(6, 214, 160),
        ftxui::Color::RGB(17, 138, 178), ftxui::Color::RGB(76, 201, 240),  ftxui::Color::RGB(150, 199, 255)};

    return palette[static_cast<std::size_t>(note_index)];
}

std::vector<ftxui::Decorator> Ui::decorators_for_row_index(int offset_from_center, int) const {
    std::vector<ftxui::Decorator> decorators;
    int distance = std::abs(offset_from_center);
    
    if (distance == 0) {
        return decorators;
    }

    if (offset_from_center > 0 && distance <= 2) {
        decorators.push_back(ftxui::bold);
    }
    
    return decorators;
}

void Ui::update_visualizer_peaks(const TransportState &state, int total_channels) {
    double decay = std::clamp(last_frame_seconds_ * 1.5, 0.0, 1.0);

    if (total_channels <= 0) {
        for (double &peak : channel_peaks_) {
            peak = std::max(0.0, peak - decay);
        }
    } else if (static_cast<int>(channel_peaks_.size()) < total_channels) {
        channel_peaks_.resize(static_cast<std::size_t>(total_channels), 0.0);
    }

    const int bucket_count = kMasterVisualizerBars;
    if (static_cast<int>(master_levels_.size()) != bucket_count) {
        master_levels_.assign(static_cast<std::size_t>(bucket_count), 0.0);
    }
    if (static_cast<int>(master_peaks_.size()) != bucket_count) {
        master_peaks_.assign(static_cast<std::size_t>(bucket_count), 0.0);
    }

    for (int ch = 0; ch < total_channels; ++ch) {
        double amplitude = 0.0;

        if (ch < static_cast<int>(state.channels.size())) {
            const ChannelStatus &status = state.channels[static_cast<std::size_t>(ch)];
            amplitude = std::max(std::abs(status.vu_left), std::abs(status.vu_right));
        }

        amplitude = std::clamp(amplitude, 0.0, 1.0);

        if (ch < static_cast<int>(channel_peaks_.size())) {
            double &peak = channel_peaks_[static_cast<std::size_t>(ch)];
            peak = std::max(amplitude, peak - decay);
            if (peak < 0.0) {
                peak = 0.0;
            }
        }
    }

    if (static_cast<std::size_t>(total_channels) < channel_peaks_.size()) {
        for (std::size_t i = static_cast<std::size_t>(total_channels); i < channel_peaks_.size(); ++i) {
            channel_peaks_[i] = std::max(0.0, channel_peaks_[i] - decay);
        }
    }

    double smoothing = std::clamp(last_frame_seconds_ * 15.0, 0.2, 0.85);
    double peak_decay = std::clamp(last_frame_seconds_ * 1.2, 0.0, 1.0);

    if (!state.spectrum_bands.empty() && state.spectrum_bands.size() == static_cast<std::size_t>(bucket_count)) {
        double overall_sum = 0.0;
        
        for (int bucket = 0; bucket < bucket_count; ++bucket) {
            double target = state.spectrum_bands[static_cast<std::size_t>(bucket)];
            target = std::clamp(target, 0.0, 1.0);

            double &level = master_levels_[static_cast<std::size_t>(bucket)];
            if (target > 0.01) {
                level = std::clamp(level + (target - level) * smoothing, 0.0, 1.0);
            } else {
                level = std::max(0.0, level - decay);
            }

            double &peak = master_peaks_[static_cast<std::size_t>(bucket)];
            if (target > 0.01) {
                peak = std::max(level, peak - peak_decay);
            } else {
                peak = std::max(0.0, peak - peak_decay);
            }
            
            overall_sum += level;
        }

        double target_overall = overall_sum / static_cast<double>(bucket_count);
        master_overall_level_ = std::clamp(master_overall_level_ + (target_overall - master_overall_level_) * smoothing, 0.0, 1.0);
    } else {
        for (int bucket = 0; bucket < bucket_count; ++bucket) {
            master_levels_[static_cast<std::size_t>(bucket)] = std::max(0.0, master_levels_[static_cast<std::size_t>(bucket)] - decay);
            master_peaks_[static_cast<std::size_t>(bucket)] = std::max(0.0, master_peaks_[static_cast<std::size_t>(bucket)] - peak_decay);
        }
        master_overall_level_ = std::max(0.0, master_overall_level_ - decay);
    }
}
void Ui::set_status_message(const std::string &message, std::chrono::milliseconds duration) {
    status_message_ = message;
    status_message_until_ = std::chrono::steady_clock::now() + duration;
}

} 
