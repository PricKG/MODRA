#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "application/NoteDocument.h"
#include "domain/Note.h"
#include "infrastructure/database/NoteRepository.h"
#include "infrastructure/database/ProjectRepository.h"
#include "infrastructure/database/TaskRepository.h"
#include "infrastructure/system/ExternalEditor.h"

namespace modra {

class Database;

struct NoteQuery {
    bool archived = false;
    std::string search;
    std::optional<NoteType> type;
    std::optional<std::int64_t> project_id;
    std::optional<std::int64_t> task_id;
    bool only_favorites = false;
    bool only_global = false;
    bool with_task = false;
    bool without_task = false;
};

struct EditedNoteDocument {
    ParsedNoteDocument document;
    std::filesystem::path temporary_file;
};

class NoteService {
public:
    NoteService(Database& database,
                std::filesystem::path config_path,
                std::filesystem::path temporary_directory);

    Note create(NoteInput input);
    std::optional<Note> find_by_id(std::int64_t id) const;
    std::optional<NoteSummary> find_summary_by_id(std::int64_t id) const;
    std::vector<NoteSummary> list(const NoteQuery& query = {}) const;
    std::vector<NoteSummary> list_active() const;
    std::vector<NoteSummary> list_archived() const;
    std::vector<NoteSummary> list_by_project(std::int64_t project_id) const;
    std::vector<NoteSummary> list_by_task(std::int64_t task_id) const;
    std::vector<NoteSummary> list_favorites() const;
    Note update(std::int64_t id, NoteInput input);
    Note set_favorite(std::int64_t id, bool favorite);
    Note archive(std::int64_t id);
    Note restore(std::int64_t id);
    EditedNoteDocument edit_external(const std::string& title, const std::string& body) const;
    void complete_external_edit(const std::filesystem::path& temporary_file) const;

private:
    NoteInput normalize_and_validate(NoteInput input) const;

    ProjectRepository projects_;
    TaskRepository tasks_;
    NoteRepository notes_;
    ExternalEditor editor_;
};

}  // namespace modra
