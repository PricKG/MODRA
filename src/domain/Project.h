#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modra {

enum class ProjectStatus {
    planned,
    active,
    paused,
    completed,
    archived,
};

struct ProjectInput {
    std::string name;
    std::string alias;
    std::optional<std::string> description;
    ProjectStatus status = ProjectStatus::planned;
    std::optional<std::string> start_date;
    std::optional<std::string> target_date;
    std::optional<std::string> local_path;
};

struct Project {
    std::int64_t id = 0;
    std::string name;
    std::string alias;
    std::optional<std::string> description;
    ProjectStatus status = ProjectStatus::planned;
    std::optional<std::string> start_date;
    std::optional<std::string> target_date;
    std::optional<std::string> local_path;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> archived_at;
};

std::string_view project_status_name(ProjectStatus status);
std::string_view project_status_label(ProjectStatus status);
ProjectStatus project_status_from_name(std::string_view name);

}  // namespace modra
