#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "application/AppInfo.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"
#include "modra/EmbeddedMigrations.h"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Data directory resolution follows each operating system convention") {
    CHECK(modra::resolve_data_directory({modra::OperatingSystem::windows, "C:/Local", {}, {}}) ==
          std::filesystem::path("C:/Local") / "MODRA");
    CHECK(modra::resolve_data_directory({modra::OperatingSystem::linux, {}, "/xdg", "/home/user"}) ==
          std::filesystem::path("/xdg") / "modra");
    CHECK(modra::resolve_data_directory({modra::OperatingSystem::linux, {}, {}, "/home/user"}) ==
          std::filesystem::path("/home/user") / ".local" / "share" / "modra");
    CHECK(modra::resolve_data_directory({modra::OperatingSystem::macos, {}, {}, "/Users/user"}) ==
          std::filesystem::path("/Users/user") / "Library" / "Application Support" / "MODRA");
}

TEST_CASE("Data directory initialization creates the required structure") {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "data";
    const auto paths = modra::initialize_data_directory(root);

    CHECK(std::filesystem::is_directory(paths.backups));
    CHECK(std::filesystem::is_directory(paths.exports));
    CHECK(std::filesystem::is_directory(paths.logs));
    REQUIRE(std::filesystem::is_regular_file(paths.config));

    std::ifstream config(paths.config);
    const auto json = nlohmann::json::parse(config);
    CHECK(json.at("version") == 1);
    CHECK(json.at("editor").at("command") == "");
}

TEST_CASE("Data directory initialization preserves existing configuration") {
    TemporaryDirectory temporary;
    const auto root = temporary.path() / "data";
    std::filesystem::create_directories(root);
    {
        std::ofstream output(root / "config.json");
        output << nlohmann::json{{"version", 1}, {"custom", "preserved"}};
    }
    const auto paths = modra::initialize_data_directory(root);
    std::ifstream input(paths.config);
    const auto config = nlohmann::json::parse(input);
    CHECK(config.at("custom") == "preserved");
    CHECK(config.at("editor").at("command") == "");
}

TEST_CASE("SQLite initializes a temporary database") {
    TemporaryDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    modra::Database database(paths.database);
    CHECK(std::filesystem::is_regular_file(paths.database));
    CHECK_FALSE(database.sqlite_version().empty());
}

TEST_CASE("The five SQL migrations are embedded in deterministic order") {
    const auto migrations = modra::embedded_migrations();
    REQUIRE(migrations.size() == 5);

    const std::string_view expected_names[]{"initial_schema", "projects", "tasks", "task_assignee_name", "notes"};
    for (std::size_t index = 0; index < migrations.size(); ++index) {
        CHECK(migrations[index].version == static_cast<int>(index + 1));
        CHECK(migrations[index].name == expected_names[index]);
        CHECK_FALSE(migrations[index].sql.empty());
    }
}

TEST_CASE("All registered migrations are applied") {
    TemporaryDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    modra::Database database(paths.database);
    database.apply_migrations();
    CHECK(database.migration_count() == 5);

    sqlite3_stmt* foreign_keys = nullptr;
    REQUIRE(sqlite3_prepare_v2(database.handle(), "PRAGMA foreign_keys;", -1, &foreign_keys, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(foreign_keys) == SQLITE_ROW);
    CHECK(sqlite3_column_int(foreign_keys, 0) == 1);
    sqlite3_finalize(foreign_keys);

    CHECK(sqlite3_table_column_metadata(database.handle(), nullptr, "projects", "alias", nullptr, nullptr,
                                        nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_table_column_metadata(database.handle(), nullptr, "tasks", "assignee_name", nullptr, nullptr,
                                        nullptr, nullptr, nullptr) == SQLITE_OK);
    CHECK(sqlite3_table_column_metadata(database.handle(), nullptr, "notes", "is_favorite", nullptr, nullptr,
                                        nullptr, nullptr, nullptr) == SQLITE_OK);

    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database.handle(), "SELECT name FROM schema_migrations ORDER BY version;", -1,
                               &statement, nullptr) == SQLITE_OK);
    for (const auto& migration : modra::embedded_migrations()) {
        REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
        CHECK(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == migration.name);
    }
    CHECK(sqlite3_step(statement) == SQLITE_DONE);
    sqlite3_finalize(statement);

    REQUIRE(sqlite3_prepare_v2(database.handle(),
                               "SELECT value FROM app_metadata WHERE key = 'application_version';", -1,
                               &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(std::string_view(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) ==
          modra::application_version());
    sqlite3_finalize(statement);

    REQUIRE(sqlite3_prepare_v2(database.handle(),
                               "SELECT COUNT(*) FROM app_metadata WHERE key = 'initialized_at';", -1,
                               &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
}

TEST_CASE("Applied migrations are not executed twice") {
    TemporaryDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    modra::Database database(paths.database);
    database.apply_migrations();
    database.apply_migrations();
    CHECK(database.migration_count() == 5);

    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(database.handle(),
                               "SELECT COUNT(*) FROM app_metadata WHERE key = 'initialized_at';", -1,
                               &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
}

TEST_CASE("A copied migrated database keeps its data when reopened") {
    TemporaryDirectory temporary;
    const auto original = temporary.path() / "original.db";
    const auto copied = temporary.path() / "copied.db";
    {
        modra::Database database(original);
        database.apply_migrations();
        REQUIRE(sqlite3_exec(database.handle(),
            "INSERT INTO projects(name, alias, status, created_at, updated_at) "
            "VALUES('Persisted project', 'persisted-project', 'planned', "
            "'2026-07-22T00:00:00.000Z', '2026-07-22T00:00:00.000Z');",
            nullptr, nullptr, nullptr) == SQLITE_OK);
    }
    std::filesystem::copy_file(original, copied);

    modra::Database reopened(copied);
    reopened.apply_migrations();
    CHECK(reopened.migration_count() == 5);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(reopened.handle(),
                               "SELECT COUNT(*) FROM projects WHERE alias = 'persisted-project';", -1,
                               &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
}

TEST_CASE("A failing migration rolls back the complete migration transaction") {
    TemporaryDirectory temporary;
    modra::Database database(temporary.path() / "incompatible.db");
    const std::string conflicting_schema(modra::embedded_migrations()[1].sql);
    REQUIRE(sqlite3_exec(database.handle(), conflicting_schema.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);

    REQUIRE_THROWS(database.apply_migrations());

    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(
                database.handle(),
                "SELECT COUNT(*) FROM sqlite_master "
                "WHERE type = 'table' AND name IN ('schema_migrations', 'app_metadata');",
                -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 0);
    sqlite3_finalize(statement);

    REQUIRE(sqlite3_prepare_v2(database.handle(),
                               "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'projects';",
                               -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    CHECK(sqlite3_column_int(statement, 0) == 1);
    sqlite3_finalize(statement);
}

TEST_CASE("Application version is available from the shared core") {
    CHECK(modra::application_name() == "MODRA");
    CHECK(modra::application_version() == "0.2.0");
}
