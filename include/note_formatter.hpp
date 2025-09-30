#pragma once

#include <string>
#include <optional>

namespace tracker {

struct NoteEvent {
    std::optional<int> note;
    std::optional<int> instrument;
    std::optional<int> volume;
    std::optional<int> effect;
    std::optional<int> effect_param;
};

std::string format_note_name(std::optional<int> note);
std::string format_instrument(std::optional<int> instrument);
std::string format_volume(std::optional<int> volume);
std::string format_effect(std::optional<int> effect, std::optional<int> param);
std::string format_note_event(const NoteEvent &event);

}
