#include "domain/Project.h"

#include <stdexcept>

namespace modra {

std::string_view project_status_name(ProjectStatus status) {
    switch (status) {
        case ProjectStatus::planned:
            return "planned";
        case ProjectStatus::active:
            return "active";
        case ProjectStatus::paused:
            return "paused";
        case ProjectStatus::completed:
            return "completed";
        case ProjectStatus::archived:
            return "archived";
    }
    throw std::invalid_argument("Estado de proyecto inválido.");
}

std::string_view project_status_label(ProjectStatus status) {
    switch (status) {
        case ProjectStatus::planned:
            return "Planificado";
        case ProjectStatus::active:
            return "Activo";
        case ProjectStatus::paused:
            return "En pausa";
        case ProjectStatus::completed:
            return "Finalizado";
        case ProjectStatus::archived:
            return "Archivado";
    }
    throw std::invalid_argument("Estado de proyecto inválido.");
}

ProjectStatus project_status_from_name(std::string_view name) {
    if (name == "planned") {
        return ProjectStatus::planned;
    }
    if (name == "active") {
        return ProjectStatus::active;
    }
    if (name == "paused") {
        return ProjectStatus::paused;
    }
    if (name == "completed") {
        return ProjectStatus::completed;
    }
    if (name == "archived") {
        return ProjectStatus::archived;
    }
    throw std::runtime_error("SQLite contiene un estado de proyecto inválido: " + std::string(name));
}

}  // namespace modra
