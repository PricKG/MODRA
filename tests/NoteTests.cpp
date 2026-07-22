#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "application/NoteService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "infrastructure/config/DataDirectory.h"
#include "infrastructure/database/Database.h"
#include "infrastructure/system/ExternalEditor.h"

namespace {

class TemporaryNoteDirectory {
public:
    TemporaryNoteDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / ("modra-note-tests-" + std::to_string(suffix));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryNoteDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

struct NoteFixture {
    NoteFixture()
        : paths(modra::initialize_data_directory(temporary.path() / "data")), database(paths.database),
          projects(database), tasks(database), notes(database, paths.config, paths.root) {
        database.apply_migrations();
        project = projects.create({"MODRA", "modra"});
        task = tasks.create({project.id, "Preparar conocimiento"});
    }
    modra::NoteInput valid(std::string title = "Solución SQLite") const {
        return {std::move(title), modra::NoteType::solution, "# Solución\n\nUsar una transacción.",
                project.id, std::nullopt, false};
    }
    TemporaryNoteDirectory temporary;
    modra::DataPaths paths;
    modra::Database database;
    modra::ProjectService projects;
    modra::TaskService tasks;
    modra::NoteService notes;
    modra::Project project;
    modra::Task task;
};

std::string quoted_fixture_command(const std::string& mode) {
    return std::string("\"") + MODRA_EDITOR_FIXTURE + "\" " + mode;
}

void configure_editor(const std::filesystem::path& config_path, const std::string& mode) {
    std::ofstream output(config_path, std::ios::trunc);
    output << nlohmann::json{{"version", 1}, {"editor", {{"command", quoted_fixture_command(mode)}}}}.dump(2);
}

}  // namespace

TEST_CASE("A note validates title content and type") {
    NoteFixture fixture;
    const auto note = fixture.notes.create(fixture.valid());
    CHECK(note.id > 0);
    CHECK(note.type == modra::NoteType::solution);
    CHECK(note.content.find("transacción") != std::string::npos);

    auto input = fixture.valid();
    input.title.clear();
    CHECK_THROWS_WITH(fixture.notes.create(input), "El título es obligatorio.");
    input = fixture.valid();
    input.title = "   ";
    CHECK_THROWS_WITH(fixture.notes.create(input), "El título es obligatorio.");
    input = fixture.valid();
    input.content.clear();
    CHECK_THROWS_WITH(fixture.notes.create(input), "El contenido es obligatorio.");
    input = fixture.valid();
    input.content = "\n\t ";
    CHECK_THROWS_WITH(fixture.notes.create(input), "El contenido es obligatorio.");
    input = fixture.valid();
    input.type = static_cast<modra::NoteType>(999);
    CHECK_THROWS_WITH(fixture.notes.create(input), "Tipo de nota inválido.");
}

TEST_CASE("A default note is global general knowledge") {
    NoteFixture fixture;
    modra::NoteInput input;
    input.title = "Nota global";
    input.content = "Contenido general";
    const auto note = fixture.notes.create(input);
    CHECK(note.type == modra::NoteType::general);
    CHECK_FALSE(note.project_id);
    CHECK_FALSE(note.task_id);
}

TEST_CASE("Note project and task relations are validated and inferred") {
    NoteFixture fixture;
    auto input = fixture.valid("Vinculada con tarea");
    input.project_id.reset();
    input.task_id = fixture.task.id;
    const auto note = fixture.notes.create(input);
    CHECK(note.project_id == fixture.project.id);
    CHECK(note.task_id == fixture.task.id);

    input = fixture.valid();
    input.project_id = 999999;
    CHECK_THROWS_WITH(fixture.notes.create(input), "El proyecto asociado no existe.");
    input = fixture.valid();
    input.task_id = 999999;
    CHECK_THROWS_WITH(fixture.notes.create(input), "La tarea asociada no existe.");

    const auto other = fixture.projects.create({"Otro", "otro"});
    input = fixture.valid();
    input.project_id = other.id;
    input.task_id = fixture.task.id;
    CHECK_THROWS_WITH(fixture.notes.create(input), "La tarea no pertenece al proyecto seleccionado.");
}

TEST_CASE("Notes remain editable for archived projects and tasks") {
    NoteFixture fixture;
    fixture.tasks.archive(fixture.task.id);
    fixture.projects.archive(fixture.project.id);
    auto input = fixture.valid();
    input.task_id = fixture.task.id;
    const auto created = fixture.notes.create(input);
    input.title = "Contexto que continúa evolucionando";
    const auto updated = fixture.notes.update(created.id, input);
    CHECK(updated.title == input.title);
}

TEST_CASE("Editing without changes preserves updated_at") {
    NoteFixture fixture;
    const auto created = fixture.notes.create(fixture.valid());
    const auto unchanged = fixture.notes.update(created.id, fixture.valid());
    CHECK(unchanged.updated_at == created.updated_at);

    auto changed_input = fixture.valid("Título editado");
    changed_input.type = modra::NoteType::technical;
    changed_input.content = "Contenido editado";
    const auto changed = fixture.notes.update(created.id, changed_input);
    CHECK(changed.title == "Título editado");
    CHECK(changed.type == modra::NoteType::technical);
    CHECK(changed.content == "Contenido editado");
}

TEST_CASE("Favorites and logical archive update the note lists") {
    NoteFixture fixture;
    const auto created = fixture.notes.create(fixture.valid());
    const auto favorite = fixture.notes.set_favorite(created.id, true);
    CHECK(favorite.is_favorite);
    REQUIRE(fixture.notes.list_favorites().size() == 1);
    CHECK(fixture.notes.set_favorite(created.id, false).is_favorite == false);

    const auto archived = fixture.notes.archive(created.id);
    REQUIRE(archived.archived_at);
    CHECK(fixture.notes.list_active().empty());
    REQUIRE(fixture.notes.list_archived().size() == 1);
    CHECK(fixture.notes.list_archived().front().note.id == created.id);
}

TEST_CASE("An archived note can be restored") {
    NoteFixture fixture;
    const auto created = fixture.notes.create(fixture.valid());
    fixture.notes.archive(created.id);

    const auto restored = fixture.notes.restore(created.id);

    CHECK_FALSE(restored.archived_at);
    REQUIRE(fixture.notes.list_active().size() == 1);
    CHECK(fixture.notes.list_active().front().note.id == created.id);
    CHECK(fixture.notes.list_archived().empty());
    CHECK_THROWS_WITH(fixture.notes.restore(created.id), "La nota no está archivada.");
}

TEST_CASE("A note can be restored when its project and task are archived") {
    NoteFixture fixture;
    auto input = fixture.valid("Conocimiento histórico");
    input.task_id = fixture.task.id;
    const auto created = fixture.notes.create(input);
    fixture.notes.archive(created.id);
    fixture.tasks.archive(fixture.task.id);
    fixture.projects.archive(fixture.project.id);

    const auto restored = fixture.notes.restore(created.id);

    CHECK_FALSE(restored.archived_at);
    CHECK(restored.project_id == fixture.project.id);
    CHECK(restored.task_id == fixture.task.id);
}

TEST_CASE("Notes list by project task and favorites first") {
    NoteFixture fixture;
    auto first = fixture.valid("Primera");
    first.task_id = fixture.task.id;
    const auto first_note = fixture.notes.create(first);
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    auto second = fixture.valid("Segunda");
    second.is_favorite = true;
    const auto second_note = fixture.notes.create(second);

    const auto by_project = fixture.notes.list_by_project(fixture.project.id);
    REQUIRE(by_project.size() == 2);
    CHECK(by_project.front().note.id == second_note.id);
    const auto by_task = fixture.notes.list_by_task(fixture.task.id);
    REQUIRE(by_task.size() == 1);
    CHECK(by_task.front().note.id == first_note.id);
}

TEST_CASE("Notes with equal favorite state order by latest modification") {
    NoteFixture fixture;
    const auto first = fixture.notes.create(fixture.valid("Primera"));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    fixture.notes.create(fixture.valid("Segunda"));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto edited = fixture.valid("Primera actualizada");
    const auto updated = fixture.notes.update(first.id, edited);
    const auto notes = fixture.notes.list_active();
    REQUIRE(notes.size() == 2);
    CHECK(notes.front().note.id == updated.id);
}

TEST_CASE("Note search includes content project task and visible type") {
    NoteFixture fixture;
    auto input = fixture.valid("Error de conexión");
    input.content = "ORA-00001 al guardar";
    input.task_id = fixture.task.id;
    fixture.notes.create(input);
    CHECK(fixture.notes.list({false, "conexión"}).size() == 1);
    CHECK(fixture.notes.list({false, "ORA-00001"}).size() == 1);
    CHECK(fixture.notes.list({false, "MODRA"}).size() == 1);
    CHECK(fixture.notes.list({false, "modra"}).size() == 1);
    CHECK(fixture.notes.list({false, "Preparar conocimiento"}).size() == 1);
    CHECK(fixture.notes.list({false, "Solución"}).size() == 1);
}

TEST_CASE("Note filters combine type project favorites and relation presence") {
    NoteFixture fixture;
    auto related = fixture.valid("Relacionada");
    related.task_id = fixture.task.id;
    related.is_favorite = true;
    fixture.notes.create(related);
    modra::NoteInput global{"Global", modra::NoteType::general, "Contenido"};
    fixture.notes.create(global);

    modra::NoteQuery query;
    query.type = modra::NoteType::solution;
    query.project_id = fixture.project.id;
    query.only_favorites = true;
    query.with_task = true;
    CHECK(fixture.notes.list(query).size() == 1);
    query = {};
    query.only_global = true;
    query.without_task = true;
    CHECK(fixture.notes.list(query).size() == 1);
}

TEST_CASE("The notes migration is applied once and preserves existing data") {
    TemporaryNoteDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    modra::Database database(paths.database);
    database.apply_migrations();
    modra::ProjectService projects(database);
    modra::TaskService tasks(database);
    const auto project = projects.create({"Persistente", "persistente"});
    const auto task = tasks.create({project.id, "Tarea persistente"});
    database.apply_migrations();
    CHECK(database.migration_count() == 5);
    REQUIRE(projects.find_by_id(project.id));
    REQUIRE(tasks.find_by_id(task.id));
}

TEST_CASE("Foreign keys remain enabled for notes") {
    NoteFixture fixture;
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(fixture.database.handle(),
                               "INSERT INTO notes(title,type,content,project_id,is_favorite,created_at,updated_at) "
                               "VALUES('Inválida','general','Contenido',999999,0,'now','now');",
                               -1, &statement, nullptr) == SQLITE_OK);
    CHECK(sqlite3_step(statement) == SQLITE_CONSTRAINT_FOREIGNKEY);
    sqlite3_finalize(statement);
}

TEST_CASE("Notes and relations persist after reopening SQLite") {
    TemporaryNoteDirectory temporary;
    const auto paths = modra::initialize_data_directory(temporary.path() / "data");
    std::int64_t note_id = 0;
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::ProjectService projects(database);
        modra::TaskService tasks(database);
        modra::NoteService notes(database, paths.config, paths.root);
        const auto project = projects.create({"Persistente", "persistente"});
        const auto task = tasks.create({project.id, "Tarea"});
        note_id = notes.create({"Nota", modra::NoteType::technical, "Contenido", project.id, task.id, true}).id;
    }
    {
        modra::Database database(paths.database);
        database.apply_migrations();
        modra::NoteService notes(database, paths.config, paths.root);
        const auto note = notes.find_summary_by_id(note_id);
        REQUIRE(note);
        CHECK(note->project_alias == "persistente");
        CHECK(note->task_title == "Tarea");
        CHECK(note->note.is_favorite);
    }
}

TEST_CASE("Editor resolution honors config VISUAL and EDITOR") {
    TemporaryNoteDirectory temporary;
    const auto config = temporary.path() / "config.json";
    {
        std::ofstream output(config);
        output << nlohmann::json{{"editor", {{"command", "configured --wait"}}}};
    }
    CHECK(modra::ExternalEditor::resolve_command(config, {"visual", "editor", true}) == "configured --wait");
    {
        std::ofstream output(config, std::ios::trunc);
        output << nlohmann::json{{"editor", {{"command", ""}}}};
    }
    CHECK(modra::ExternalEditor::resolve_command(config, {"visual --wait", "editor", true}) == "visual --wait");
    CHECK(modra::ExternalEditor::resolve_command(config, {std::nullopt, "editor --wait", true}) == "editor --wait");
    CHECK(modra::ExternalEditor::split_command("\"C:/Program Files/Editor/editor.exe\" --wait").front() ==
          "C:/Program Files/Editor/editor.exe");
}

TEST_CASE("Controlled editor reads UTF-8 multiline content and cleans its temporary file") {
    NoteFixture fixture;
    configure_editor(fixture.paths.config, "--write");
    const std::string edited = fixture.notes.edit_external("Prueba", "Contenido inicial");
    CHECK(edited.find("Línea UTF-8") != std::string::npos);
    CHECK(edited.find("```sql\nSELECT 1;") != std::string::npos);
    for (const auto& entry : std::filesystem::directory_iterator(fixture.paths.root))
        CHECK(entry.path().filename().string().find(".modra-note-") == std::string::npos);
}

TEST_CASE("An editor failure keeps the stored content unchanged") {
    NoteFixture fixture;
    const auto created = fixture.notes.create(fixture.valid());
    configure_editor(fixture.paths.config, "--fail");
    CHECK_THROWS_WITH(fixture.notes.edit_external(created.title, created.content),
                      "El editor externo terminó con error. La nota original no fue modificada.");
    const auto stored = fixture.notes.find_by_id(created.id);
    REQUIRE(stored);
    CHECK(stored->content == created.content);
}

TEST_CASE("A missing editor reports an error and keeps the stored content") {
    NoteFixture fixture;
    const auto created = fixture.notes.create(fixture.valid());
    {
        std::ofstream output(fixture.paths.config, std::ios::trunc);
        output << nlohmann::json{{"editor", {{"command", "modra-editor-that-does-not-exist"}}}};
    }
    CHECK_THROWS_WITH(fixture.notes.edit_external(created.title, created.content),
                      "No se pudo iniciar el editor externo.");
    REQUIRE(fixture.notes.find_by_id(created.id));
    CHECK(fixture.notes.find_by_id(created.id)->content == created.content);
}
