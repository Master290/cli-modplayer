#ifndef AUDIO_EXPORTER_HPP
#define AUDIO_EXPORTER_HPP

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace tracker {

enum class ExportFormat {
    WAV,
    MP3,
    FLAC
};

struct ExportOptions {
    ExportFormat format = ExportFormat::WAV;
    std::string output_path;
    int sample_rate = 48000;
    int channels = 2;
    int mp3_bitrate = 320;
    int flac_compression_level = 5;
    
    std::function<bool(std::size_t, std::size_t)> progress_callback;
};

class AudioExporter {
public:
    AudioExporter() = default;
    ~AudioExporter() = default;

    bool export_audio(const std::vector<float>& audio_data,
                     const ExportOptions& options,
                     std::string& error_message);

    static bool is_format_supported(ExportFormat format);
    
    static std::string get_extension(ExportFormat format);
    
    static std::string get_format_name(ExportFormat format);

private:
    bool export_wav(const std::vector<float>& audio_data,
                   const ExportOptions& options,
                   std::string& error_message);
    
    bool export_mp3(const std::vector<float>& audio_data,
                   const ExportOptions& options,
                   std::string& error_message);
    
    bool export_flac(const std::vector<float>& audio_data,
                    const ExportOptions& options,
                    std::string& error_message);
    
    static std::int16_t float_to_int16(float sample);
    
    static void write_le16(std::vector<std::uint8_t>& buffer, std::uint16_t value);
    static void write_le32(std::vector<std::uint8_t>& buffer, std::uint32_t value);
};

} 

#endif
