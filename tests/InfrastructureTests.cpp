#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "application/AppInfo.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"

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

TEST_CASE("All registered migrations are applied") {
    TemporaryDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    modra::Database database(paths.database);
    database.apply_migrations();
    CHECK(database.migration_count() == 5);
}

TEST_CASE("Applied migrations are not executed twice") {
    TemporaryDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    modra::Database database(paths.database);
    database.apply_migrations();
    database.apply_migrations();
    CHECK(database.migration_count() == 5);
}

TEST_CASE("Application version is available from the shared core") {
    CHECK(modra::application_name() == "MODRA");
    CHECK(modra::application_version() == "0.1.0");
}
