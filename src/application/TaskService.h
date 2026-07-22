#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/Task.h"
#include "infrastructure/database/ProjectRepository.h"
#include "infrastructure/database/TaskRepository.h"

namespace modra {

class Database;

enum class TaskQuickView {
    all,
    today,
    overdue,
    upcoming,
    blocked,
    recently_completed,
    archived,
};

enum class TaskSort {
    recommended,
    due_date,
    priority,
    status,
    project,
    responsible,
    updated_at,
};

enum class TaskDateFilter {
    any,
    with_date,
    without_date,
};

struct TaskQuery {
    TaskQuickView view = TaskQuickView::all;
    std::optional<std::int64_t> project_id;
    std::optional<std::string> responsible;
    bool without_responsible = false;
    std::optional<TaskStatus> status;
    std::optional<TaskType> type;
    std::optional<TaskPriority> priority;
    TaskDateFilter date_filter = TaskDateFilter::any;
    std::string search;
    TaskSort sort = TaskSort::recommended;
};

class TaskService {
public:
    explicit TaskService(Database& database);

    Task create(TaskInput input);
    std::optional<Task> find_by_id(std::int64_t id) const;
    std::vector<Task> list_active(std::int64_t project_id) const;
    std::vector<Task> list_archived(std::int64_t project_id) const;
    std::vector<TaskSummary> list_global(const TaskQuery& query, const std::string& today) const;
    std::vector<std::string> list_distinct_responsibles() const;
    static std::string current_local_date();
    Task update(std::int64_t id, TaskInput input);
    Task archive(std::int64_t id);
    Task restore(std::int64_t id);

private:
    TaskInput normalize_and_validate(TaskInput input) const;
    void require_active_project(std::int64_t project_id) const;

    ProjectRepository projects_;
    TaskRepository tasks_;
};

}  // namespace modra
