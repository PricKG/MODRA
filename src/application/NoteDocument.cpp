#include "application/NoteDocument.h"

#include <stdexcept>
#include <utility>

namespace modra {
namespace {

constexpr std::string_view document_help = R"(<!--
MODRA_DOCUMENT_V1

Formato:
- El primer encabezado "# " es el título del documento.
- Todo lo que aparece después es el cuerpo en Markdown.
- Se permiten subtítulos, listas, tablas, enlaces y bloques de código.
-->

)";

std::string_view trim_title(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}

}  // namespace

std::string build_note_document(std::string_view title, std::string_view body) {
    title = trim_title(title);
    if (title.empty()) {
        throw std::invalid_argument("El documento debe contener un título con el formato: # Título");
    }

    std::string document;
    document.reserve(document_help.size() + title.size() + body.size() + 4);
    document.append(document_help);
    document.append("# ");
    document.append(title);
    document.append("\n\n");
    document.append(body);
    return document;
}

ParsedNoteDocument parse_note_document(std::string_view document) {
    if (document.starts_with("\xEF\xBB\xBF")) document.remove_prefix(3);

    bool inside_html_comment = false;
    char fence_character = '\0';
    std::size_t position = 0;
    while (position <= document.size()) {
        const std::size_t line_end = document.find('\n', position);
        const std::size_t next = line_end == std::string_view::npos ? document.size() : line_end + 1;
        std::string_view line = document.substr(position, (line_end == std::string_view::npos ? document.size() : line_end) - position);
        if (line.ends_with('\r')) line.remove_suffix(1);

        const auto first_visible = line.find_first_not_of(" \t");
        const std::string_view visible = first_visible == std::string_view::npos ? std::string_view{} : line.substr(first_visible);

        if (inside_html_comment) {
            if (visible.find("-->") != std::string_view::npos) inside_html_comment = false;
        } else if (visible.starts_with("<!--")) {
            inside_html_comment = visible.find("-->") == std::string_view::npos;
        } else {
            const char possible_fence = visible.empty() ? '\0' : visible.front();
            if (possible_fence == '`' || possible_fence == '~') {
                std::size_t fence_length = 0;
                while (fence_length < visible.size() && visible[fence_length] == possible_fence) ++fence_length;
                if (fence_length >= 3) {
                    if (fence_character == '\0') fence_character = possible_fence;
                    else if (fence_character == possible_fence) fence_character = '\0';
                    position = next;
                    if (line_end == std::string_view::npos) break;
                    continue;
                }
            }

            if (fence_character == '\0' && line.starts_with("# ")) {
                const std::string_view title = trim_title(line.substr(2));
                if (title.empty()) {
                    throw std::invalid_argument("El documento debe contener un título con el formato: # Título");
                }

                std::size_t body_start = next;
                if (body_start < document.size()) {
                    const std::size_t separator_end = document.find('\n', body_start);
                    const std::size_t separator_length =
                        (separator_end == std::string_view::npos ? document.size() : separator_end) - body_start;
                    std::string_view separator = document.substr(body_start, separator_length);
                    if (separator.ends_with('\r')) separator.remove_suffix(1);
                    if (separator.find_first_not_of(" \t") == std::string_view::npos) {
                        body_start = separator_end == std::string_view::npos ? document.size() : separator_end + 1;
                    }
                }

                std::string body(document.substr(body_start));
                if (body.find_first_not_of(" \t\r\n") == std::string::npos) {
                    throw std::invalid_argument("El cuerpo del documento no puede estar vacío.");
                }
                return {std::string(title), std::move(body)};
            }
        }

        position = next;
        if (line_end == std::string_view::npos) break;
    }

    throw std::invalid_argument("El documento debe contener un título con el formato: # Título");
}

}  // namespace modra
