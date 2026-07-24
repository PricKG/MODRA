#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <sqlite3.h>

#include "application/DashboardService.h"
#include "application/NoteService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"
#include "ui/DashboardScreen.h"

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
          notes(database, paths.config, paths.root),
          dashboard(database) {
        database.apply_migrations();
        alpha = projects.create({"Proyecto Alpha", "alpha"});
        beta = projects.create({"Proyecto Beta", "beta"});
    }

    modra::Note create_note(std::string title,
                            std::optional<std::int64_t> project_id = std::nullopt,
                            std::optional<std::int64_t> task_id = std::nullopt,
                            bool favorite = true) {
        modra::NoteInput input{std::move(title), modra::NoteType::general, "Contenido"};
        input.project_id = project_id;
        input.task_id = task_id;
        input.is_favorite = favorite;
        return notes.create(std::move(input));
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
    modra::NoteService notes;
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

TEST_CASE("Dashboard shows the five most recently updated favorite notes and counts the rest") {
    DashboardFixture fixture;
    std::vector<modra::Note> created;
    for (int index = 1; index <= 6; ++index) {
        created.push_back(fixture.create_note("Favorita " + std::to_string(index)));
        const int day = index >= 5 ? 16 : 10 + index;
        const std::string sql = "UPDATE notes SET updated_at='2026-07-" + std::to_string(day) +
                                "T12:00:00.000Z' WHERE id=" + std::to_string(created.back().id) + ";";
        REQUIRE(sqlite3_exec(fixture.database.handle(), sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
    }

    const auto data = fixture.dashboard.load(fixture.today);
    CHECK(data.favorite_note_count == 6);
    CHECK(data.additional_favorite_count == 1);
    REQUIRE(data.favorite_notes.size() == 5);
    CHECK(data.favorite_notes.front().note.title == "Favorita 5");
    CHECK(data.favorite_notes[1].note.title == "Favorita 6");
    CHECK(data.favorite_notes.back().note.title == "Favorita 2");
}

TEST_CASE("Dashboard favorite notes include global project task and archived relation context in one query") {
    DashboardFixture fixture;
    fixture.create_note("Global");
    fixture.create_note("Proyecto archivado", fixture.beta.id);
    const auto task = fixture.create(fixture.alpha, "Tarea archivada");
    fixture.create_note("Con tarea", fixture.alpha.id, task.id);
    fixture.tasks.archive(task.id);
    fixture.projects.archive(fixture.beta.id);

    int note_selects = 0;
    sqlite3_trace_v2(
        fixture.database.handle(), SQLITE_TRACE_STMT,
        [](unsigned, void* context, void* statement, void*) {
            const char* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
            if (sql && std::string_view(sql).find("FROM notes") != std::string_view::npos) {
                ++*static_cast<int*>(context);
            }
            return 0;
        },
        &note_selects);
    const auto data = fixture.dashboard.load(fixture.today);
    sqlite3_trace_v2(fixture.database.handle(), 0, nullptr, nullptr);

    CHECK(note_selects == 1);
    REQUIRE(data.favorite_notes.size() == 3);
    const auto find = [&](const std::string& title) -> const modra::NoteSummary& {
        const auto result = std::find_if(data.favorite_notes.begin(), data.favorite_notes.end(),
                                         [&](const auto& item) { return item.note.title == title; });
        REQUIRE(result != data.favorite_notes.end());
        return *result;
    };
    CHECK_FALSE(find("Global").note.project_id);
    CHECK(find("Proyecto archivado").project_name == fixture.beta.name);
    CHECK(find("Proyecto archivado").project_archived);
    CHECK(find("Con tarea").project_name == fixture.alpha.name);
    CHECK(find("Con tarea").task_title == task.title);
    CHECK(find("Con tarea").task_archived);
}

TEST_CASE("Archiving hides a favorite note and restoring preserves its favorite state") {
    DashboardFixture fixture;
    const auto note = fixture.create_note("Recuperable");
    fixture.notes.archive(note.id);
    CHECK(fixture.dashboard.load(fixture.today).favorite_notes.empty());
    REQUIRE(fixture.notes.find_by_id(note.id));
    CHECK(fixture.notes.find_by_id(note.id)->is_favorite);

    fixture.notes.restore(note.id);
    const auto data = fixture.dashboard.load(fixture.today);
    REQUIRE(data.favorite_notes.size() == 1);
    CHECK(data.favorite_notes.front().note.id == note.id);
    CHECK(data.favorite_notes.front().note.is_favorite);
}

TEST_CASE("Dashboard opens a favorite note and the complete favorites view") {
    DashboardFixture fixture;
    fixture.create_note("Primera");
    fixture.create_note("Segunda");
    const auto data = fixture.dashboard.load(fixture.today);
    REQUIRE(data.favorite_notes.size() == 2);

    std::optional<std::int64_t> opened_note;
    bool opened_all = false;
    auto screen = modra::create_dashboard_screen(
        fixture.dashboard,
        [&](std::int64_t id) { opened_note = id; },
        [&] { opened_all = true; });
    CHECK(screen->OnEvent(ftxui::Event::Return));
    CHECK(opened_note == data.favorite_notes.front().note.id);
    CHECK(screen->OnEvent(ftxui::Event::ArrowDown));
    CHECK(screen->OnEvent(ftxui::Event::ArrowDown));
    CHECK(screen->OnEvent(ftxui::Event::Return));
    CHECK(opened_all);
}

TEST_CASE("Dashboard renders favorite note content excerpts") {
    DashboardFixture fixture;
    const auto note = fixture.create_note("Procedimiento");
    modra::NoteInput input{note.title, note.type,
                           "# Procedimiento\n\nRevisar release, checksum y enlace público antes de publicar."};
    input.is_favorite = true;
    fixture.notes.update(note.id, input);

    auto screen = modra::create_dashboard_screen(fixture.dashboard, [](std::int64_t) {}, [] {});
    auto rendered = screen->Render();
    auto output = ftxui::Screen::Create(ftxui::Dimension::Fixed(120), ftxui::Dimension::Fixed(80));
    Render(output, rendered);

    CHECK(output.ToString().find("Revisar release") != std::string::npos);
}

TEST_CASE("Multiple favorite notes remain independent after reopening SQLite") {
    TemporaryDashboardDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    std::int64_t kept_id = 0;
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::NoteService notes(database, paths.config, paths.root);
        auto first = notes.create({"Primera", modra::NoteType::general, "Contenido"});
        auto second = notes.create({"Segunda", modra::NoteType::general, "Contenido"});
        auto third = notes.create({"Tercera", modra::NoteType::general, "Contenido"});
        notes.set_favorite(first.id, true);
        notes.set_favorite(second.id, true);
        notes.set_favorite(third.id, true);
        notes.set_favorite(second.id, false);
        kept_id = first.id;
    }
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::DashboardService dashboard(database);
        const auto data = dashboard.load("2026-07-22");
        REQUIRE(data.favorite_note_count == 2);
        CHECK(std::any_of(data.favorite_notes.begin(), data.favorite_notes.end(),
                          [&](const auto& item) { return item.note.id == kept_id; }));
        CHECK(std::none_of(data.favorite_notes.begin(), data.favorite_notes.end(),
                           [](const auto& item) { return item.note.title == "Segunda"; }));
    }
}
