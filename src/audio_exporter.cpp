#include "audio_exporter.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef HAVE_LAME
#include <lame/lame.h>
#endif

#ifdef HAVE_FLAC
#include <FLAC/stream_encoder.h>
#endif

namespace tracker {

bool AudioExporter::is_format_supported(ExportFormat format) {
    switch (format) {
        case ExportFormat::WAV:
            return true;
        case ExportFormat::MP3:
#ifdef HAVE_LAME
            return true;
#else
            return false;
#endif
        case ExportFormat::FLAC:
#ifdef HAVE_FLAC
            return true;
#else
            return false;
#endif
    }
    return false;
}

std::string AudioExporter::get_extension(ExportFormat format) {
    switch (format) {
        case ExportFormat::WAV:  return ".wav";
        case ExportFormat::MP3:  return ".mp3";
        case ExportFormat::FLAC: return ".flac";
    }
    return ".wav";
}

std::string AudioExporter::get_format_name(ExportFormat format) {
    switch (format) {
        case ExportFormat::WAV:  return "WAV (PCM)";
        case ExportFormat::MP3:  return "MP3 (Lossy)";
        case ExportFormat::FLAC: return "FLAC (Lossless)";
    }
    return "Unknown";
}

std::int16_t AudioExporter::float_to_int16(float sample) {
    sample = std::clamp(sample, -1.0f, 1.0f);
    
    if (sample >= 0.0f) {
        return static_cast<std::int16_t>(sample * 32767.0f);
    } else {
        return static_cast<std::int16_t>(sample * 32768.0f);
    }
}

void AudioExporter::write_le16(std::vector<std::uint8_t>& buffer, std::uint16_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
}

void AudioExporter::write_le32(std::vector<std::uint8_t>& buffer, std::uint32_t value) {
    buffer.push_back(value & 0xFF);
    buffer.push_back((value >> 8) & 0xFF);
    buffer.push_back((value >> 16) & 0xFF);
    buffer.push_back((value >> 24) & 0xFF);
}

bool AudioExporter::export_audio(const std::vector<float>& audio_data,
                                 const ExportOptions& options,
                                 std::string& error_message) {
    if (audio_data.empty()) {
        error_message = "No audio data to export";
        return false;
    }
    
    if (options.output_path.empty()) {
        error_message = "Output path not specified";
        return false;
    }
    
    if (!is_format_supported(options.format)) {
        error_message = "Format not supported (missing library)";
        return false;
    }
    
    switch (options.format) {
        case ExportFormat::WAV:
            return export_wav(audio_data, options, error_message);
        case ExportFormat::MP3:
            return export_mp3(audio_data, options, error_message);
        case ExportFormat::FLAC:
            return export_flac(audio_data, options, error_message);
    }
    
    error_message = "Unknown export format";
    return false;
}

bool AudioExporter::export_wav(const std::vector<float>& audio_data,
                               const ExportOptions& options,
                               std::string& error_message) {
    try {
        std::ofstream file(options.output_path, std::ios::binary);
        if (!file) {
            error_message = "Failed to open output file";
            return false;
        }
        
        const std::uint32_t sample_count = audio_data.size() / options.channels;
        const std::uint16_t bits_per_sample = 16;
        const std::uint32_t byte_rate = options.sample_rate * options.channels * (bits_per_sample / 8);
        const std::uint16_t block_align = options.channels * (bits_per_sample / 8);
        const std::uint32_t data_size = sample_count * options.channels * (bits_per_sample / 8);
        
        std::vector<std::uint8_t> header;
        
        header.insert(header.end(), {'R', 'I', 'F', 'F'});
        write_le32(header, 36 + data_size);
        header.insert(header.end(), {'W', 'A', 'V', 'E'});
        
        header.insert(header.end(), {'f', 'm', 't', ' '});
        write_le32(header, 16);
        write_le16(header, 1);
        write_le16(header, options.channels);
        write_le32(header, options.sample_rate);
        write_le32(header, byte_rate);
        write_le16(header, block_align);
        write_le16(header, bits_per_sample);
        
        header.insert(header.end(), {'d', 'a', 't', 'a'});
        write_le32(header, data_size);
        
        file.write(reinterpret_cast<const char*>(header.data()), header.size());
        
        std::vector<std::int16_t> pcm_data;
        pcm_data.reserve(audio_data.size());
        
        for (std::size_t i = 0; i < audio_data.size(); ++i) {
            if (options.progress_callback && i % 4800 == 0) {
                if (!options.progress_callback(i, audio_data.size())) {
                    error_message = "Export cancelled by user";
                    file.close();
                    std::remove(options.output_path.c_str());
                    return false;
                }
            }
            
            pcm_data.push_back(float_to_int16(audio_data[i]));
        }
        
        file.write(reinterpret_cast<const char*>(pcm_data.data()), 
                  pcm_data.size() * sizeof(std::int16_t));
        
        file.close();
        return true;
        
    } catch (const std::exception& e) {
        error_message = std::string("WAV export failed: ") + e.what();
        return false;
    }
}

bool AudioExporter::export_mp3(const std::vector<float>& audio_data,
                               const ExportOptions& options,
                               std::string& error_message) {
#ifdef HAVE_LAME
    try {
        lame_t lame = lame_init();
        if (!lame) {
            error_message = "Failed to initialize LAME encoder";
            return false;
        }
        
        lame_set_num_channels(lame, options.channels);
        lame_set_in_samplerate(lame, options.sample_rate);
        lame_set_brate(lame, options.mp3_bitrate);
        lame_set_mode(lame, options.channels == 2 ? STEREO : MONO);
        lame_set_quality(lame, 2);
        
        if (lame_init_params(lame) < 0) {   
            error_message = "Failed to set LAME parameters";
            lame_close(lame);
            return false;
        }
        
        std::ofstream file(options.output_path, std::ios::binary);
        if (!file) {
            error_message = "Failed to open output file";
            lame_close(lame);
            return false;
        }
        
        const std::size_t sample_count = audio_data.size() / options.channels;
        std::vector<float> left_channel(sample_count);
        std::vector<float> right_channel(sample_count);
        
        for (std::size_t i = 0; i < sample_count; ++i) {
            left_channel[i] = audio_data[i * options.channels];
            if (options.channels == 2) {
                right_channel[i] = audio_data[i * options.channels + 1];
            }
        }
        
        std::vector<unsigned char> result;
        
        const std::size_t chunk_size = 1152;
        std::vector<unsigned char> mp3_buffer(chunk_size * 2 * 5 / 4 + 7200);
        
        for (std::size_t i = 0; i < sample_count; i += chunk_size) {
            if (options.progress_callback) {
                if (!options.progress_callback(i * options.channels, audio_data.size())) {
                    error_message = "Export cancelled by user";
                    file.close();
                    lame_close(lame);
                    std::remove(options.output_path.c_str());
                    return false;
                }
            }
            
            std::size_t samples_to_encode = std::min(chunk_size, sample_count - i);
            
            int encoded_bytes = lame_encode_buffer_ieee_float(
                lame,
                left_channel.data() + i,
                options.channels == 2 ? right_channel.data() + i : left_channel.data() + i,
                samples_to_encode,
                mp3_buffer.data(),
                mp3_buffer.size()
            );
            
            if (encoded_bytes < 0) {
                error_message = "LAME encoding error";
                file.close();
                lame_close(lame);
                return false;
            }
            
            if (encoded_bytes > 0) {
                file.write(reinterpret_cast<const char*>(mp3_buffer.data()), encoded_bytes);
            }
        }
        
        int encoded_bytes = lame_encode_flush(lame, mp3_buffer.data(), mp3_buffer.size());
        if (encoded_bytes > 0) {
            file.write(reinterpret_cast<const char*>(mp3_buffer.data()), encoded_bytes);
        }
        
        file.close();
        lame_close(lame);
        return true;
        
    } catch (const std::exception& e) {
        error_message = std::string("MP3 export failed: ") + e.what();
        return false;
    }
#else
    error_message = "MP3 support not compiled (LAME library required)";
    return false;
#endif
}

bool AudioExporter::export_flac(const std::vector<float>& audio_data,
                                const ExportOptions& options,
                                std::string& error_message) {
#ifdef HAVE_FLAC
    try {
        FLAC__StreamEncoder* encoder = FLAC__stream_encoder_new();
        if (!encoder) {
            error_message = "Failed to create FLAC encoder";
            return false;
        }
        
        FLAC__stream_encoder_set_channels(encoder, options.channels);
        FLAC__stream_encoder_set_bits_per_sample(encoder, 16);
        FLAC__stream_encoder_set_sample_rate(encoder, options.sample_rate);
        FLAC__stream_encoder_set_compression_level(encoder, options.flac_compression_level);
        
        FLAC__StreamEncoderInitStatus init_status = 
            FLAC__stream_encoder_init_file(encoder, options.output_path.c_str(), nullptr, nullptr);
        
        if (init_status != FLAC__STREAM_ENCODER_INIT_STATUS_OK) {
            error_message = "Failed to initialize FLAC encoder: ";
            error_message += FLAC__StreamEncoderInitStatusString[init_status];
            FLAC__stream_encoder_delete(encoder);
            return false;
        }
        
        const std::size_t sample_count = audio_data.size() / options.channels;
        std::vector<FLAC__int32> pcm_buffer(audio_data.size());
        
        for (std::size_t i = 0; i < audio_data.size(); ++i) {
            pcm_buffer[i] = static_cast<FLAC__int32>(float_to_int16(audio_data[i]));
        }
        
        const std::size_t chunk_size = 4096;
        for (std::size_t i = 0; i < sample_count; i += chunk_size) {
            if (options.progress_callback) {
                if (!options.progress_callback(i * options.channels, audio_data.size())) {
                    error_message = "Export cancelled by user";
                    FLAC__stream_encoder_finish(encoder);
                    FLAC__stream_encoder_delete(encoder);
                    std::remove(options.output_path.c_str());
                    return false;
                }
            }
            
            std::size_t samples_to_encode = std::min(chunk_size, sample_count - i);
            
            if (!FLAC__stream_encoder_process_interleaved(
                    encoder,
                    pcm_buffer.data() + (i * options.channels),
                    samples_to_encode)) {
                error_message = "FLAC encoding error";
                FLAC__stream_encoder_finish(encoder);
                FLAC__stream_encoder_delete(encoder);
                return false;
            }
        }
        
        FLAC__stream_encoder_finish(encoder);
        FLAC__stream_encoder_delete(encoder);
        return true;
        
    } catch (const std::exception& e) {
        error_message = std::string("FLAC export failed: ") + e.what();
        return false;
    }
#else
    error_message = "FLAC support not compiled (libFLAC library required)";
    return false;
#endif
}

} 
