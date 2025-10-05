#pragma once

#include <vector>
#include <cstddef>
#include <array>

namespace tracker {

enum class AudioEffect {
    None,
    BassBoost,
    Echo,
    Reverb,
    Flanger,
    Phaser,
    Chorus
};

class AudioEffects {
public:
    AudioEffects(int sample_rate);
    
    void apply_effects(float* buffer, std::size_t frame_count, AudioEffect effect);
    
private:
    void apply_bass_boost(float* buffer, std::size_t frame_count);
    void apply_echo(float* buffer, std::size_t frame_count);
    void apply_reverb(float* buffer, std::size_t frame_count);
    void apply_flanger(float* buffer, std::size_t frame_count);
    void apply_phaser(float* buffer, std::size_t frame_count);
    void apply_chorus(float* buffer, std::size_t frame_count);
    
    float bass_lp_left_{0.0f};
    float bass_lp_right_{0.0f};
    
    static constexpr std::size_t kEchoBufferSize = 48000;
    std::vector<float> echo_buffer_left_;
    std::vector<float> echo_buffer_right_;
    std::size_t echo_write_pos_{0};
    
    static constexpr std::size_t kReverbBufferSize = 96000;
    std::vector<float> reverb_buffer_left_;
    std::vector<float> reverb_buffer_right_;
    std::size_t reverb_write_pos_{0};
    
    static constexpr std::size_t kFlangerBufferSize = 4800;
    std::vector<float> flanger_buffer_left_;
    std::vector<float> flanger_buffer_right_;
    std::size_t flanger_write_pos_{0};
    float flanger_lfo_phase_{0.0f};
    
    std::array<float, 4> phaser_state_left_{};
    std::array<float, 4> phaser_state_right_{};
    float phaser_lfo_phase_{0.0f};
    
    static constexpr std::size_t kChorusBufferSize = 9600;
    std::vector<float> chorus_buffer_left_;
    std::vector<float> chorus_buffer_right_;
    std::size_t chorus_write_pos_{0};
    float chorus_lfo_phase1_{0.0f};
    float chorus_lfo_phase2_{0.0f};
    
    int sample_rate_;
};

}
