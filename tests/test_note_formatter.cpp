#include "note_formatter.hpp"

#include <cassert>
#include <iostream>
#include <optional>

using tracker::NoteEvent;
using tracker::format_effect;
using tracker::format_instrument;
using tracker::format_note_event;
using tracker::format_note_name;
using tracker::format_volume;

int main() {
    assert(format_note_name(std::optional<int>{0}) == "C-0");
    assert(format_note_name(std::optional<int>{13}) == "C#1");
    assert(format_note_name(std::nullopt) == "---");

    assert(format_instrument(std::optional<int>{5}) == "05");
    assert(format_instrument(std::nullopt) == "..");

    assert(format_volume(std::optional<int>{64}) == "64");
    assert(format_volume(std::optional<int>{-1}) == "..");

    assert(format_effect(std::optional<int>{0xA}, std::optional<int>{0x0F}) == "A0F");
    assert(format_effect(std::nullopt, std::nullopt) == "...");

    NoteEvent event;
    event.note = 24;
    event.instrument = 2;
    event.volume = 48;
    event.effect = 0x0;
    event.effect_param = 0x00;

    assert(format_note_event(event) == "C-2 02 48 000");

    std::cout << "All note formatter tests passed." << std::endl;
    return 0;
}
