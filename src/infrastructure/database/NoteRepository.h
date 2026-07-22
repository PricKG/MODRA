#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "domain/Note.h"

namespace modra {

class Database;

class NoteRepository {
public:
    explicit NoteRepository(Database& database);

    Note create(const NoteInput& input);
    std::optional<Note> find_by_id(std::int64_t id) const;
    std::optional<NoteSummary> find_summary_by_id(std::int64_t id) const;
    std::vector<NoteSummary> list_active() const;
    std::vector<NoteSummary> list_archived() const;
    std::vector<NoteSummary> list_by_project(std::int64_t project_id) const;
    std::vector<NoteSummary> list_by_task(std::int64_t task_id) const;
    std::vector<NoteSummary> list_favorites() const;
    std::vector<NoteSummary> search(const std::string& query, bool archived) const;
    Note update(std::int64_t id, const NoteInput& input);
    Note set_favorite(std::int64_t id, bool favorite);
    Note archive(std::int64_t id);
    Note restore(std::int64_t id);

private:
    Database& database_;
};

}  // namespace modra
