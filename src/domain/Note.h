#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace modra {

enum class NoteType {
    general,
    technical,
    solution,
    meeting,
    sql,
    procedure,
    configuration,
    reference,
};

struct NoteInput {
    std::string title;
    NoteType type = NoteType::general;
    std::string content;
    std::optional<std::int64_t> project_id;
    std::optional<std::int64_t> task_id;
    bool is_favorite = false;
};

struct Note {
    std::int64_t id = 0;
    std::string title;
    NoteType type = NoteType::general;
    std::string content;
    std::optional<std::int64_t> project_id;
    std::optional<std::int64_t> task_id;
    bool is_favorite = false;
    std::string created_at;
    std::string updated_at;
    std::optional<std::string> archived_at;
};

struct NoteSummary {
    Note note;
    std::optional<std::string> project_name;
    std::optional<std::string> project_alias;
    std::optional<std::string> task_title;
};

std::string_view note_type_name(NoteType type);
std::string_view note_type_label(NoteType type);
NoteType note_type_from_name(std::string_view name);

}  // namespace modra
