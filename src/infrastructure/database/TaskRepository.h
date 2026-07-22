#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/Task.h"

namespace modra {

class Database;

class TaskRepository {
public:
    explicit TaskRepository(Database& database);

    Task create(const TaskInput& input);
    std::optional<Task> find_by_id(std::int64_t id) const;
    std::vector<Task> list_active(std::int64_t project_id) const;
    std::vector<Task> list_archived(std::int64_t project_id) const;
    std::vector<TaskSummary> list_all_active() const;
    std::vector<TaskSummary> list_all_archived() const;
    std::vector<std::string> list_responsible_names() const;
    Task update(std::int64_t id, const TaskInput& input);
    Task archive(std::int64_t id);
    Task restore(std::int64_t id);

private:
    Database& database_;
};

}  // namespace modra
