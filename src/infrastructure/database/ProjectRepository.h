#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/Project.h"

namespace modra {

class Database;

class ProjectRepository {
public:
    explicit ProjectRepository(Database& database);

    Project create(const ProjectInput& input);
    std::optional<Project> find_by_id(std::int64_t id) const;
    std::optional<Project> find_by_alias(const std::string& alias) const;
    std::vector<Project> list_active() const;
    std::vector<Project> list_archived() const;
    Project update(std::int64_t id, const ProjectInput& input);
    Project archive(std::int64_t id);

private:
    Database& database_;
};

}  // namespace modra
