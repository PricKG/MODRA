#include "application/TaskService.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "domain/Project.h"
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
        return std::chrono::year_month_day{
                   std::chrono::year{year_value}, std::chrono::month{month_value}, std::chrono::day{day_value}}
            .ok();
    } catch (const std::exception&) {
        return false;
    }
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string cleaned_responsible(const std::string& value) {
    std::string result;
    bool pending_space = false;
    for (unsigned char character : value) {
        if (std::isspace(character)) {
            pending_space = !result.empty();
        } else {
            if (pending_space) {
                result.push_back(' ');
            }
            result.push_back(static_cast<char>(character));
            pending_space = false;
        }
    }
    return result;
}

std::string normalized_responsible(const std::string& value) {
    return lowercase(cleaned_responsible(value));
}

std::chrono::sys_days parse_date(const std::string& value) {
    return std::chrono::sys_days{std::chrono::year{std::stoi(value.substr(0, 4))} /
                                 std::chrono::month{static_cast<unsigned>(std::stoul(value.substr(5, 2)))} /
                                 std::chrono::day{static_cast<unsigned>(std::stoul(value.substr(8, 2)))}};
}

std::string format_date(std::chrono::sys_days value) {
    const std::chrono::year_month_day date{value};
    std::ostringstream output;
    output << std::setfill('0') << std::setw(4) << static_cast<int>(date.year()) << '-' << std::setw(2)
           << static_cast<unsigned>(date.month()) << '-' << std::setw(2) << static_cast<unsigned>(date.day());
    return output.str();
}

bool completed_recently(const Task& task, const std::string& first_day, const std::string& today) {
    if (task.status != TaskStatus::completed || !task.completed_at || task.completed_at->size() < 10) {
        return false;
    }
    const std::string completion_date = task.completed_at->substr(0, 10);
    return completion_date >= first_day && completion_date <= today;
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

}  // namespace

TaskService::TaskService(Database& database) : projects_(database), tasks_(database) {}

void TaskService::require_active_project(std::int64_t project_id) const {
    const auto project = projects_.find_by_id(project_id);
    if (!project) {
        throw std::invalid_argument("El proyecto asociado no existe.");
    }
    if (project->status == ProjectStatus::archived) {
        throw std::invalid_argument("No se pueden modificar tareas de un proyecto archivado.");
    }
}

TaskInput TaskService::normalize_and_validate(TaskInput input) const {
    if (input.project_id <= 0) {
        throw std::invalid_argument("El proyecto es obligatorio.");
    }
    if (input.title.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("El título es obligatorio.");
    }
    if (input.description && input.description->empty()) {
        input.description.reset();
    }
    if (input.assignee_name) {
        const auto first = input.assignee_name->find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            input.assignee_name.reset();
        } else {
            const auto last = input.assignee_name->find_last_not_of(" \t\r\n");
            *input.assignee_name = input.assignee_name->substr(first, last - first + 1);
        }
    }
    if (input.due_date && input.due_date->empty()) {
        input.due_date.reset();
    }
    if (input.blocked_reason && input.blocked_reason->find_first_not_of(" \t\r\n") == std::string::npos) {
        input.blocked_reason.reset();
    }
    if (input.due_date && !valid_iso_date(*input.due_date)) {
        throw std::invalid_argument("La fecha de seguimiento debe tener formato YYYY-MM-DD y ser válida.");
    }
    if (input.status == TaskStatus::blocked && !input.blocked_reason) {
        throw std::invalid_argument("Una tarea bloqueada debe indicar el motivo del bloqueo.");
    }
    if (input.status != TaskStatus::blocked) {
        input.blocked_reason.reset();
    }
    return input;
}

Task TaskService::create(TaskInput input) {
    input.status = TaskStatus::pending;
    input = normalize_and_validate(std::move(input));
    require_active_project(input.project_id);
    const Task task = tasks_.create(input);
    spdlog::info("Task created: id={}, project_id={}", task.id, task.project_id);
    return task;
}

std::optional<Task> TaskService::find_by_id(std::int64_t id) const {
    return tasks_.find_by_id(id);
}

std::vector<Task> TaskService::list_active(std::int64_t project_id) const {
    return tasks_.list_active(project_id);
}

std::vector<Task> TaskService::list_archived(std::int64_t project_id) const {
    return tasks_.list_archived(project_id);
}

std::vector<TaskSummary> TaskService::list_global(const TaskQuery& query, const std::string& today) const {
    if (!valid_iso_date(today)) {
        throw std::invalid_argument("La fecha de referencia debe tener formato YYYY-MM-DD y ser válida.");
    }
    const std::string next_week = format_date(parse_date(today) + std::chrono::days{7});
    const std::string previous_week = format_date(parse_date(today) - std::chrono::days{6});
    const auto loaded = query.view == TaskQuickView::archived ? tasks_.list_all_archived() : tasks_.list_all_active();
    std::vector<TaskSummary> result;
    const std::string normalized_search = lowercase(query.search);
    const std::string responsible_filter = query.responsible ? normalized_responsible(*query.responsible) : "";

    for (const auto& summary : loaded) {
        const Task& task = summary.task;
        bool matches_view = false;
        switch (query.view) {
            case TaskQuickView::all: matches_view = true; break;
            case TaskQuickView::today:
                matches_view = task.due_date && *task.due_date == today && task.status != TaskStatus::completed;
                break;
            case TaskQuickView::overdue:
                matches_view = task.due_date && *task.due_date < today && task.status != TaskStatus::completed;
                break;
            case TaskQuickView::upcoming:
                matches_view = task.due_date && *task.due_date > today && *task.due_date <= next_week &&
                               task.status != TaskStatus::completed;
                break;
            case TaskQuickView::blocked: matches_view = task.status == TaskStatus::blocked; break;
            case TaskQuickView::recently_completed:
                matches_view = completed_recently(task, previous_week, today);
                break;
            case TaskQuickView::archived: matches_view = true; break;
        }
        if (!matches_view || (query.project_id && task.project_id != *query.project_id) ||
            (query.without_responsible && task.assignee_name) ||
            (query.responsible &&
             (!task.assignee_name || normalized_responsible(*task.assignee_name) != responsible_filter)) ||
            (query.status && task.status != *query.status) || (query.type && task.type != *query.type) ||
            (query.priority && task.priority != *query.priority) ||
            (query.date_filter == TaskDateFilter::with_date && !task.due_date) ||
            (query.date_filter == TaskDateFilter::without_date && task.due_date)) {
            continue;
        }
        if (!normalized_search.empty()) {
            const std::string haystack = lowercase(task.title + " " + task.description.value_or("") + " " +
                                                    task.assignee_name.value_or("") + " " + summary.project_name +
                                                    " " + summary.project_alias);
            if (haystack.find(normalized_search) == std::string::npos) {
                continue;
            }
        }
        result.push_back(summary);
    }

    const auto due_value = [](const Task& task) { return task.due_date.value_or("9999-12-31"); };
    std::stable_sort(result.begin(), result.end(), [&](const TaskSummary& left, const TaskSummary& right) {
        const Task& a = left.task;
        const Task& b = right.task;
        switch (query.sort) {
            case TaskSort::due_date:
                return std::pair{due_value(a), a.updated_at} < std::pair{due_value(b), b.updated_at};
            case TaskSort::priority:
                return std::pair{priority_rank(a.priority), due_value(a)} <
                       std::pair{priority_rank(b.priority), due_value(b)};
            case TaskSort::status:
                return std::pair{std::string(task_status_name(a.status)), due_value(a)} <
                       std::pair{std::string(task_status_name(b.status)), due_value(b)};
            case TaskSort::project:
                return std::pair{lowercase(left.project_name), due_value(a)} <
                       std::pair{lowercase(right.project_name), due_value(b)};
            case TaskSort::responsible:
                return std::pair{normalized_responsible(a.assignee_name.value_or("zzzz")), due_value(a)} <
                       std::pair{normalized_responsible(b.assignee_name.value_or("zzzz")), due_value(b)};
            case TaskSort::updated_at: return a.updated_at > b.updated_at;
            case TaskSort::recommended: {
                const bool a_overdue = a.due_date && *a.due_date < today && a.status != TaskStatus::completed;
                const bool b_overdue = b.due_date && *b.due_date < today && b.status != TaskStatus::completed;
                if (a_overdue != b_overdue) return a_overdue;
                if (priority_rank(a.priority) != priority_rank(b.priority))
                    return priority_rank(a.priority) < priority_rank(b.priority);
                if (due_value(a) != due_value(b)) return due_value(a) < due_value(b);
                return a.updated_at > b.updated_at;
            }
        }
        return false;
    });
    return result;
}

std::vector<std::string> TaskService::list_distinct_responsibles() const {
    std::map<std::string, std::string> unique;
    for (const auto& name : tasks_.list_responsible_names()) {
        const std::string cleaned = cleaned_responsible(name);
        const std::string key = normalized_responsible(cleaned);
        const auto existing = unique.find(key);
        if (existing == unique.end() || cleaned < existing->second) {
            unique[key] = cleaned;
        }
    }
    std::vector<std::string> names;
    for (const auto& [normalized, display] : unique) {
        static_cast<void>(normalized);
        names.push_back(display);
    }
    return names;
}

std::string TaskService::current_local_date() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream output;
    output << std::put_time(&local, "%Y-%m-%d");
    return output.str();
}

Task TaskService::update(std::int64_t id, TaskInput input) {
    const auto existing = tasks_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("No existe la tarea solicitada.");
    }
    if (existing->archived_at) {
        throw std::runtime_error("La tarea está archivada. Desarchivala antes de editarla.");
    }
    input = normalize_and_validate(std::move(input));
    require_active_project(input.project_id);
    const Task task = tasks_.update(id, input);
    spdlog::info("Task updated: id={}, project_id={}", task.id, task.project_id);
    return task;
}

Task TaskService::archive(std::int64_t id) {
    const auto existing = tasks_.find_by_id(id);
    if (!existing) {
        throw std::runtime_error("No existe la tarea solicitada.");
    }
    require_active_project(existing->project_id);
    const Task task = tasks_.archive(id);
    spdlog::info("Task archived: id={}, project_id={}", task.id, task.project_id);
    return task;
}

Task TaskService::restore(std::int64_t id) {
    const auto existing = tasks_.find_by_id(id);
    if (!existing) throw std::runtime_error("No existe la tarea solicitada.");
    if (!existing->archived_at) throw std::runtime_error("La tarea no está archivada.");
    require_active_project(existing->project_id);
    const Task task = tasks_.restore(id);
    spdlog::info("Task restored: id={}, project_id={}", task.id, task.project_id);
    return task;
}

}  // namespace modra
