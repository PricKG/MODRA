#include "application/ProjectService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

#include "infrastructure/database/Database.h"

namespace modra {
namespace {

bool valid_iso_date(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        return false;
    }
    try {
        const int year_value = std::stoi(value.substr(0, 4));
        const unsigned month_value = static_cast<unsigned>(std::stoul(value.substr(5, 2)));
        const unsigned day_value = static_cast<unsigned>(std::stoul(value.substr(8, 2)));
        const std::chrono::year_month_day date{
            std::chrono::year{year_value}, std::chrono::month{month_value}, std::chrono::day{day_value}};
        return date.ok();
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace

ProjectService::ProjectService(Database& database) : repository_(database) {}

ProjectInput ProjectService::normalize_and_validate(ProjectInput input) const {
    if (input.name.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("El nombre es obligatorio.");
    }
    if (input.alias.empty()) {
        throw std::invalid_argument("El alias es obligatorio.");
    }

    std::transform(input.alias.begin(), input.alias.end(), input.alias.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (!std::all_of(input.alias.begin(), input.alias.end(), [](unsigned char character) {
            return std::islower(character) || std::isdigit(character) || character == '-' || character == '_';
        })) {
        throw std::invalid_argument("El alias solo puede contener letras minúsculas, números, guion y guion bajo.");
    }

    if (input.description && input.description->empty()) {
        input.description.reset();
    }
    if (input.start_date && input.start_date->empty()) {
        input.start_date.reset();
    }
    if (input.target_date && input.target_date->empty()) {
        input.target_date.reset();
    }
    if (input.local_path && input.local_path->empty()) {
        input.local_path.reset();
    }

    if (input.start_date && !valid_iso_date(*input.start_date)) {
        throw std::invalid_argument("La fecha inicial debe tener formato YYYY-MM-DD y ser válida.");
    }
    if (input.target_date && !valid_iso_date(*input.target_date)) {
        throw std::invalid_argument("La fecha objetivo debe tener formato YYYY-MM-DD y ser válida.");
    }
    if (input.start_date && input.target_date && *input.target_date < *input.start_date) {
        throw std::invalid_argument("La fecha objetivo no puede ser anterior a la fecha inicial.");
    }
    if (input.local_path) {
        if (std::any_of(input.local_path->begin(), input.local_path->end(), [](unsigned char character) {
                return character < 32;
            })) {
            throw std::invalid_argument("La ruta local contiene caracteres no permitidos.");
        }
        try {
            static_cast<void>(std::filesystem::path(*input.local_path));
        } catch (const std::exception&) {
            throw std::invalid_argument("La ruta local no tiene un formato válido.");
        }
    }
    return input;
}

Project ProjectService::create(ProjectInput input) {
    input.status = ProjectStatus::planned;
    input = normalize_and_validate(std::move(input));
    const Project project = repository_.create(input);
    spdlog::info("Project created: id={}, alias={}", project.id, project.alias);
    return project;
}

std::optional<Project> ProjectService::find_by_id(std::int64_t id) const {
    return repository_.find_by_id(id);
}

std::optional<Project> ProjectService::find_by_alias(std::string alias) const {
    std::transform(alias.begin(), alias.end(), alias.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return repository_.find_by_alias(alias);
}

std::vector<Project> ProjectService::list_active() const {
    return repository_.list_active();
}

std::vector<Project> ProjectService::list_archived() const {
    return repository_.list_archived();
}

Project ProjectService::update(std::int64_t id, ProjectInput input) {
    const auto existing = repository_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("No existe el proyecto solicitado.");
    }
    if (existing->status != ProjectStatus::archived && input.status == ProjectStatus::archived) {
        throw std::invalid_argument("Usá la acción de archivar para cambiar el proyecto a Archivado.");
    }
    if (existing->status == ProjectStatus::archived && input.status != ProjectStatus::archived) {
        throw std::invalid_argument("Esta versión todavía no permite restaurar proyectos archivados.");
    }

    input = normalize_and_validate(std::move(input));
    const Project project = repository_.update(id, input);
    spdlog::info("Project updated: id={}, alias={}", project.id, project.alias);
    return project;
}

Project ProjectService::archive(std::int64_t id) {
    const Project project = repository_.archive(id);
    spdlog::info("Project archived: id={}, alias={}", project.id, project.alias);
    return project;
}

}  // namespace modra
