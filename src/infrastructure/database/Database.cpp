#include "infrastructure/database/Database.h"

#include <stdexcept>
#include <string>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include "application/AppInfo.h"

namespace modra {

Database::Database(const std::filesystem::path& path) {
    const std::string path_text = path.string();
    const int result = sqlite3_open_v2(
        path_text.c_str(), &connection_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = connection_ ? sqlite3_errmsg(connection_) : "unknown SQLite error";
        if (connection_) {
            sqlite3_close(connection_);
            connection_ = nullptr;
        }
        throw std::runtime_error("Could not open SQLite database: " + message);
    }
    execute("PRAGMA foreign_keys = ON;");
    sqlite3_busy_timeout(connection_, 5000);
    spdlog::info("SQLite database opened");
}

Database::~Database() {
    if (connection_) {
        sqlite3_close(connection_);
    }
}

void Database::execute(const char* sql) const {
    char* error = nullptr;
    const int result = sqlite3_exec(connection_, sql, nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(connection_);
        sqlite3_free(error);
        throw std::runtime_error("SQLite error: " + message);
    }
}

int Database::scalar_int(const char* sql) const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection_, sql, -1, &statement, nullptr) != SQLITE_OK) {
        throw std::runtime_error("Could not prepare SQLite query: " + std::string(sqlite3_errmsg(connection_)));
    }

    const int step = sqlite3_step(statement);
    if (step != SQLITE_ROW) {
        const std::string message = sqlite3_errmsg(connection_);
        sqlite3_finalize(statement);
        throw std::runtime_error("Could not read SQLite query result: " + message);
    }
    const int value = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return value;
}

void Database::apply_migrations() {
    execute("BEGIN IMMEDIATE;");
    try {
        execute(
            "CREATE TABLE IF NOT EXISTS schema_migrations ("
            "version INTEGER PRIMARY KEY,"
            "name TEXT NOT NULL,"
            "applied_at TEXT NOT NULL"
            ");");

        if (scalar_int("SELECT COUNT(*) FROM schema_migrations WHERE version = 1;") == 0) {
            spdlog::info("Applying migration 1: initial_schema");
            execute(
                "CREATE TABLE IF NOT EXISTS app_metadata ("
                "key TEXT PRIMARY KEY,"
                "value TEXT NOT NULL"
                ");");

            sqlite3_stmt* statement = nullptr;
            const char* metadata_sql =
                "INSERT OR REPLACE INTO app_metadata(key, value) VALUES('application_version', ?1);";
            if (sqlite3_prepare_v2(connection_, metadata_sql, -1, &statement, nullptr) != SQLITE_OK) {
                throw std::runtime_error("Could not prepare application metadata: " +
                                         std::string(sqlite3_errmsg(connection_)));
            }
            const std::string version(application_version());
            sqlite3_bind_text(statement, 1, version.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) != SQLITE_DONE) {
                const std::string message = sqlite3_errmsg(connection_);
                sqlite3_finalize(statement);
                throw std::runtime_error("Could not store application metadata: " + message);
            }
            sqlite3_finalize(statement);

            execute(
                "INSERT INTO app_metadata(key, value) "
                "VALUES('initialized_at', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(1, 'initial_schema', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
            );
        }

        if (scalar_int("SELECT COUNT(*) FROM schema_migrations WHERE version = 2;") == 0) {
            spdlog::info("Applying migration 2: projects");
            execute(
                "CREATE TABLE projects ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "name TEXT NOT NULL CHECK(length(trim(name)) > 0),"
                "alias TEXT NOT NULL UNIQUE CHECK(length(alias) > 0),"
                "description TEXT,"
                "status TEXT NOT NULL DEFAULT 'planned' "
                "CHECK(status IN ('planned', 'active', 'paused', 'completed', 'archived')),"
                "start_date TEXT,"
                "target_date TEXT,"
                "local_path TEXT,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "archived_at TEXT"
                ");"
                "CREATE INDEX idx_projects_status ON projects(status);"
                "CREATE INDEX idx_projects_archived_at ON projects(archived_at);"
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(2, 'projects', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
            );
        }

        if (scalar_int("SELECT COUNT(*) FROM schema_migrations WHERE version = 3;") == 0) {
            spdlog::info("Applying migration 3: tasks");
            execute(
                "CREATE TABLE tasks ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "project_id INTEGER NOT NULL REFERENCES projects(id) ON DELETE RESTRICT,"
                "title TEXT NOT NULL CHECK(length(trim(title)) > 0),"
                "description TEXT,"
                "type TEXT NOT NULL DEFAULT 'technical' "
                "CHECK(type IN ('technical', 'administrative', 'management', 'research', 'documentation', "
                "'follow_up')),"
                "status TEXT NOT NULL DEFAULT 'pending' "
                "CHECK(status IN ('pending', 'in_progress', 'blocked', 'in_review', 'completed', 'cancelled')),"
                "priority TEXT NOT NULL DEFAULT 'normal' "
                "CHECK(priority IN ('low', 'normal', 'high', 'critical')),"
                "due_date TEXT,"
                "completed_at TEXT,"
                "blocked_reason TEXT,"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "archived_at TEXT,"
                "CHECK(status <> 'blocked' OR (blocked_reason IS NOT NULL AND length(trim(blocked_reason)) > 0)),"
                "CHECK(status <> 'completed' OR completed_at IS NOT NULL)"
                ");"
                "CREATE INDEX idx_tasks_project_id ON tasks(project_id);"
                "CREATE INDEX idx_tasks_status ON tasks(status);"
                "CREATE INDEX idx_tasks_due_date ON tasks(due_date);"
                "CREATE INDEX idx_tasks_archived_at ON tasks(archived_at);"
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(3, 'tasks', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
            );
        }

        if (scalar_int("SELECT COUNT(*) FROM schema_migrations WHERE version = 4;") == 0) {
            spdlog::info("Applying migration 4: task_assignee_name");
            execute(
                "ALTER TABLE tasks ADD COLUMN assignee_name TEXT "
                "CHECK(assignee_name IS NULL OR length(trim(assignee_name)) > 0);"
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(4, 'task_assignee_name', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
            );
        }
        if (scalar_int("SELECT COUNT(*) FROM schema_migrations WHERE version = 5;") == 0) {
            spdlog::info("Applying migration 5: notes");
            execute(
                "CREATE TABLE notes ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "title TEXT NOT NULL CHECK(length(trim(title)) > 0),"
                "type TEXT NOT NULL DEFAULT 'general' "
                "CHECK(type IN ('general', 'technical', 'solution', 'meeting', 'sql', 'procedure', "
                "'configuration', 'reference')),"
                "content TEXT NOT NULL CHECK(length(trim(content)) > 0),"
                "project_id INTEGER REFERENCES projects(id) ON DELETE RESTRICT,"
                "task_id INTEGER REFERENCES tasks(id) ON DELETE RESTRICT,"
                "is_favorite INTEGER NOT NULL DEFAULT 0 CHECK(is_favorite IN (0, 1)),"
                "created_at TEXT NOT NULL,"
                "updated_at TEXT NOT NULL,"
                "archived_at TEXT"
                ");"
                "CREATE INDEX idx_notes_type ON notes(type);"
                "CREATE INDEX idx_notes_project_id ON notes(project_id);"
                "CREATE INDEX idx_notes_task_id ON notes(task_id);"
                "CREATE INDEX idx_notes_is_favorite ON notes(is_favorite);"
                "CREATE INDEX idx_notes_updated_at ON notes(updated_at);"
                "CREATE INDEX idx_notes_archived_at ON notes(archived_at);"
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(5, 'notes', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
            );
        }
        execute("COMMIT;");
    } catch (...) {
        try {
            execute("ROLLBACK;");
        } catch (...) {
        }
        throw;
    }
}

int Database::migration_count() const {
    return scalar_int("SELECT COUNT(*) FROM schema_migrations;");
}

std::string Database::sqlite_version() const {
    return sqlite3_libversion();
}

sqlite3* Database::handle() const {
    return connection_;
}

}  // namespace modra
