#include "note_formatter.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <string>

namespace tracker {

namespace {
constexpr std::array<const char *, 12> NOTE_NAMES = {
    "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
}

std::string format_note_name(std::optional<int> note) {
    if (!note || *note < 0) {
        return "---";
    }
    auto value = *note;
    auto octave = value / 12;
    auto index = value % 12;
    std::ostringstream oss;
    oss << NOTE_NAMES.at(static_cast<std::size_t>(index)) << octave;
    return oss.str();
}

std::string format_instrument(std::optional<int> instrument) {
    if (!instrument || *instrument <= 0) {
        return "..";
    }
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << *instrument;
    return oss.str();
}

std::string format_volume(std::optional<int> volume) {
    if (!volume || *volume < 0) {
        return "..";
    }
    std::ostringstream oss;
    oss << std::setw(2) << std::setfill('0') << std::clamp(*volume, 0, 64);
    return oss.str();
}

std::string format_effect(std::optional<int> effect, std::optional<int> param) {
    if (!effect && !param) {
        return "...";
    }
    std::ostringstream oss;
    oss << std::uppercase << std::hex;
    if (effect) {
        oss << std::setw(1) << *effect;
    } else {
        oss << '.';
    }
    if (param) {
        oss << std::setw(2) << std::setfill('0') << *param;
    } else {
        oss << "..";
    }
    return oss.str();
}

std::string format_note_event(const NoteEvent &event) {
    std::ostringstream oss;
    oss << format_note_name(event.note) << ' '
        << format_instrument(event.instrument) << ' '
        << format_volume(event.volume) << ' '
        << format_effect(event.effect, event.effect_param);
    return oss.str();
}

}
