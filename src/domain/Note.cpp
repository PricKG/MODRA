#include "domain/Note.h"

#include <stdexcept>

namespace modra {

std::string_view note_type_name(NoteType type) {
    switch (type) {
        case NoteType::general: return "general";
        case NoteType::technical: return "technical";
        case NoteType::solution: return "solution";
        case NoteType::meeting: return "meeting";
        case NoteType::sql: return "sql";
        case NoteType::procedure: return "procedure";
        case NoteType::configuration: return "configuration";
        case NoteType::reference: return "reference";
    }
    throw std::invalid_argument("Tipo de nota inválido.");
}

std::string_view note_type_label(NoteType type) {
    switch (type) {
        case NoteType::general: return "General";
        case NoteType::technical: return "Técnica";
        case NoteType::solution: return "Solución";
        case NoteType::meeting: return "Reunión";
        case NoteType::sql: return "SQL";
        case NoteType::procedure: return "Procedimiento";
        case NoteType::configuration: return "Configuración";
        case NoteType::reference: return "Referencia";
    }
    throw std::invalid_argument("Tipo de nota inválido.");
}

NoteType note_type_from_name(std::string_view name) {
    if (name == "general") return NoteType::general;
    if (name == "technical") return NoteType::technical;
    if (name == "solution") return NoteType::solution;
    if (name == "meeting") return NoteType::meeting;
    if (name == "sql") return NoteType::sql;
    if (name == "procedure") return NoteType::procedure;
    if (name == "configuration") return NoteType::configuration;
    if (name == "reference") return NoteType::reference;
    throw std::runtime_error("SQLite contiene un tipo de nota inválido: " + std::string(name));
}

}  // namespace modra
