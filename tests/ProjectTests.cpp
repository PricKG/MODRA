#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "application/ProjectService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"

namespace {

class TemporaryProjectDirectory {
public:
    TemporaryProjectDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-project-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryProjectDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct ProjectFixture {
    ProjectFixture()
        : paths(modra::initialize_data_directory(temporary.path() / "data")),
          database(paths.database),
          projects(database) {
        database.apply_migrations();
    }

    modra::ProjectInput valid_input(std::string alias = "modra") const {
        return {
            "MODRA",
            std::move(alias),
            "Organización local de proyectos",
            modra::ProjectStatus::planned,
            "2026-07-01",
            "2026-12-31",
            "E:/Projects/MODRA",
        };
    }

    TemporaryProjectDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
};

}  // namespace

TEST_CASE("A valid project can be created") {
    ProjectFixture fixture;
    const auto project = fixture.projects.create(fixture.valid_input());

    CHECK(project.id > 0);
    CHECK(project.name == "MODRA");
    CHECK(project.alias == "modra");
    CHECK(project.description == "Organización local de proyectos");
    CHECK(project.created_at.empty() == false);
    CHECK(project.updated_at.empty() == false);
}

TEST_CASE("A project with an empty name is rejected") {
    ProjectFixture fixture;
    auto input = fixture.valid_input();
    input.name = "   ";

    CHECK_THROWS_WITH(fixture.projects.create(input), "El nombre es obligatorio.");
}

TEST_CASE("A project with an empty alias is rejected") {
    ProjectFixture fixture;
    auto input = fixture.valid_input();
    input.alias.clear();

    CHECK_THROWS_WITH(fixture.projects.create(input), "El alias es obligatorio.");
}

TEST_CASE("Project aliases are normalized to lowercase") {
    ProjectFixture fixture;
    const auto project = fixture.projects.create(fixture.valid_input("My_PROJECT-42"));

    CHECK(project.alias == "my_project-42");
    REQUIRE(fixture.projects.find_by_alias("MY_PROJECT-42"));
}

TEST_CASE("An invalid project alias is rejected") {
    ProjectFixture fixture;
    auto input = fixture.valid_input("alias with spaces");

    CHECK_THROWS_WITH(
        fixture.projects.create(input),
        "El alias solo puede contener letras minúsculas, números, guion y guion bajo."
    );
}

TEST_CASE("Duplicate project aliases are rejected") {
    ProjectFixture fixture;
    fixture.projects.create(fixture.valid_input("same-alias"));

    CHECK_THROWS_WITH(
        fixture.projects.create(fixture.valid_input("SAME-ALIAS")),
        "Ya existe un proyecto con el alias 'same-alias'."
    );
}

TEST_CASE("New projects always start as planned") {
    ProjectFixture fixture;
    auto input = fixture.valid_input();
    input.status = modra::ProjectStatus::completed;

    const auto project = fixture.projects.create(input);
    CHECK(project.status == modra::ProjectStatus::planned);
}

TEST_CASE("A target date before the start date is rejected") {
    ProjectFixture fixture;
    auto input = fixture.valid_input();
    input.start_date = "2026-08-10";
    input.target_date = "2026-08-09";

    CHECK_THROWS_WITH(
        fixture.projects.create(input), "La fecha objetivo no puede ser anterior a la fecha inicial."
    );
}

TEST_CASE("A project can be edited") {
    ProjectFixture fixture;
    const auto created = fixture.projects.create(fixture.valid_input());
    auto input = fixture.valid_input("modra-cli");
    input.name = "MODRA CLI";
    input.status = modra::ProjectStatus::active;
    input.local_path.reset();

    const auto updated = fixture.projects.update(created.id, input);
    CHECK(updated.name == "MODRA CLI");
    CHECK(updated.alias == "modra-cli");
    CHECK(updated.status == modra::ProjectStatus::active);
    CHECK_FALSE(updated.local_path);
}

TEST_CASE("A project can be archived") {
    ProjectFixture fixture;
    const auto created = fixture.projects.create(fixture.valid_input());

    const auto archived = fixture.projects.archive(created.id);
    CHECK(archived.status == modra::ProjectStatus::archived);
    CHECK(archived.archived_at.has_value());
    CHECK_FALSE(archived.archived_at->empty());
}

TEST_CASE("Archived projects are excluded from the active list") {
    ProjectFixture fixture;
    const auto created = fixture.projects.create(fixture.valid_input());
    fixture.projects.archive(created.id);

    CHECK(fixture.projects.list_active().empty());
}

TEST_CASE("Archived projects are included in their specific list") {
    ProjectFixture fixture;
    const auto created = fixture.projects.create(fixture.valid_input());
    fixture.projects.archive(created.id);

    const auto archived = fixture.projects.list_archived();
    REQUIRE(archived.size() == 1);
    CHECK(archived.front().id == created.id);
}

TEST_CASE("The projects migration is applied") {
    TemporaryProjectDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    modra::Database database(paths.database);

    database.apply_migrations();
    CHECK(database.migration_count() == 5);
}

TEST_CASE("The projects migration is not applied twice") {
    TemporaryProjectDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    modra::Database database(paths.database);

    database.apply_migrations();
    database.apply_migrations();
    CHECK(database.migration_count() == 5);
}

TEST_CASE("Project data persists after reopening the database") {
    TemporaryProjectDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");

    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        projects.create({"Persistent project", "persistent"});
    }

    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        const auto project = projects.find_by_alias("persistent");
        REQUIRE(project);
        CHECK(project->name == "Persistent project");
    }
}
