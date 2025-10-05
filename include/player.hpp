#pragma once

#include "note_formatter.hpp"
#include "audio_effects.hpp"
#include "audio_exporter.hpp"

#include <complex>
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
    int instrument_index{-1};
    std::string instrument_name;
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
    std::vector<double> spectrum_bands;
    std::vector<float> waveform_left;
    std::vector<float> waveform_right;
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
    
    void set_volume(double volume);
    double get_volume() const noexcept;
    
    void set_effect(AudioEffect effect);
    AudioEffect get_effect() const noexcept;
    
    bool export_to_file(const ExportOptions& options, std::string& error_message);

    TransportState snapshot() const;
    const std::vector<std::string> &instrument_names() const noexcept { return instrument_names_; }
    const std::vector<std::string> &sample_names() const noexcept { return sample_names_; }
    const std::vector<std::string> &module_message_lines() const noexcept { return module_message_lines_; }
    const std::string &title() const noexcept { return title_; }
    const std::string &tracker_name() const noexcept { return tracker_name_; }
    const std::string &artist() const noexcept { return artist_; }
    const std::string &module_type() const noexcept { return module_type_; }
    const std::string &date() const noexcept { return date_; }
    int num_channels() const noexcept { return num_channels_; }
    int num_instruments() const noexcept { return num_instruments_; }
    int num_samples() const noexcept { return num_samples_; }
    int num_patterns() const noexcept { return num_patterns_; }
    int num_orders() const noexcept { return num_orders_; }
    double duration_seconds() const noexcept { return duration_seconds_; }

private:
    void playback_loop();
    void update_state_locked();
    void update_spectrum(const float *audio_data, std::size_t sample_count);
    void update_waveform(const float *audio_data, std::size_t sample_count);

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
    double volume_{1.0};
    AudioEffect current_effect_{AudioEffect::None};
    std::unique_ptr<AudioEffects> audio_effects_;

    mutable std::mutex module_mutex_;
    TransportState state_{};
    std::vector<std::string> instrument_names_;
    std::vector<std::string> sample_names_;
    std::vector<std::string> module_message_lines_;
    std::string title_;
    std::string tracker_name_;
    std::string artist_;
    std::string module_type_;
    std::string date_;
    int num_channels_{0};
    int num_instruments_{0};
    int num_samples_{0};
    int num_patterns_{0};
    int num_orders_{0};
    double duration_seconds_{0.0};

    static constexpr int kSpectrumBands = 20;
    static constexpr int kFFTSize = 2048;
    std::vector<std::complex<float>> fft_buffer_;
    std::size_t fft_write_pos_{0};
    std::vector<double> spectrum_bands_;
    mutable std::mutex spectrum_mutex_;
    
    static constexpr int kWaveformSize = 512;
    std::vector<float> waveform_buffer_left_;
    std::vector<float> waveform_buffer_right_;
    std::size_t waveform_write_pos_{0};
    mutable std::mutex waveform_mutex_;
    
    std::vector<int> channel_instruments_;
};

}
