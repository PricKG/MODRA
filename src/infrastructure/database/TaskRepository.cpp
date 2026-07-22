#include "infrastructure/database/TaskRepository.h"

#include <memory>
#include <stdexcept>
#include <string>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include "infrastructure/database/Database.h"

namespace modra {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* database, const std::string& sql) {
    sqlite3_stmt* raw_statement = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw_statement, nullptr) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Task persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return Statement(raw_statement, sqlite3_finalize);
}

void bind_text(sqlite3* database, sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Task persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
}

void bind_optional(sqlite3* database,
                   sqlite3_stmt* statement,
                   int index,
                   const std::optional<std::string>& value) {
    const int result = value ? sqlite3_bind_text(statement, index, value->c_str(), -1, SQLITE_TRANSIENT)
                             : sqlite3_bind_null(statement, index);
    if (result != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Task persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
}

std::optional<std::string> optional_text(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, column)));
}

Task read_task(sqlite3_stmt* statement) {
    Task task;
    task.id = sqlite3_column_int64(statement, 0);
    task.project_id = sqlite3_column_int64(statement, 1);
    task.title = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    task.description = optional_text(statement, 3);
    task.type = task_type_from_name(reinterpret_cast<const char*>(sqlite3_column_text(statement, 4)));
    task.status = task_status_from_name(reinterpret_cast<const char*>(sqlite3_column_text(statement, 5)));
    task.priority = task_priority_from_name(reinterpret_cast<const char*>(sqlite3_column_text(statement, 6)));
    task.assignee_name = optional_text(statement, 7);
    task.due_date = optional_text(statement, 8);
    task.completed_at = optional_text(statement, 9);
    task.blocked_reason = optional_text(statement, 10);
    task.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 11));
    task.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 12));
    task.archived_at = optional_text(statement, 13);
    return task;
}

TaskSummary read_task_summary(sqlite3_stmt* statement) {
    TaskSummary summary;
    summary.task = read_task(statement);
    summary.project_name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 14));
    summary.project_alias = reinterpret_cast<const char*>(sqlite3_column_text(statement, 15));
    summary.project_archived = sqlite3_column_int(statement, 16) != 0;
    return summary;
}

constexpr const char* task_columns =
    "id, project_id, title, description, type, status, priority, assignee_name, due_date, completed_at, blocked_reason, "
    "created_at, updated_at, archived_at ";

constexpr const char* task_summary_columns =
    "t.id, t.project_id, t.title, t.description, t.type, t.status, t.priority, t.assignee_name, t.due_date, "
    "t.completed_at, t.blocked_reason, t.created_at, t.updated_at, t.archived_at, p.name, p.alias, "
    "p.archived_at IS NOT NULL ";

void execute_write(sqlite3* database, sqlite3_stmt* statement) {
    const int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        return;
    }
    const int extended_result = sqlite3_extended_errcode(database);
    if (extended_result == SQLITE_CONSTRAINT_FOREIGNKEY) {
        throw std::runtime_error("El proyecto asociado no existe.");
    }
    const std::string message = sqlite3_errmsg(database);
    spdlog::error("Task persistence error: {}", message);
    throw std::runtime_error("Error de SQLite: " + message);
}

std::vector<Task> list_for_project(Database& database, std::int64_t project_id, bool archived) {
    auto statement = prepare(database.handle(), std::string("SELECT ") + task_columns +
                                                    "FROM tasks WHERE project_id = ?1 AND archived_at IS " +
                                                    (archived ? "NOT NULL " : "NULL ") +
                                                    "ORDER BY CASE priority WHEN 'critical' THEN 0 WHEN 'high' THEN 1 "
                                                    "WHEN 'normal' THEN 2 ELSE 3 END, due_date IS NULL, due_date, "
                                                    "updated_at DESC, id DESC;");
    sqlite3_bind_int64(statement.get(), 1, project_id);
    std::vector<Task> tasks;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        tasks.push_back(read_task(statement.get()));
    }
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database.handle());
        spdlog::error("Task persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return tasks;
}

std::vector<TaskSummary> list_global(Database& database, bool archived) {
    auto statement = prepare(
        database.handle(),
        std::string("SELECT ") + task_summary_columns + "FROM tasks t JOIN projects p ON p.id = t.project_id WHERE " +
            (archived ? "(t.archived_at IS NOT NULL OR p.archived_at IS NOT NULL) "
                      : "t.archived_at IS NULL AND p.archived_at IS NULL ") +
            "ORDER BY t.updated_at DESC, t.id DESC;"
    );
    std::vector<TaskSummary> tasks;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        tasks.push_back(read_task_summary(statement.get()));
    }
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database.handle());
        spdlog::error("Task global query error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return tasks;
}

}  // namespace

TaskRepository::TaskRepository(Database& database) : database_(database) {
    sqlite3_extended_result_codes(database_.handle(), 1);
}

Task TaskRepository::create(const TaskInput& input) {
    auto statement = prepare(
        database_.handle(),
        "INSERT INTO tasks(project_id, title, description, type, status, priority, assignee_name, due_date, "
        "completed_at, blocked_reason, created_at, updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, "
        "CASE WHEN ?5 = 'completed' THEN strftime('%Y-%m-%dT%H:%M:%fZ', 'now') ELSE NULL END, ?9, "
        "strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
    );
    sqlite3_bind_int64(statement.get(), 1, input.project_id);
    bind_text(database_.handle(), statement.get(), 2, input.title);
    bind_optional(database_.handle(), statement.get(), 3, input.description);
    bind_text(database_.handle(), statement.get(), 4, std::string(task_type_name(input.type)));
    bind_text(database_.handle(), statement.get(), 5, std::string(task_status_name(input.status)));
    bind_text(database_.handle(), statement.get(), 6, std::string(task_priority_name(input.priority)));
    bind_optional(database_.handle(), statement.get(), 7, input.assignee_name);
    bind_optional(database_.handle(), statement.get(), 8, input.due_date);
    bind_optional(database_.handle(), statement.get(), 9, input.blocked_reason);
    execute_write(database_.handle(), statement.get());

    const auto task = find_by_id(sqlite3_last_insert_rowid(database_.handle()));
    if (!task) {
        throw std::runtime_error("No se pudo recuperar la tarea creada.");
    }
    return *task;
}

std::optional<Task> TaskRepository::find_by_id(std::int64_t id) const {
    auto statement = prepare(database_.handle(), std::string("SELECT ") + task_columns +
                                                    "FROM tasks WHERE id = ?1;");
    sqlite3_bind_int64(statement.get(), 1, id);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        return read_task(statement.get());
    }
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    const std::string message = sqlite3_errmsg(database_.handle());
    spdlog::error("Task persistence error: {}", message);
    throw std::runtime_error("Error de SQLite: " + message);
}

std::vector<Task> TaskRepository::list_active(std::int64_t project_id) const {
    return list_for_project(database_, project_id, false);
}

std::vector<Task> TaskRepository::list_archived(std::int64_t project_id) const {
    return list_for_project(database_, project_id, true);
}

std::vector<TaskSummary> TaskRepository::list_all_active() const {
    return list_global(database_, false);
}

std::vector<TaskSummary> TaskRepository::list_all_archived() const {
    return list_global(database_, true);
}

std::vector<std::string> TaskRepository::list_responsible_names() const {
    auto statement = prepare(database_.handle(),
                             "SELECT DISTINCT trim(assignee_name) FROM tasks "
                             "WHERE assignee_name IS NOT NULL AND length(trim(assignee_name)) > 0 "
                             "ORDER BY lower(trim(assignee_name));");
    std::vector<std::string> names;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        names.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(statement.get(), 0)));
    }
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database_.handle());
        spdlog::error("Task responsible query error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return names;
}

Task TaskRepository::update(std::int64_t id, const TaskInput& input) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE tasks SET project_id = ?1, title = ?2, description = ?3, type = ?4, status = ?5, "
        "priority = ?6, assignee_name = ?7, due_date = ?8, completed_at = CASE WHEN ?5 = 'completed' "
        "THEN COALESCE(completed_at, strftime('%Y-%m-%dT%H:%M:%fZ', 'now')) ELSE NULL END, "
        "blocked_reason = ?9, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
        "WHERE id = ?10 AND archived_at IS NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, input.project_id);
    bind_text(database_.handle(), statement.get(), 2, input.title);
    bind_optional(database_.handle(), statement.get(), 3, input.description);
    bind_text(database_.handle(), statement.get(), 4, std::string(task_type_name(input.type)));
    bind_text(database_.handle(), statement.get(), 5, std::string(task_status_name(input.status)));
    bind_text(database_.handle(), statement.get(), 6, std::string(task_priority_name(input.priority)));
    bind_optional(database_.handle(), statement.get(), 7, input.assignee_name);
    bind_optional(database_.handle(), statement.get(), 8, input.due_date);
    bind_optional(database_.handle(), statement.get(), 9, input.blocked_reason);
    sqlite3_bind_int64(statement.get(), 10, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) {
        throw std::runtime_error("La tarea no existe o está archivada.");
    }

    const auto task = find_by_id(id);
    if (!task) {
        throw std::runtime_error("No se pudo recuperar la tarea editada.");
    }
    return *task;
}

Task TaskRepository::archive(std::int64_t id) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE tasks SET archived_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), "
        "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?1 AND archived_at IS NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) {
        throw std::runtime_error("La tarea no existe o ya está archivada.");
    }

    const auto task = find_by_id(id);
    if (!task) {
        throw std::runtime_error("No se pudo recuperar la tarea archivada.");
    }
    return *task;
}

Task TaskRepository::restore(std::int64_t id) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE tasks SET archived_at = NULL, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
        "WHERE id = ?1 AND archived_at IS NOT NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) {
        throw std::runtime_error("La tarea no existe o no está archivada.");
    }
    const auto task = find_by_id(id);
    if (!task) throw std::runtime_error("No se pudo recuperar la tarea desarchivada.");
    return *task;
}

}  // namespace modra
