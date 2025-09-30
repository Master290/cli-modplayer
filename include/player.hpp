#pragma once

#include "note_formatter.hpp"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <libopenmpt/libopenmpt.hpp>
#include <portaudio.h>

namespace tracker {

struct ChannelStatus {
    std::string line;
    double vu_left{};
    double vu_right{};
};

struct PatternRowPreview {
    int order{0};
    int pattern{0};
    int row{0};
    std::vector<std::string> channels;
};

struct TransportState {
    int order{-1};
    int pattern{-1};
    int row{-1};
    int speed{-1};
    double position_seconds{0.0};
    bool paused{false};
    bool finished{false};
    std::vector<ChannelStatus> channels;
    std::vector<PatternRowPreview> preview_rows;
};

class Player {
public:
    Player(const std::string &path, int sample_rate = 48000, int buffer_size = 1024);
    ~Player();

    void start();
    void stop();
    void toggle_pause();
    void jump_to_order(int delta);
    void jump_rows(int delta_rows);

    TransportState snapshot() const;
    const std::vector<std::string> &instrument_names() const noexcept { return instrument_names_; }
    const std::vector<std::string> &sample_names() const noexcept { return sample_names_; }
    const std::vector<std::string> &module_message_lines() const noexcept { return module_message_lines_; }
    const std::string &title() const noexcept { return title_; }
    const std::string &tracker_name() const noexcept { return tracker_name_; }
    double duration_seconds() const noexcept { return duration_seconds_; }

private:
    void playback_loop();
    void update_state_locked();

private:
    std::unique_ptr<openmpt::module> module_;
    PaStream *stream_{nullptr};
    bool pa_initialized_{false};
    std::thread playback_thread_;
    int sample_rate_;
    int buffer_size_;

    mutable std::mutex state_mutex_;
    std::condition_variable pause_cv_;
    bool running_{false};
    bool paused_{false};
    bool stop_requested_{false};
    bool finished_{false};
    bool stream_running_{false};

    mutable std::mutex module_mutex_;
    TransportState state_{};
    std::vector<std::string> instrument_names_;
    std::vector<std::string> sample_names_;
    std::vector<std::string> module_message_lines_;
    std::string title_;
    std::string tracker_name_;
    double duration_seconds_{0.0};
};

}
