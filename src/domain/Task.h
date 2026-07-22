#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modra {

enum class TaskType {
    technical,
    administrative,
    management,
    research,
    documentation,
    follow_up,
};

enum class TaskStatus {
    pending,
    in_progress,
    blocked,
    in_review,
    completed,
    cancelled,
};

enum class TaskPriority {
    low,
    normal,
    high,
    critical,
};

struct TaskInput {
    std::int64_t project_id = 0;
    std::string title;
    std::optional<std::string> description;
    TaskType type = TaskType::technical;
    TaskStatus status = TaskStatus::pending;
    TaskPriority priority = TaskPriority::normal;
    std::optional<std::string> assignee_name;
    std::optional<std::string> due_date;
    std::optional<std::string> blocked_reason;
};

struct Task {
    std::int64_t id = 0;
    std::int64_t project_id = 0;
    std::string title;
    std::optional<std::string> description;
    TaskType type = TaskType::technical;
    TaskStatus status = TaskStatus::pending;
    TaskPriority priority = TaskPriority::normal;
    std::optional<std::string> assignee_name;
    std::optional<std::string> due_date;
    std::optional<std::string> completed_at;
    std::optional<std::string> blocked_reason;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> archived_at;
};

struct TaskSummary {
    Task task;
    std::string project_name;
    std::string project_alias;
    bool project_archived = false;
};

std::string_view task_type_name(TaskType type);
std::string_view task_type_label(TaskType type);
TaskType task_type_from_name(std::string_view name);
std::string_view task_status_name(TaskStatus status);
std::string_view task_status_label(TaskStatus status);
TaskStatus task_status_from_name(std::string_view name);
std::string_view task_priority_name(TaskPriority priority);
std::string_view task_priority_label(TaskPriority priority);
TaskPriority task_priority_from_name(std::string_view name);

}  // namespace modra
