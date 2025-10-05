#include "audio_effects.hpp"
#include <algorithm>
#include <cmath>

namespace tracker {

AudioEffects::AudioEffects(int sample_rate) 
    : echo_buffer_left_(kEchoBufferSize, 0.0f),
      echo_buffer_right_(kEchoBufferSize, 0.0f),
      reverb_buffer_left_(kReverbBufferSize, 0.0f),
      reverb_buffer_right_(kReverbBufferSize, 0.0f),
      flanger_buffer_left_(kFlangerBufferSize, 0.0f),
      flanger_buffer_right_(kFlangerBufferSize, 0.0f),
      chorus_buffer_left_(kChorusBufferSize, 0.0f),
      chorus_buffer_right_(kChorusBufferSize, 0.0f),
      sample_rate_(sample_rate) {
}

void AudioEffects::apply_effects(float* buffer, std::size_t frame_count, AudioEffect effect) {
    if (effect == AudioEffect::None) {
        return;
    }
    
    switch (effect) {
        case AudioEffect::BassBoost:
            apply_bass_boost(buffer, frame_count);
            break;
        case AudioEffect::Echo:
            apply_echo(buffer, frame_count);
            break;
        case AudioEffect::Reverb:
            apply_reverb(buffer, frame_count);
            break;
        case AudioEffect::Flanger:
            apply_flanger(buffer, frame_count);
            break;
        case AudioEffect::Phaser:
            apply_phaser(buffer, frame_count);
            break;
        case AudioEffect::Chorus:
            apply_chorus(buffer, frame_count);
            break;
        default:
            break;
    }
}

void AudioEffects::apply_bass_boost(float* buffer, std::size_t frame_count) {
    const float alpha = 0.15f;
    const float gain = 1.8f;
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        bass_lp_left_ = bass_lp_left_ + alpha * (buffer[idx] - bass_lp_left_);
        float boosted_left = bass_lp_left_ * gain + buffer[idx] * (1.0f - gain * 0.5f);
        buffer[idx] = std::clamp(boosted_left, -1.0f, 1.0f);
        
        bass_lp_right_ = bass_lp_right_ + alpha * (buffer[idx + 1] - bass_lp_right_);
        float boosted_right = bass_lp_right_ * gain + buffer[idx + 1] * (1.0f - gain * 0.5f);
        buffer[idx + 1] = std::clamp(boosted_right, -1.0f, 1.0f);
    }
}

void AudioEffects::apply_echo(float* buffer, std::size_t frame_count) {
    const float delay_time = 0.25f;
    const float feedback = 0.4f;
    const float mix = 0.3f;
    
    const std::size_t delay_samples = static_cast<std::size_t>(delay_time * static_cast<float>(sample_rate_));
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        std::size_t read_pos = (echo_write_pos_ + kEchoBufferSize - delay_samples) % kEchoBufferSize;
        
        float delayed_left = echo_buffer_left_[read_pos];
        float delayed_right = echo_buffer_right_[read_pos];
        
        float output_left = buffer[idx] * (1.0f - mix) + delayed_left * mix;
        float output_right = buffer[idx + 1] * (1.0f - mix) + delayed_right * mix;
        
        echo_buffer_left_[echo_write_pos_] = buffer[idx] + delayed_left * feedback;
        echo_buffer_right_[echo_write_pos_] = buffer[idx + 1] + delayed_right * feedback;
        
        echo_buffer_left_[echo_write_pos_] = std::clamp(echo_buffer_left_[echo_write_pos_], -1.0f, 1.0f);
        echo_buffer_right_[echo_write_pos_] = std::clamp(echo_buffer_right_[echo_write_pos_], -1.0f, 1.0f);
        
        buffer[idx] = std::clamp(output_left, -1.0f, 1.0f);
        buffer[idx + 1] = std::clamp(output_right, -1.0f, 1.0f);
        
        echo_write_pos_ = (echo_write_pos_ + 1) % kEchoBufferSize;
    }
}

void AudioEffects::apply_reverb(float* buffer, std::size_t frame_count) {
    const float mix = 0.35f;
    const float decay = 0.5f;
    
    const std::array<std::size_t, 4> delays = {
        static_cast<std::size_t>(0.029f * sample_rate_),
        static_cast<std::size_t>(0.037f * sample_rate_),
        static_cast<std::size_t>(0.041f * sample_rate_),
        static_cast<std::size_t>(0.043f * sample_rate_)
    };
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        float reverb_left = 0.0f;
        float reverb_right = 0.0f;
        
        for (auto tap_delay : delays) {
            std::size_t read_pos = (reverb_write_pos_ + kReverbBufferSize - tap_delay) % kReverbBufferSize;
            reverb_left += reverb_buffer_left_[read_pos] * decay;
            reverb_right += reverb_buffer_right_[read_pos] * decay;
        }
        
        reverb_left *= 0.25f;
        reverb_right *= 0.25f;
        
        reverb_buffer_left_[reverb_write_pos_] = buffer[idx] + reverb_left * decay;
        reverb_buffer_right_[reverb_write_pos_] = buffer[idx + 1] + reverb_right * decay;
        
        buffer[idx] = std::clamp(buffer[idx] * (1.0f - mix) + reverb_left * mix, -1.0f, 1.0f);
        buffer[idx + 1] = std::clamp(buffer[idx + 1] * (1.0f - mix) + reverb_right * mix, -1.0f, 1.0f);
        
        reverb_write_pos_ = (reverb_write_pos_ + 1) % kReverbBufferSize;
    }
}

void AudioEffects::apply_flanger(float* buffer, std::size_t frame_count) {
    const float lfo_freq = 0.5f;
    const float depth = 0.003f;
    const float feedback = 0.6f;
    const float mix = 0.5f;
    
    const float lfo_increment = 2.0f * M_PI * lfo_freq / sample_rate_;
    const std::size_t base_delay = static_cast<std::size_t>(0.002f * sample_rate_);
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        float lfo = std::sin(flanger_lfo_phase_);
        flanger_lfo_phase_ += lfo_increment;
        if (flanger_lfo_phase_ >= 2.0f * M_PI) {
            flanger_lfo_phase_ -= 2.0f * M_PI;
        }
        
        std::size_t delay_samples = base_delay + static_cast<std::size_t>((lfo * 0.5f + 0.5f) * depth * sample_rate_);
        delay_samples = std::min(delay_samples, kFlangerBufferSize - 1);
        
        std::size_t read_pos = (flanger_write_pos_ + kFlangerBufferSize - delay_samples) % kFlangerBufferSize;
        
        float delayed_left = flanger_buffer_left_[read_pos];
        float delayed_right = flanger_buffer_right_[read_pos];
        
        flanger_buffer_left_[flanger_write_pos_] = buffer[idx] + delayed_left * feedback;
        flanger_buffer_right_[flanger_write_pos_] = buffer[idx + 1] + delayed_right * feedback;
        
        buffer[idx] = std::clamp(buffer[idx] * (1.0f - mix) + delayed_left * mix, -1.0f, 1.0f);
        buffer[idx + 1] = std::clamp(buffer[idx + 1] * (1.0f - mix) + delayed_right * mix, -1.0f, 1.0f);
        
        flanger_write_pos_ = (flanger_write_pos_ + 1) % kFlangerBufferSize;
    }
}

void AudioEffects::apply_phaser(float* buffer, std::size_t frame_count) {
    const float lfo_freq = 0.4f;
    const float lfo_increment = 2.0f * M_PI * lfo_freq / sample_rate_;
    const float feedback = 0.7f;
    const float mix = 0.5f;
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        float lfo = std::sin(phaser_lfo_phase_);
        phaser_lfo_phase_ += lfo_increment;
        if (phaser_lfo_phase_ >= 2.0f * M_PI) {
            phaser_lfo_phase_ -= 2.0f * M_PI;
        }
        
        float apf_coeff = 0.3f + (lfo * 0.5f + 0.5f) * 0.5f;
        
        float left_in = buffer[idx];
        for (int stage = 0; stage < 4; ++stage) {
            float output = -left_in + phaser_state_left_[stage];
            phaser_state_left_[stage] = left_in + output * apf_coeff;
            left_in = output;
        }
        
        float right_in = buffer[idx + 1];
        for (int stage = 0; stage < 4; ++stage) {
            float output = -right_in + phaser_state_right_[stage];
            phaser_state_right_[stage] = right_in + output * apf_coeff;
            right_in = output;
        }
        
        buffer[idx] = std::clamp(buffer[idx] * (1.0f - mix) + left_in * mix + left_in * feedback * 0.3f, -1.0f, 1.0f);
        buffer[idx + 1] = std::clamp(buffer[idx + 1] * (1.0f - mix) + right_in * mix + right_in * feedback * 0.3f, -1.0f, 1.0f);
    }
}

void AudioEffects::apply_chorus(float* buffer, std::size_t frame_count) {
    const float lfo_freq1 = 0.7f;
    const float lfo_freq2 = 1.1f;
    const float depth = 0.002f;
    const float mix = 0.4f;
    
    const float lfo_increment1 = 2.0f * M_PI * lfo_freq1 / sample_rate_;
    const float lfo_increment2 = 2.0f * M_PI * lfo_freq2 / sample_rate_;
    const std::size_t base_delay = static_cast<std::size_t>(0.020f * sample_rate_);
    
    for (std::size_t i = 0; i < frame_count; ++i) {
        std::size_t idx = i * 2;
        
        float lfo1 = std::sin(chorus_lfo_phase1_);
        float lfo2 = std::sin(chorus_lfo_phase2_);
        
        chorus_lfo_phase1_ += lfo_increment1;
        chorus_lfo_phase2_ += lfo_increment2;
        
        if (chorus_lfo_phase1_ >= 2.0f * M_PI) chorus_lfo_phase1_ -= 2.0f * M_PI;
        if (chorus_lfo_phase2_ >= 2.0f * M_PI) chorus_lfo_phase2_ -= 2.0f * M_PI;
        
        std::size_t delay1 = base_delay + static_cast<std::size_t>((lfo1 * 0.5f + 0.5f) * depth * sample_rate_);
        delay1 = std::min(delay1, kChorusBufferSize - 1);
        std::size_t read_pos1 = (chorus_write_pos_ + kChorusBufferSize - delay1) % kChorusBufferSize;
        
        std::size_t delay2 = base_delay + static_cast<std::size_t>((lfo2 * 0.5f + 0.5f) * depth * sample_rate_);
        delay2 = std::min(delay2, kChorusBufferSize - 1);
        std::size_t read_pos2 = (chorus_write_pos_ + kChorusBufferSize - delay2) % kChorusBufferSize;
        
        float voice1_left = chorus_buffer_left_[read_pos1];
        float voice1_right = chorus_buffer_right_[read_pos1];
        float voice2_left = chorus_buffer_left_[read_pos2];
        float voice2_right = chorus_buffer_right_[read_pos2];
        
        chorus_buffer_left_[chorus_write_pos_] = buffer[idx];
        chorus_buffer_right_[chorus_write_pos_] = buffer[idx + 1];
        
        float chorused_left = (voice1_left + voice2_left) * 0.5f;
        float chorused_right = (voice1_right + voice2_right) * 0.5f;
        
        buffer[idx] = std::clamp(buffer[idx] * (1.0f - mix) + chorused_left * mix, -1.0f, 1.0f);
        buffer[idx + 1] = std::clamp(buffer[idx + 1] * (1.0f - mix) + chorused_right * mix, -1.0f, 1.0f);
        
        chorus_write_pos_ = (chorus_write_pos_ + 1) % kChorusBufferSize;
    }
}

}
