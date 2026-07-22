#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/Project.h"
#include "infrastructure/database/ProjectRepository.h"

namespace modra {

class Database;

class ProjectService {
public:
    explicit ProjectService(Database& database);

    Project create(ProjectInput input);
    std::optional<Project> find_by_id(std::int64_t id) const;
    std::optional<Project> find_by_alias(std::string alias) const;
    std::vector<Project> list_active() const;
    std::vector<Project> list_archived() const;
    Project update(std::int64_t id, ProjectInput input);
    Project archive(std::int64_t id);

private:
    ProjectInput normalize_and_validate(ProjectInput input) const;

    ProjectRepository repository_;
};

}  // namespace modra
