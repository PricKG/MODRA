#pragma once

#include <string>
#include <string_view>

namespace modra {

struct ParsedNoteDocument {
    std::string title;
    std::string body;
};

std::string build_note_document(std::string_view title, std::string_view body);
ParsedNoteDocument parse_note_document(std::string_view document);

}  // namespace modra
