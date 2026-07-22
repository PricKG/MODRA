#include "application/NoteService.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "infrastructure/database/Database.h"

namespace modra {

NoteService::NoteService(Database& database,
                         std::filesystem::path config_path,
                         std::filesystem::path temporary_directory)
    : projects_(database),
      tasks_(database),
      notes_(database),
      editor_(std::move(config_path), std::move(temporary_directory)) {}

NoteInput NoteService::normalize_and_validate(NoteInput input) const {
    if (input.title.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("El título es obligatorio.");
    }
    if (input.content.find_first_not_of(" \t\r\n") == std::string::npos) {
        throw std::invalid_argument("El contenido es obligatorio.");
    }
    static_cast<void>(note_type_name(input.type));

    if (input.project_id && !projects_.find_by_id(*input.project_id)) {
        throw std::invalid_argument("El proyecto asociado no existe.");
    }
    if (input.task_id) {
        const auto task = tasks_.find_by_id(*input.task_id);
        if (!task) throw std::invalid_argument("La tarea asociada no existe.");
        if (input.project_id && *input.project_id != task->project_id) {
            throw std::invalid_argument("La tarea no pertenece al proyecto seleccionado.");
        }
        input.project_id = task->project_id;
    }
    return input;
}

Note NoteService::create(NoteInput input) {
    input = normalize_and_validate(std::move(input));
    const Note note = notes_.create(input);
    spdlog::info("Note created: id={}, type={}, project_id={}", note.id, note_type_name(note.type),
                 note.project_id.value_or(0));
    return note;
}

std::optional<Note> NoteService::find_by_id(std::int64_t id) const {
    return notes_.find_by_id(id);
}

std::optional<NoteSummary> NoteService::find_summary_by_id(std::int64_t id) const {
    return notes_.find_summary_by_id(id);
}

std::vector<NoteSummary> NoteService::list(const NoteQuery& query) const {
    auto loaded = query.search.empty() ? (query.archived ? notes_.list_archived() : notes_.list_active())
                                       : notes_.search(query.search, query.archived);
    loaded.erase(
        std::remove_if(loaded.begin(), loaded.end(), [&](const NoteSummary& summary) {
            const Note& note = summary.note;
            return (query.type && note.type != *query.type) ||
                   (query.project_id && note.project_id != query.project_id) ||
                   (query.task_id && note.task_id != query.task_id) ||
                   (query.only_favorites && !note.is_favorite) || (query.only_global && note.project_id) ||
                   (query.with_task && !note.task_id) || (query.without_task && note.task_id);
        }),
        loaded.end());
    return loaded;
}

std::vector<NoteSummary> NoteService::list_active() const {
    return notes_.list_active();
}

std::vector<NoteSummary> NoteService::list_archived() const {
    return notes_.list_archived();
}

std::vector<NoteSummary> NoteService::list_by_project(std::int64_t project_id) const {
    return notes_.list_by_project(project_id);
}

std::vector<NoteSummary> NoteService::list_by_task(std::int64_t task_id) const {
    return notes_.list_by_task(task_id);
}

std::vector<NoteSummary> NoteService::list_favorites() const {
    return notes_.list_favorites();
}

Note NoteService::update(std::int64_t id, NoteInput input) {
    const auto existing = notes_.find_by_id(id);
    if (!existing) throw std::runtime_error("La nota no existe.");
    input = normalize_and_validate(std::move(input));
    if (existing->title == input.title && existing->type == input.type && existing->content == input.content &&
        existing->project_id == input.project_id && existing->task_id == input.task_id &&
        existing->is_favorite == input.is_favorite) {
        return *existing;
    }
    const Note note = notes_.update(id, input);
    spdlog::info("Note updated: id={}, type={}, project_id={}", note.id, note_type_name(note.type),
                 note.project_id.value_or(0));
    return note;
}

Note NoteService::set_favorite(std::int64_t id, bool favorite) {
    const auto existing = notes_.find_by_id(id);
    if (!existing) throw std::runtime_error("La nota no existe.");
    if (existing->is_favorite == favorite) return *existing;
    const Note note = notes_.set_favorite(id, favorite);
    spdlog::info("Note favorite changed: id={}, favorite={}", id, favorite);
    return note;
}

Note NoteService::archive(std::int64_t id) {
    const Note note = notes_.archive(id);
    spdlog::info("Note archived: id={}, type={}, project_id={}", note.id, note_type_name(note.type),
                 note.project_id.value_or(0));
    return note;
}

Note NoteService::restore(std::int64_t id) {
    const auto existing = notes_.find_by_id(id);
    if (!existing) throw std::runtime_error("La nota no existe.");
    if (!existing->archived_at) throw std::runtime_error("La nota no está archivada.");
    const Note note = notes_.restore(id);
    spdlog::info("Note restored: id={}, type={}, project_id={}", note.id, note_type_name(note.type),
                 note.project_id.value_or(0));
    return note;
}

std::string NoteService::edit_external(const std::string& title, const std::string& initial_content) const {
    return editor_.edit(title, initial_content);
}

}  // namespace modra
