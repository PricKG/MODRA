#include "infrastructure/database/ProjectRepository.h"

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
        spdlog::error("Project persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return Statement(raw_statement, sqlite3_finalize);
}

void bind_text(sqlite3* database, sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Project persistence error: {}", message);
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
        spdlog::error("Project persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
}

std::optional<std::string> optional_text(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, column)));
}

Project read_project(sqlite3_stmt* statement) {
    Project project;
    project.id = sqlite3_column_int64(statement, 0);
    project.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    project.alias = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
    project.description = optional_text(statement, 3);
    project.status = project_status_from_name(
        reinterpret_cast<const char*>(sqlite3_column_text(statement, 4)));
    project.start_date = optional_text(statement, 5);
    project.target_date = optional_text(statement, 6);
    project.local_path = optional_text(statement, 7);
    project.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    project.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 9));
    project.archived_at = optional_text(statement, 10);
    return project;
}

constexpr const char* project_columns =
    "id, name, alias, description, status, start_date, target_date, local_path, "
    "created_at, updated_at, archived_at ";

void execute_project_write(sqlite3* database, sqlite3_stmt* statement, const std::string& alias) {
    const int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        return;
    }
    const int extended_result = sqlite3_extended_errcode(database);
    if (extended_result == SQLITE_CONSTRAINT_UNIQUE) {
        spdlog::warn("Project alias conflict: {}", alias);
        throw std::runtime_error("Ya existe un proyecto con el alias '" + alias + "'.");
    }
    const std::string message = sqlite3_errmsg(database);
    spdlog::error("Project persistence error: {}", message);
    throw std::runtime_error("Error de SQLite: " + message);
}

}  // namespace

ProjectRepository::ProjectRepository(Database& database) : database_(database) {
    sqlite3_extended_result_codes(database_.handle(), 1);
}

Project ProjectRepository::create(const ProjectInput& input) {
    auto statement = prepare(
        database_.handle(),
        "INSERT INTO projects(name, alias, description, status, start_date, target_date, local_path, created_at, "
        "updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), "
        "strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
    );
    bind_text(database_.handle(), statement.get(), 1, input.name);
    bind_text(database_.handle(), statement.get(), 2, input.alias);
    bind_optional(database_.handle(), statement.get(), 3, input.description);
    bind_text(database_.handle(), statement.get(), 4, std::string(project_status_name(input.status)));
    bind_optional(database_.handle(), statement.get(), 5, input.start_date);
    bind_optional(database_.handle(), statement.get(), 6, input.target_date);
    bind_optional(database_.handle(), statement.get(), 7, input.local_path);
    execute_project_write(database_.handle(), statement.get(), input.alias);

    const auto project = find_by_id(sqlite3_last_insert_rowid(database_.handle()));
    if (!project) {
        throw std::runtime_error("No se pudo recuperar el proyecto creado.");
    }
    return *project;
}

std::optional<Project> ProjectRepository::find_by_id(std::int64_t id) const {
    auto statement = prepare(database_.handle(), std::string("SELECT ") + project_columns +
                                                    "FROM projects WHERE id = ?1;");
    sqlite3_bind_int64(statement.get(), 1, id);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        return read_project(statement.get());
    }
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    const std::string message = sqlite3_errmsg(database_.handle());
    spdlog::error("Project persistence error: {}", message);
    throw std::runtime_error("Error de SQLite: " + message);
}

std::optional<Project> ProjectRepository::find_by_alias(const std::string& alias) const {
    auto statement = prepare(database_.handle(), std::string("SELECT ") + project_columns +
                                                    "FROM projects WHERE alias = ?1;");
    bind_text(database_.handle(), statement.get(), 1, alias);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) {
        return read_project(statement.get());
    }
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    const std::string message = sqlite3_errmsg(database_.handle());
    spdlog::error("Project persistence error: {}", message);
    throw std::runtime_error("Error de SQLite: " + message);
}

std::vector<Project> ProjectRepository::list_active() const {
    auto statement = prepare(database_.handle(), std::string("SELECT ") + project_columns +
                                                    "FROM projects WHERE archived_at IS NULL "
                                                    "ORDER BY updated_at DESC, id DESC;");
    std::vector<Project> projects;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        projects.push_back(read_project(statement.get()));
    }
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database_.handle());
        spdlog::error("Project persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return projects;
}

std::vector<Project> ProjectRepository::list_archived() const {
    auto statement = prepare(database_.handle(), std::string("SELECT ") + project_columns +
                                                    "FROM projects WHERE archived_at IS NOT NULL "
                                                    "ORDER BY archived_at DESC, id DESC;");
    std::vector<Project> projects;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
        projects.push_back(read_project(statement.get()));
    }
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database_.handle());
        spdlog::error("Project persistence error: {}", message);
        throw std::runtime_error("Error de SQLite: " + message);
    }
    return projects;
}

Project ProjectRepository::update(std::int64_t id, const ProjectInput& input) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE projects SET name = ?1, alias = ?2, description = ?3, status = ?4, start_date = ?5, "
        "target_date = ?6, local_path = ?7, updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
        "WHERE id = ?8;"
    );
    bind_text(database_.handle(), statement.get(), 1, input.name);
    bind_text(database_.handle(), statement.get(), 2, input.alias);
    bind_optional(database_.handle(), statement.get(), 3, input.description);
    bind_text(database_.handle(), statement.get(), 4, std::string(project_status_name(input.status)));
    bind_optional(database_.handle(), statement.get(), 5, input.start_date);
    bind_optional(database_.handle(), statement.get(), 6, input.target_date);
    bind_optional(database_.handle(), statement.get(), 7, input.local_path);
    sqlite3_bind_int64(statement.get(), 8, id);
    execute_project_write(database_.handle(), statement.get(), input.alias);
    if (sqlite3_changes(database_.handle()) == 0) {
        throw std::runtime_error("No existe el proyecto solicitado.");
    }

    const auto project = find_by_id(id);
    if (!project) {
        throw std::runtime_error("No se pudo recuperar el proyecto editado.");
    }
    return *project;
}

Project ProjectRepository::archive(std::int64_t id) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE projects SET status = 'archived', archived_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), "
        "updated_at = strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id = ?1 AND archived_at IS NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    execute_project_write(database_.handle(), statement.get(), "");
    if (sqlite3_changes(database_.handle()) == 0) {
        throw std::runtime_error("El proyecto no existe o ya está archivado.");
    }

    const auto project = find_by_id(id);
    if (!project) {
        throw std::runtime_error("No se pudo recuperar el proyecto archivado.");
    }
    return *project;
}

}  // namespace modra
