#include <chrono>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "application/DashboardService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"

namespace {

class TemporaryDashboardDirectory {
public:
    TemporaryDashboardDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-dashboard-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDashboardDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

struct DashboardFixture {
    DashboardFixture()
        : paths(modra::initialize_data_directory(temporary.path() / "data")),
          database(paths.database),
          projects(database),
          tasks(database),
          dashboard(database) {
        database.apply_migrations();
        alpha = projects.create({"Proyecto Alpha", "alpha"});
        beta = projects.create({"Proyecto Beta", "beta"});
    }

    modra::Task create(const modra::Project& project,
                       std::string title,
                       std::optional<std::string> due_date = std::nullopt,
                       std::optional<std::string> responsible = std::nullopt,
                       modra::TaskStatus status = modra::TaskStatus::pending,
                       modra::TaskPriority priority = modra::TaskPriority::normal) {
        modra::TaskInput input{project.id, std::move(title)};
        input.due_date = std::move(due_date);
        input.assignee_name = std::move(responsible);
        input.priority = priority;
        auto task = tasks.create(input);
        if (status != modra::TaskStatus::pending) {
            input.status = status;
            if (status == modra::TaskStatus::blocked) input.blocked_reason = "Dependencia externa";
            task = tasks.update(task.id, input);
        }
        return task;
    }

    TemporaryDashboardDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
    modra::TaskService tasks;
    modra::DashboardService dashboard;
    modra::Project alpha;
    modra::Project beta;
    const std::string today = "2026-07-22";
};

}  // namespace

TEST_CASE("Dashboard calculates actionable totals and distributions") {
    DashboardFixture fixture;
    fixture.create(fixture.alpha, "Today", fixture.today);
    fixture.create(fixture.alpha, "Overdue critical", "2026-07-20", std::nullopt,
                   modra::TaskStatus::pending, modra::TaskPriority::critical);
    fixture.create(fixture.beta, "In progress", std::nullopt, "Ana", modra::TaskStatus::in_progress,
                   modra::TaskPriority::high);
    fixture.create(fixture.beta, "Blocked", std::nullopt, "Bruno", modra::TaskStatus::blocked,
                   modra::TaskPriority::low);
    fixture.create(fixture.beta, "Review", std::nullopt, "Ana", modra::TaskStatus::in_review);
    fixture.create(fixture.beta, "Completed old", "2026-07-20", std::nullopt, modra::TaskStatus::completed,
                   modra::TaskPriority::critical);

    const auto data = fixture.dashboard.load(fixture.today);
    CHECK(data.active_project_count == 2);
    CHECK(data.active_task_count == 5);
    CHECK(data.due_today_count == 1);
    CHECK(data.overdue_count == 1);
    CHECK(data.blocked_count == 1);
    CHECK(data.critical_count == 1);
    CHECK(data.task_count_by_status[static_cast<std::size_t>(modra::TaskStatus::pending)] == 2);
    CHECK(data.task_count_by_status[static_cast<std::size_t>(modra::TaskStatus::in_progress)] == 1);
    CHECK(data.task_count_by_status[static_cast<std::size_t>(modra::TaskStatus::blocked)] == 1);
    CHECK(data.task_count_by_status[static_cast<std::size_t>(modra::TaskStatus::in_review)] == 1);
    CHECK(data.task_count_by_priority[static_cast<std::size_t>(modra::TaskPriority::critical)] == 1);
    CHECK(data.task_count_by_priority[static_cast<std::size_t>(modra::TaskPriority::high)] == 1);
    CHECK(data.task_count_by_priority[static_cast<std::size_t>(modra::TaskPriority::normal)] == 2);
    CHECK(data.task_count_by_priority[static_cast<std::size_t>(modra::TaskPriority::low)] == 1);
}

TEST_CASE("Dashboard returns the next five due tasks and excludes overdue and archived projects") {
    DashboardFixture fixture;
    fixture.create(fixture.alpha, "Overdue", "2026-07-21");
    fixture.create(fixture.alpha, "Today", fixture.today);
    for (int day = 23; day <= 29; ++day) {
        fixture.create(fixture.alpha, "Future " + std::to_string(day), "2026-07-" + std::to_string(day),
                       "Ana", modra::TaskStatus::pending,
                       day == 23 ? modra::TaskPriority::critical : modra::TaskPriority::normal);
    }
    fixture.create(fixture.beta, "Archived project future", "2026-07-23");
    fixture.projects.archive(fixture.beta.id);

    const auto data = fixture.dashboard.load(fixture.today);
    REQUIRE(data.upcoming_tasks.size() == 5);
    CHECK(data.upcoming_tasks.front().task.title == "Future 23");
    CHECK(data.upcoming_tasks.back().task.title == "Future 27");
    for (const auto& item : data.upcoming_tasks) CHECK(item.project_alias == "alpha");
}

TEST_CASE("Dashboard attention assigns one highest-priority reason per task") {
    DashboardFixture fixture;
    fixture.create(fixture.alpha, "Everything urgent", "2026-07-20", std::nullopt,
                   modra::TaskStatus::blocked, modra::TaskPriority::critical);
    fixture.create(fixture.alpha, "Blocked", std::nullopt, "Ana", modra::TaskStatus::blocked);
    fixture.create(fixture.alpha, "Critical", std::nullopt, "Ana", modra::TaskStatus::pending,
                   modra::TaskPriority::critical);
    fixture.create(fixture.alpha, "Unassigned is informational", std::nullopt, std::nullopt,
                   modra::TaskStatus::pending, modra::TaskPriority::high);

    const auto data = fixture.dashboard.load(fixture.today);
    REQUIRE(data.attention_tasks.size() == 3);
    CHECK(data.attention_tasks[0].summary.task.title == "Everything urgent");
    CHECK(data.attention_tasks[0].reason == modra::AttentionReason::overdue);
    CHECK(data.attention_tasks[1].reason == modra::AttentionReason::blocked);
    CHECK(data.attention_tasks[2].reason == modra::AttentionReason::critical);
}

TEST_CASE("Dashboard returns useful empty data and reloads after changes") {
    TemporaryDashboardDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    modra::Database database(paths.database);
    database.apply_migrations();
    modra::DashboardService dashboard(database);
    auto data = dashboard.load("2026-07-22");
    CHECK(data.active_project_count == 0);
    CHECK(data.active_task_count == 0);
    CHECK(data.upcoming_tasks.empty());
    CHECK(data.attention_tasks.empty());

    modra::ProjectService projects(database);
    modra::TaskService tasks(database);
    const auto project = projects.create({"Reload", "reload"});
    tasks.create({project.id, "New task"});
    data = dashboard.load("2026-07-22");
    CHECK(data.active_project_count == 1);
    CHECK(data.active_task_count == 1);
}

TEST_CASE("Dashboard results persist after reopening SQLite") {
    TemporaryDashboardDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        modra::TaskService tasks(database);
        const auto project = projects.create({"Persistent", "persistent"});
        tasks.create({project.id, "Persistent task"});
    }
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::DashboardService dashboard(database);
        const auto data = dashboard.load("2026-07-22");
        CHECK(data.active_project_count == 1);
        CHECK(data.active_task_count == 1);
    }
}
