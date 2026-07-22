#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "domain/Task.h"
#include "infrastructure/database/ProjectRepository.h"
#include "infrastructure/database/TaskRepository.h"

namespace modra {

class Database;

enum class AttentionReason {
    overdue,
    blocked,
    critical,
};

struct AttentionTask {
    TaskSummary summary;
    AttentionReason reason = AttentionReason::overdue;
};

struct DashboardData {
    std::size_t active_project_count = 0;
    std::size_t active_task_count = 0;
    std::size_t due_today_count = 0;
    std::size_t overdue_count = 0;
    std::size_t blocked_count = 0;
    std::size_t critical_count = 0;
    std::array<std::size_t, 6> task_count_by_status{};
    std::array<std::size_t, 4> task_count_by_priority{};
    std::vector<TaskSummary> upcoming_tasks;
    std::vector<AttentionTask> attention_tasks;
};

class DashboardService {
public:
    explicit DashboardService(Database& database);

    DashboardData load(const std::string& today) const;

private:
    ProjectRepository projects_;
    TaskRepository tasks_;
};

}  // namespace modra
