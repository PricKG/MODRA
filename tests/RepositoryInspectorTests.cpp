#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "infrastructure/system/Environment.h"
#include "infrastructure/system/Process.h"
#include "infrastructure/system/RepositoryInspector.h"

namespace {

class RepositoryTemporaryDirectory {
public:
    RepositoryTemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("modra-repository-inspector-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path);
    }

    ~RepositoryTemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;
};

}  // namespace

TEST_CASE("Repository inspector distinguishes missing paths and ordinary folders") {
    RepositoryTemporaryDirectory temporary;

    const auto missing =
        modra::inspect_repository(temporary.path / "missing", false, false);
    CHECK_FALSE(missing.path_exists);
    CHECK(missing.kind == modra::RepositoryKind::none);
    CHECK(missing.needs_attention());

    const auto ordinary = modra::inspect_repository(temporary.path, false, false);
    CHECK(ordinary.path_exists);
    CHECK(ordinary.kind == modra::RepositoryKind::none);
    CHECK_FALSE(ordinary.has_changes());
}

TEST_CASE("Repository inspector reports Git branch and working tree changes") {
    if (!modra::command_is_available("git")) SKIP("Git no está disponible en este entorno.");

    RepositoryTemporaryDirectory temporary;
    const auto initialized = modra::run_process_capture({"git", "init", temporary.path.string()});
    REQUIRE(initialized.exit_code == 0);
    const auto nested_path = temporary.path / "src" / "feature";
    std::filesystem::create_directories(nested_path);

    {
        std::ofstream file(temporary.path / "nuevo.txt");
        file << "contenido";
    }
    auto status = modra::inspect_repository(temporary.path, true, false);
    REQUIRE(status.kind == modra::RepositoryKind::git);
    CHECK(status.error.empty());
    CHECK(status.untracked == 1);
    CHECK(status.modified == 0);
    CHECK(status.staged == 0);
    CHECK(status.has_changes());
    CHECK_FALSE(status.branch.empty());

    const auto nested_status = modra::inspect_repository(nested_path, true, false);
    CHECK(nested_status.kind == modra::RepositoryKind::git);
    CHECK(nested_status.untracked == 1);

    const auto staged =
        modra::run_process_capture({"git", "-C", temporary.path.string(), "add", "nuevo.txt"});
    REQUIRE(staged.exit_code == 0);
    status = modra::inspect_repository(temporary.path, true, false);
    CHECK(status.untracked == 0);
    CHECK(status.staged == 1);
}

TEST_CASE("Tool inspection includes the installed Git version") {
    const auto git = modra::inspect_tool("git");
    CHECK(git.available == modra::command_is_available("git"));
    if (git.available) CHECK(git.version.find("git version") != std::string::npos);
}
