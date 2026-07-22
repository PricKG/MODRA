#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"

namespace {

class TemporaryTaskDirectory {
public:
    TemporaryTaskDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-task-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryTaskDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct TaskFixture {
    TaskFixture()
        : paths(modra::initialize_data_directory(temporary.path() / "data")),
          database(paths.database),
          projects(database),
          tasks(database) {
        database.apply_migrations();
        project = projects.create({"MODRA", "modra"});
    }

    modra::TaskInput valid_input(std::string title = "Implementar tareas") const {
        return {
            project.id,
            std::move(title),
            "Persistencia directa por proyecto",
            modra::TaskType::technical,
            modra::TaskStatus::pending,
            modra::TaskPriority::high,
            "Ana Pérez",
            "2026-08-31",
            std::nullopt,
        };
    }

    TemporaryTaskDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
    modra::TaskService tasks;
    modra::Project project;
};

}  // namespace

TEST_CASE("A valid task belongs directly to a project") {
    TaskFixture fixture;
    const auto task = fixture.tasks.create(fixture.valid_input());

    CHECK(task.id > 0);
    CHECK(task.project_id == fixture.project.id);
    CHECK(task.title == "Implementar tareas");
    CHECK(task.priority == modra::TaskPriority::high);
    CHECK(task.assignee_name == "Ana Pérez");
    CHECK_FALSE(task.created_at.empty());
}

TEST_CASE("A task requires a project") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.project_id = 0;

    CHECK_THROWS_WITH(fixture.tasks.create(input), "El proyecto es obligatorio.");
}

TEST_CASE("A task rejects a project that does not exist") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.project_id = 999999;

    CHECK_THROWS_WITH(fixture.tasks.create(input), "El proyecto asociado no existe.");
}

TEST_CASE("A task requires a title") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.title = "  ";

    CHECK_THROWS_WITH(fixture.tasks.create(input), "El título es obligatorio.");
}

TEST_CASE("A task rejects an invalid due date") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.due_date = "2026-02-30";

    CHECK_THROWS_WITH(
        fixture.tasks.create(input), "La fecha de seguimiento debe tener formato YYYY-MM-DD y ser válida."
    );
}

TEST_CASE("New tasks always start pending") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.status = modra::TaskStatus::completed;

    const auto task = fixture.tasks.create(input);
    CHECK(task.status == modra::TaskStatus::pending);
    CHECK_FALSE(task.completed_at);
}

TEST_CASE("A blocked task requires a reason") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    auto input = fixture.valid_input();
    input.status = modra::TaskStatus::blocked;

    CHECK_THROWS_WITH(
        fixture.tasks.update(created.id, input), "Una tarea bloqueada debe indicar el motivo del bloqueo."
    );
}

TEST_CASE("A task can be blocked with a reason") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    auto input = fixture.valid_input();
    input.status = modra::TaskStatus::blocked;
    input.blocked_reason = "Falta una definición funcional";

    const auto updated = fixture.tasks.update(created.id, input);
    CHECK(updated.status == modra::TaskStatus::blocked);
    CHECK(updated.blocked_reason == "Falta una definición funcional");
}

TEST_CASE("Completing a task records its completion time") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    auto input = fixture.valid_input();
    input.status = modra::TaskStatus::completed;

    const auto completed = fixture.tasks.update(created.id, input);
    CHECK(completed.status == modra::TaskStatus::completed);
    REQUIRE(completed.completed_at);
    CHECK_FALSE(completed.completed_at->empty());
}

TEST_CASE("A task can be edited") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    auto input = fixture.valid_input("Documentar tareas");
    input.type = modra::TaskType::documentation;
    input.priority = modra::TaskPriority::critical;
    input.assignee_name = "  Bruno Silva  ";
    input.due_date.reset();

    const auto updated = fixture.tasks.update(created.id, input);
    CHECK(updated.title == "Documentar tareas");
    CHECK(updated.type == modra::TaskType::documentation);
    CHECK(updated.priority == modra::TaskPriority::critical);
    CHECK(updated.assignee_name == "Bruno Silva");
    CHECK_FALSE(updated.due_date);
}

TEST_CASE("A task can be left without an assignee") {
    TaskFixture fixture;
    auto input = fixture.valid_input();
    input.assignee_name = "   ";

    const auto task = fixture.tasks.create(input);
    CHECK_FALSE(task.assignee_name);
}

TEST_CASE("Archived tasks leave the active list and enter the archived list") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    const auto archived = fixture.tasks.archive(created.id);

    REQUIRE(archived.archived_at);
    CHECK(fixture.tasks.list_active(fixture.project.id).empty());
    const auto archived_tasks = fixture.tasks.list_archived(fixture.project.id);
    REQUIRE(archived_tasks.size() == 1);
    CHECK(archived_tasks.front().id == created.id);
}

TEST_CASE("An archived task can be restored") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    fixture.tasks.archive(created.id);

    const auto restored = fixture.tasks.restore(created.id);

    CHECK_FALSE(restored.archived_at);
    REQUIRE(fixture.tasks.list_active(fixture.project.id).size() == 1);
    CHECK(fixture.tasks.list_active(fixture.project.id).front().id == created.id);
    CHECK(fixture.tasks.list_archived(fixture.project.id).empty());
    CHECK_THROWS_WITH(fixture.tasks.restore(created.id), "La tarea no está archivada.");
}

TEST_CASE("A task cannot be restored while its project is archived") {
    TaskFixture fixture;
    const auto created = fixture.tasks.create(fixture.valid_input());
    fixture.tasks.archive(created.id);
    fixture.projects.archive(fixture.project.id);

    CHECK_THROWS_WITH(
        fixture.tasks.restore(created.id), "No se pueden modificar tareas de un proyecto archivado."
    );
    REQUIRE(fixture.tasks.find_by_id(created.id));
    CHECK(fixture.tasks.find_by_id(created.id)->archived_at.has_value());
}

TEST_CASE("Tasks are isolated by their project") {
    TaskFixture fixture;
    fixture.tasks.create(fixture.valid_input());
    const auto second_project = fixture.projects.create({"Second", "second"});

    CHECK(fixture.tasks.list_active(second_project.id).empty());
    CHECK(fixture.tasks.list_active(fixture.project.id).size() == 1);
}

TEST_CASE("Archived projects keep their tasks in read-only mode") {
    TaskFixture fixture;
    fixture.projects.archive(fixture.project.id);

    CHECK_THROWS_WITH(
        fixture.tasks.create(fixture.valid_input()), "No se pueden modificar tareas de un proyecto archivado."
    );
}

TEST_CASE("The tasks migration is applied once") {
    TemporaryTaskDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    modra::Database database(paths.database);

    database.apply_migrations();
    database.apply_migrations();
    CHECK(database.migration_count() == 5);
}

TEST_CASE("Task data persists after reopening the database") {
    TemporaryTaskDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    std::int64_t project_id = 0;
    std::int64_t task_id = 0;

    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        modra::TaskService tasks(database);
        project_id = projects.create({"Persistent", "persistent"}).id;
        modra::TaskInput input{project_id, "Persistent task"};
        input.assignee_name = "María Rodríguez";
        task_id = tasks.create(input).id;
    }

    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::TaskService tasks(database);
        const auto task = tasks.find_by_id(task_id);
        REQUIRE(task);
        CHECK(task->project_id == project_id);
        CHECK(task->title == "Persistent task");
        CHECK(task->assignee_name == "María Rodríguez");
    }
}
