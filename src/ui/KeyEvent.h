#pragma once

#include <ftxui/component/event.hpp>

namespace modra {

inline bool shortcut(const ftxui::Event& event, char lowercase) {
    const char uppercase = lowercase >= 'a' && lowercase <= 'z'
                               ? static_cast<char>(lowercase - 'a' + 'A')
                               : lowercase;
    return event == ftxui::Event::Character(lowercase) || event == ftxui::Event::Character(uppercase);
}

}  // namespace modra
