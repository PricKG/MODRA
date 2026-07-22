#include "infrastructure/database/Database.h"

#include <stdexcept>
#include <string>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include "application/AppInfo.h"
#include "modra/EmbeddedMigrations.h"

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
    const auto migrations = embedded_migrations();
    if (migrations.empty() || migrations.front().version != 1) {
        throw std::runtime_error("Embedded migrations must start with version 1");
    }

    execute("BEGIN IMMEDIATE;");
    try {
        bool migration_table_exists =
            scalar_int("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations';") != 0;

        for (const auto& migration : migrations) {
            bool applied = false;
            if (migration_table_exists) {
                sqlite3_stmt* statement = nullptr;
                const char* applied_sql = "SELECT COUNT(*) FROM schema_migrations WHERE version = ?1;";
                if (sqlite3_prepare_v2(connection_, applied_sql, -1, &statement, nullptr) != SQLITE_OK) {
                    throw std::runtime_error("Could not prepare migration lookup: " +
                                             std::string(sqlite3_errmsg(connection_)));
                }
                sqlite3_bind_int(statement, 1, migration.version);
                const int step = sqlite3_step(statement);
                if (step != SQLITE_ROW) {
                    const std::string message = sqlite3_errmsg(connection_);
                    sqlite3_finalize(statement);
                    throw std::runtime_error("Could not read migration state: " + message);
                }
                applied = sqlite3_column_int(statement, 0) != 0;
                sqlite3_finalize(statement);
            }
            if (applied) continue;

            spdlog::info("Applying migration {}: {}", migration.version, migration.name);
            const std::string sql(migration.sql);
            execute(sql.c_str());
            migration_table_exists = true;

            if (migration.version == 1) {
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
                );
            }

            sqlite3_stmt* statement = nullptr;
            const char* register_sql =
                "INSERT INTO schema_migrations(version, name, applied_at) "
                "VALUES(?1, ?2, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));";
            if (sqlite3_prepare_v2(connection_, register_sql, -1, &statement, nullptr) != SQLITE_OK) {
                throw std::runtime_error("Could not prepare migration registration: " +
                                         std::string(sqlite3_errmsg(connection_)));
            }
            sqlite3_bind_int(statement, 1, migration.version);
            sqlite3_bind_text(statement, 2, migration.name.data(), static_cast<int>(migration.name.size()),
                              SQLITE_TRANSIENT);
            if (sqlite3_step(statement) != SQLITE_DONE) {
                const std::string message = sqlite3_errmsg(connection_);
                sqlite3_finalize(statement);
                throw std::runtime_error("Could not register migration: " + message);
            }
            sqlite3_finalize(statement);
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
