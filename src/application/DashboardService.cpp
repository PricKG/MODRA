#include "application/DashboardService.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <tuple>

#include "infrastructure/database/Database.h"

namespace modra {
namespace {

std::chrono::sys_days parse_date(const std::string& value) {
    bool valid_syntax = value.size() == 10;
    for (std::size_t index = 0; valid_syntax && index < value.size(); ++index) {
        const bool separator = index == 4 || index == 7;
        valid_syntax = separator ? value[index] == '-' : value[index] >= '0' && value[index] <= '9';
    }
    if (!valid_syntax) {
        throw std::invalid_argument("La fecha de referencia debe tener formato YYYY-MM-DD y ser válida.");
    }
    const int year = std::stoi(value.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(value.substr(5, 2)));
    const unsigned day = static_cast<unsigned>(std::stoi(value.substr(8, 2)));
    const std::chrono::year_month_day parsed{
        std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!parsed.ok()) {
        throw std::invalid_argument("La fecha de referencia debe tener formato YYYY-MM-DD y ser válida.");
    }
    return std::chrono::sys_days{parsed};
}

int priority_rank(TaskPriority priority) {
    switch (priority) {
        case TaskPriority::critical: return 0;
        case TaskPriority::high: return 1;
        case TaskPriority::normal: return 2;
        case TaskPriority::low: return 3;
    }
    return 4;
}

bool actionable(TaskStatus status) {
    return status != TaskStatus::completed && status != TaskStatus::cancelled;
}

}  // namespace

DashboardService::DashboardService(Database& database) : projects_(database), tasks_(database) {}

DashboardData DashboardService::load(const std::string& today) const {
    static_cast<void>(parse_date(today));
    DashboardData data;
    data.active_project_count = projects_.list_active().size();

    std::vector<TaskSummary> actionable_tasks;
    for (const auto& summary : tasks_.list_all_active()) {
        const Task& task = summary.task;
        if (!actionable(task.status)) continue;

        actionable_tasks.push_back(summary);
        ++data.active_task_count;
        ++data.task_count_by_status[static_cast<std::size_t>(task.status)];
        ++data.task_count_by_priority[static_cast<std::size_t>(task.priority)];
        if (task.due_date && *task.due_date == today) ++data.due_today_count;
        if (task.due_date && *task.due_date < today) ++data.overdue_count;
        if (task.status == TaskStatus::blocked) ++data.blocked_count;
        if (task.priority == TaskPriority::critical) ++data.critical_count;
    }

    for (const auto& summary : actionable_tasks) {
        if (summary.task.due_date && *summary.task.due_date > today) {
            data.upcoming_tasks.push_back(summary);
        }

        std::optional<AttentionReason> reason;
        if (summary.task.due_date && *summary.task.due_date < today) {
            reason = AttentionReason::overdue;
        } else if (summary.task.status == TaskStatus::blocked) {
            reason = AttentionReason::blocked;
        } else if (summary.task.priority == TaskPriority::critical) {
            reason = AttentionReason::critical;
        }
        if (reason) data.attention_tasks.push_back({summary, *reason});
    }

    std::stable_sort(data.upcoming_tasks.begin(), data.upcoming_tasks.end(), [](const auto& left, const auto& right) {
        return std::tuple{left.task.due_date.value(), priority_rank(left.task.priority), left.task.id} <
               std::tuple{right.task.due_date.value(), priority_rank(right.task.priority), right.task.id};
    });
    if (data.upcoming_tasks.size() > 5) data.upcoming_tasks.resize(5);

    std::stable_sort(data.attention_tasks.begin(), data.attention_tasks.end(), [](const auto& left, const auto& right) {
        const auto due = [](const Task& task) { return task.due_date.value_or("9999-12-31"); };
        return std::tuple{static_cast<int>(left.reason), due(left.summary.task),
                          priority_rank(left.summary.task.priority), left.summary.task.id} <
               std::tuple{static_cast<int>(right.reason), due(right.summary.task),
                          priority_rank(right.summary.task.priority), right.summary.task.id};
    });
    if (data.attention_tasks.size() > 5) data.attention_tasks.resize(5);
    return data;
}

}  // namespace modra
