#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <sqlite3.h>

#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"

namespace {

class TemporaryGlobalTaskDirectory {
public:
    TemporaryGlobalTaskDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-global-task-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryGlobalTaskDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct GlobalTaskFixture {
    GlobalTaskFixture()
        : paths(modra::initialize_data_directory(temporary.path() / "data")),
          database(paths.database),
          projects(database),
          tasks(database) {
        database.apply_migrations();
        alpha = projects.create({"Proyecto Alpha", "alpha"});
        beta = projects.create({"Proyecto Beta", "beta"});
    }

    modra::Task create(const modra::Project& project,
                       std::string title,
                       std::optional<std::string> due_date = std::nullopt,
                       std::optional<std::string> responsible = std::nullopt,
                       modra::TaskStatus status = modra::TaskStatus::pending,
                       modra::TaskPriority priority = modra::TaskPriority::normal,
                       modra::TaskType type = modra::TaskType::technical) {
        modra::TaskInput input{project.id, std::move(title)};
        input.description = "Descripción con PDF";
        input.assignee_name = std::move(responsible);
        input.due_date = std::move(due_date);
        input.priority = priority;
        input.type = type;
        auto task = tasks.create(input);
        if (status != modra::TaskStatus::pending) {
            input.status = status;
            if (status == modra::TaskStatus::blocked) input.blocked_reason = "Dependencia externa";
            task = tasks.update(task.id, input);
        }
        return task;
    }

    void set_completed_at(std::int64_t id, const std::string& timestamp) {
        const std::string sql = "UPDATE tasks SET completed_at = '" + timestamp + "' WHERE id = " +
                                std::to_string(id) + ";";
        REQUIRE(sqlite3_exec(database.handle(), sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    }

    static modra::TaskQuery query(modra::TaskQuickView view = modra::TaskQuickView::all) {
        modra::TaskQuery result;
        result.view = view;
        return result;
    }

    TemporaryGlobalTaskDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
    modra::TaskService tasks;
    modra::Project alpha;
    modra::Project beta;
    const std::string today = "2026-07-22";
};

}  // namespace

TEST_CASE("Global active tasks include multiple projects and exclude archived tasks") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Alpha task");
    const auto archived = fixture.create(fixture.beta, "Archived task");
    fixture.tasks.archive(archived.id);

    const auto active = fixture.tasks.list_global(GlobalTaskFixture::query(), fixture.today);
    REQUIRE(active.size() == 1);
    CHECK(active.front().project_alias == "alpha");
}

TEST_CASE("The archived global view includes archived tasks") {
    GlobalTaskFixture fixture;
    const auto archived = fixture.create(fixture.beta, "Archived task");
    fixture.tasks.archive(archived.id);

    const auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::archived),
                                                 fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.id == archived.id);
}

TEST_CASE("Today view uses the controlled reference date") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Today", fixture.today);
    fixture.create(fixture.alpha, "Tomorrow", "2026-07-23");

    const auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::today), fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Today");
}

TEST_CASE("Overdue view excludes completed tasks") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Overdue", "2026-07-20");
    fixture.create(fixture.alpha, "Completed overdue", "2026-07-19", std::nullopt,
                   modra::TaskStatus::completed);

    const auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::overdue),
                                                 fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Overdue");
}

TEST_CASE("Upcoming view includes the next seven days and excludes today and overdue") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Overdue", "2026-07-21");
    fixture.create(fixture.alpha, "Today", fixture.today);
    fixture.create(fixture.alpha, "Upcoming", "2026-07-29");
    fixture.create(fixture.alpha, "Later", "2026-07-30");

    const auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::upcoming),
                                                 fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Upcoming");
}

TEST_CASE("Date-based global views exclude cancelled tasks") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Today", fixture.today);
    fixture.create(fixture.alpha, "Cancelled today", fixture.today, std::nullopt,
                   modra::TaskStatus::cancelled);
    fixture.create(fixture.alpha, "Overdue", "2026-07-20");
    fixture.create(fixture.alpha, "Cancelled overdue", "2026-07-20", std::nullopt,
                   modra::TaskStatus::cancelled);
    fixture.create(fixture.alpha, "Upcoming", "2026-07-29");
    fixture.create(fixture.alpha, "Cancelled upcoming", "2026-07-29", std::nullopt,
                   modra::TaskStatus::cancelled);

    auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::today), fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Today");

    tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::overdue), fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Overdue");

    tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::upcoming), fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Upcoming");
}

TEST_CASE("Blocked view only returns blocked tasks") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Blocked", std::nullopt, "Ana", modra::TaskStatus::blocked);
    fixture.create(fixture.alpha, "Pending");

    const auto tasks = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::blocked),
                                                 fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.status == modra::TaskStatus::blocked);
}

TEST_CASE("Recently completed view excludes old completions") {
    GlobalTaskFixture fixture;
    const auto recent = fixture.create(fixture.alpha, "Recent", std::nullopt, std::nullopt,
                                       modra::TaskStatus::completed);
    const auto old = fixture.create(fixture.alpha, "Old", std::nullopt, std::nullopt,
                                    modra::TaskStatus::completed);
    fixture.set_completed_at(recent.id, "2026-07-18T10:00:00Z");
    fixture.set_completed_at(old.id, "2026-07-01T10:00:00Z");

    const auto tasks = fixture.tasks.list_global(
        GlobalTaskFixture::query(modra::TaskQuickView::recently_completed), fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.id == recent.id);
}

TEST_CASE("Global tasks filter by project") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Alpha");
    fixture.create(fixture.beta, "Beta");
    auto query = GlobalTaskFixture::query();
    query.project_id = fixture.beta.id;

    const auto tasks = fixture.tasks.list_global(query, fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().project_alias == "beta");
}

TEST_CASE("Global tasks filter textual responsibles and tasks without responsible") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Assigned", std::nullopt, "Ana Pérez");
    fixture.create(fixture.alpha, "Unassigned");
    auto assigned_query = GlobalTaskFixture::query();
    assigned_query.responsible = " ana   pérez ";
    auto unassigned_query = GlobalTaskFixture::query();
    unassigned_query.without_responsible = true;

    CHECK(fixture.tasks.list_global(assigned_query, fixture.today).size() == 1);
    const auto unassigned = fixture.tasks.list_global(unassigned_query, fixture.today);
    REQUIRE(unassigned.size() == 1);
    CHECK(unassigned.front().task.title == "Unassigned");
}

TEST_CASE("Global tasks filter by status type priority and date presence") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Match", "2026-07-25", std::nullopt, modra::TaskStatus::in_progress,
                   modra::TaskPriority::critical, modra::TaskType::research);
    fixture.create(fixture.alpha, "Other");
    auto query = GlobalTaskFixture::query();
    query.status = modra::TaskStatus::in_progress;
    query.type = modra::TaskType::research;
    query.priority = modra::TaskPriority::critical;
    query.date_filter = modra::TaskDateFilter::with_date;

    const auto tasks = fixture.tasks.list_global(query, fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().task.title == "Match");
}

TEST_CASE("Search combines with filters and searches task and project fields") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Generate report", std::nullopt, "Ana", modra::TaskStatus::blocked);
    fixture.create(fixture.beta, "Other PDF", std::nullopt, "Bruno", modra::TaskStatus::blocked);
    auto query = GlobalTaskFixture::query(modra::TaskQuickView::blocked);
    query.project_id = fixture.alpha.id;
    query.search = "pdf";

    const auto combined = fixture.tasks.list_global(query, fixture.today);
    REQUIRE(combined.size() == 1);
    query.project_id.reset();
    query.search = "Proyecto Beta";
    CHECK(fixture.tasks.list_global(query, fixture.today).size() == 1);
    query.search = "beta";
    CHECK(fixture.tasks.list_global(query, fixture.today).size() == 1);
}

TEST_CASE("Tasks cannot be created in archived projects") {
    GlobalTaskFixture fixture;
    fixture.projects.archive(fixture.alpha.id);
    modra::TaskInput input{fixture.alpha.id, "Rejected"};

    CHECK_THROWS_WITH(fixture.tasks.create(input), "No se pueden modificar tareas de un proyecto archivado.");
}

TEST_CASE("A task can move between active projects") {
    GlobalTaskFixture fixture;
    const auto task = fixture.create(fixture.alpha, "Move this task", "2026-07-25", "Ana");

    modra::TaskInput input{fixture.beta.id, task.title};
    input.description = task.description;
    input.assignee_name = task.assignee_name;
    input.type = task.type;
    input.status = task.status;
    input.priority = task.priority;
    input.due_date = task.due_date;
    const auto updated = fixture.tasks.update(task.id, input);

    CHECK(updated.project_id == fixture.beta.id);
    modra::TaskQuery query;
    query.project_id = fixture.beta.id;
    const auto tasks = fixture.tasks.list_global(query, fixture.today);
    REQUIRE(tasks.size() == 1);
    CHECK(tasks.front().project_alias == "beta");
}

TEST_CASE("Tasks of archived projects leave active views and appear in archived view") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "Project archived task");
    fixture.projects.archive(fixture.alpha.id);

    CHECK(fixture.tasks.list_global(GlobalTaskFixture::query(), fixture.today).empty());
    const auto archived = fixture.tasks.list_global(GlobalTaskFixture::query(modra::TaskQuickView::archived),
                                                    fixture.today);
    REQUIRE(archived.size() == 1);
    CHECK(archived.front().project_archived);
}

TEST_CASE("Responsible names are distinct ignoring case and whitespace") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.alpha, "One", std::nullopt, "Ana Perez");
    fixture.create(fixture.beta, "Two", std::nullopt, "  ana   perez  ");

    const auto names = fixture.tasks.list_distinct_responsibles();
    REQUIRE(names.size() == 1);
    CHECK(names.front() == "Ana Perez");
}

TEST_CASE("Global task ordering supports recommended priority project and responsible modes") {
    GlobalTaskFixture fixture;
    fixture.create(fixture.beta, "Critical future", "2026-07-25", "Bruno", modra::TaskStatus::pending,
                   modra::TaskPriority::critical);
    fixture.create(fixture.alpha, "Cancelled overdue critical", "2026-07-19", "Carla",
                   modra::TaskStatus::cancelled, modra::TaskPriority::critical);
    fixture.create(fixture.alpha, "Overdue low", "2026-07-20", "Ana", modra::TaskStatus::pending,
                   modra::TaskPriority::low);

    auto query = GlobalTaskFixture::query();
    auto tasks = fixture.tasks.list_global(query, fixture.today);
    REQUIRE(tasks.size() == 3);
    CHECK(tasks.front().task.title == "Overdue low");

    query.sort = modra::TaskSort::project;
    tasks = fixture.tasks.list_global(query, fixture.today);
    CHECK(tasks.front().project_alias == "alpha");

    query.sort = modra::TaskSort::responsible;
    tasks = fixture.tasks.list_global(query, fixture.today);
    CHECK(tasks.front().task.assignee_name == "Ana");
}

TEST_CASE("Global task summaries persist after reopening SQLite") {
    TemporaryGlobalTaskDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        modra::TaskService tasks(database);
        const auto project = projects.create({"Persistent project", "persistent"});
        tasks.create({project.id, "Persistent global task"});
    }
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::TaskService tasks(database);
        modra::TaskQuery query;
        const auto summaries = tasks.list_global(query, "2026-07-22");
        REQUIRE(summaries.size() == 1);
        CHECK(summaries.front().project_alias == "persistent");
    }
}
