#include "infrastructure/database/NoteRepository.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include "infrastructure/database/Database.h"

namespace modra {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

Statement prepare(sqlite3* database, const std::string& sql) {
    sqlite3_stmt* raw = nullptr;
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &raw, nullptr) != SQLITE_OK) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Note persistence prepare error: {}", message);
        throw std::runtime_error("Error de SQLite al preparar notas: " + message);
    }
    return Statement(raw, sqlite3_finalize);
}

void bind_text(sqlite3* database, sqlite3_stmt* statement, int index, const std::string& value) {
    if (sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        throw std::runtime_error("Error de SQLite al enlazar notas: " + std::string(sqlite3_errmsg(database)));
    }
}

void bind_optional_id(sqlite3* database,
                      sqlite3_stmt* statement,
                      int index,
                      const std::optional<std::int64_t>& value) {
    const int result = value ? sqlite3_bind_int64(statement, index, *value) : sqlite3_bind_null(statement, index);
    if (result != SQLITE_OK) {
        throw std::runtime_error("Error de SQLite al enlazar notas: " + std::string(sqlite3_errmsg(database)));
    }
}

std::optional<std::string> optional_text(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    return reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
}

std::optional<std::int64_t> optional_id(sqlite3_stmt* statement, int column) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_int64(statement, column);
}

Note read_note(sqlite3_stmt* statement) {
    Note note;
    note.id = sqlite3_column_int64(statement, 0);
    note.title = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    note.type = note_type_from_name(reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)));
    note.content = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
    note.project_id = optional_id(statement, 4);
    note.task_id = optional_id(statement, 5);
    note.is_favorite = sqlite3_column_int(statement, 6) != 0;
    note.created_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 7));
    note.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(statement, 8));
    note.archived_at = optional_text(statement, 9);
    return note;
}

NoteSummary read_summary(sqlite3_stmt* statement) {
    NoteSummary summary;
    summary.note = read_note(statement);
    summary.project_name = optional_text(statement, 10);
    summary.project_alias = optional_text(statement, 11);
    summary.task_title = optional_text(statement, 12);
    return summary;
}

constexpr const char* note_columns =
    "n.id, n.title, n.type, n.content, n.project_id, n.task_id, n.is_favorite, n.created_at, n.updated_at, "
    "n.archived_at, p.name, p.alias, t.title ";

std::vector<NoteSummary> query_summaries(Database& database,
                                         const std::string& where,
                                         const std::optional<std::int64_t>& id = std::nullopt,
                                         const std::optional<std::string>& text = std::nullopt) {
    auto statement = prepare(
        database.handle(), std::string("SELECT ") + note_columns +
                               "FROM notes n LEFT JOIN projects p ON p.id = n.project_id "
                               "LEFT JOIN tasks t ON t.id = n.task_id WHERE " + where +
                               " ORDER BY n.is_favorite DESC, n.updated_at DESC, n.title COLLATE NOCASE, n.id DESC;"
    );
    if (id) sqlite3_bind_int64(statement.get(), 1, *id);
    if (text) bind_text(database.handle(), statement.get(), 1, *text);

    std::vector<NoteSummary> notes;
    int result = SQLITE_ROW;
    while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) notes.push_back(read_summary(statement.get()));
    if (result != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database.handle());
        spdlog::error("Note persistence query error: {}", message);
        throw std::runtime_error("Error de SQLite al consultar notas: " + message);
    }
    return notes;
}

void execute_write(sqlite3* database, sqlite3_stmt* statement) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        const std::string message = sqlite3_errmsg(database);
        spdlog::error("Note persistence write error: {}", message);
        throw std::runtime_error("Error de SQLite al guardar la nota: " + message);
    }
}

}  // namespace

NoteRepository::NoteRepository(Database& database) : database_(database) {
    sqlite3_extended_result_codes(database_.handle(), 1);
}

Note NoteRepository::create(const NoteInput& input) {
    auto statement = prepare(
        database_.handle(),
        "INSERT INTO notes(title, type, content, project_id, task_id, is_favorite, created_at, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), "
        "strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));"
    );
    bind_text(database_.handle(), statement.get(), 1, input.title);
    bind_text(database_.handle(), statement.get(), 2, std::string(note_type_name(input.type)));
    bind_text(database_.handle(), statement.get(), 3, input.content);
    bind_optional_id(database_.handle(), statement.get(), 4, input.project_id);
    bind_optional_id(database_.handle(), statement.get(), 5, input.task_id);
    sqlite3_bind_int(statement.get(), 6, input.is_favorite ? 1 : 0);
    execute_write(database_.handle(), statement.get());
    const auto note = find_by_id(sqlite3_last_insert_rowid(database_.handle()));
    if (!note) throw std::runtime_error("No se pudo recuperar la nota creada.");
    return *note;
}

std::optional<Note> NoteRepository::find_by_id(std::int64_t id) const {
    const auto summary = find_summary_by_id(id);
    return summary ? std::optional<Note>{summary->note} : std::nullopt;
}

std::optional<NoteSummary> NoteRepository::find_summary_by_id(std::int64_t id) const {
    auto statement = prepare(
        database_.handle(), std::string("SELECT ") + note_columns +
                                "FROM notes n LEFT JOIN projects p ON p.id = n.project_id "
                                "LEFT JOIN tasks t ON t.id = n.task_id WHERE n.id = ?1;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    const int result = sqlite3_step(statement.get());
    if (result == SQLITE_ROW) return read_summary(statement.get());
    if (result == SQLITE_DONE) return std::nullopt;
    throw std::runtime_error("Error de SQLite al consultar la nota: " + std::string(sqlite3_errmsg(database_.handle())));
}

std::vector<NoteSummary> NoteRepository::list_active() const {
    return query_summaries(database_, "n.archived_at IS NULL");
}

std::vector<NoteSummary> NoteRepository::list_archived() const {
    return query_summaries(database_, "n.archived_at IS NOT NULL");
}

std::vector<NoteSummary> NoteRepository::list_by_project(std::int64_t project_id) const {
    return query_summaries(database_, "n.archived_at IS NULL AND n.project_id = ?1", project_id);
}

std::vector<NoteSummary> NoteRepository::list_by_task(std::int64_t task_id) const {
    return query_summaries(database_, "n.archived_at IS NULL AND n.task_id = ?1", task_id);
}

std::vector<NoteSummary> NoteRepository::list_favorites() const {
    return query_summaries(database_, "n.archived_at IS NULL AND n.is_favorite = 1");
}

std::vector<NoteSummary> NoteRepository::search(const std::string& query, bool archived) const {
    const std::string match = "%" + query + "%";
    return query_summaries(
        database_,
        std::string(archived ? "n.archived_at IS NOT NULL" : "n.archived_at IS NULL") +
            " AND (n.title LIKE ?1 COLLATE NOCASE OR n.content LIKE ?1 COLLATE NOCASE OR "
            "n.type LIKE ?1 COLLATE NOCASE OR "
            "CASE n.type WHEN 'general' THEN 'General' WHEN 'technical' THEN 'Técnica' "
            "WHEN 'solution' THEN 'Solución' WHEN 'meeting' THEN 'Reunión' WHEN 'sql' THEN 'SQL' "
            "WHEN 'procedure' THEN 'Procedimiento' WHEN 'configuration' THEN 'Configuración' "
            "WHEN 'reference' THEN 'Referencia' END LIKE ?1 COLLATE NOCASE OR "
            "p.name LIKE ?1 COLLATE NOCASE OR p.alias LIKE ?1 COLLATE NOCASE OR t.title LIKE ?1 COLLATE NOCASE)",
        std::nullopt,
        match
    );
}

Note NoteRepository::update(std::int64_t id, const NoteInput& input) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE notes SET title=?1, type=?2, content=?3, project_id=?4, task_id=?5, is_favorite=?6, "
        "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id=?7;"
    );
    bind_text(database_.handle(), statement.get(), 1, input.title);
    bind_text(database_.handle(), statement.get(), 2, std::string(note_type_name(input.type)));
    bind_text(database_.handle(), statement.get(), 3, input.content);
    bind_optional_id(database_.handle(), statement.get(), 4, input.project_id);
    bind_optional_id(database_.handle(), statement.get(), 5, input.task_id);
    sqlite3_bind_int(statement.get(), 6, input.is_favorite ? 1 : 0);
    sqlite3_bind_int64(statement.get(), 7, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) throw std::runtime_error("La nota no existe.");
    const auto note = find_by_id(id);
    if (!note) throw std::runtime_error("No se pudo recuperar la nota editada.");
    return *note;
}

Note NoteRepository::set_favorite(std::int64_t id, bool favorite) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE notes SET is_favorite=?1, updated_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id=?2;"
    );
    sqlite3_bind_int(statement.get(), 1, favorite ? 1 : 0);
    sqlite3_bind_int64(statement.get(), 2, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) throw std::runtime_error("La nota no existe.");
    const auto note = find_by_id(id);
    if (!note) throw std::runtime_error("No se pudo recuperar la nota actualizada.");
    return *note;
}

Note NoteRepository::archive(std::int64_t id) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE notes SET archived_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), "
        "updated_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now') WHERE id=?1 AND archived_at IS NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) throw std::runtime_error("La nota no existe o ya está archivada.");
    const auto note = find_by_id(id);
    if (!note) throw std::runtime_error("No se pudo recuperar la nota archivada.");
    return *note;
}

Note NoteRepository::restore(std::int64_t id) {
    auto statement = prepare(
        database_.handle(),
        "UPDATE notes SET archived_at=NULL, updated_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
        "WHERE id=?1 AND archived_at IS NOT NULL;"
    );
    sqlite3_bind_int64(statement.get(), 1, id);
    execute_write(database_.handle(), statement.get());
    if (sqlite3_changes(database_.handle()) == 0) throw std::runtime_error("La nota no existe o no está archivada.");
    const auto note = find_by_id(id);
    if (!note) throw std::runtime_error("No se pudo recuperar la nota desarchivada.");
    return *note;
}

}  // namespace modra
