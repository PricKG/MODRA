#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <ftxui/component/component.hpp>

namespace modra {

class NoteService;
class ProjectService;
class TaskService;

struct KnowledgeRequest {
    std::optional<std::int64_t> note_id;
    std::optional<std::int64_t> project_id;
    std::optional<std::int64_t> task_id;
    bool create = false;
};

ftxui::Component create_knowledge_screen(
    NoteService& notes,
    ProjectService& projects,
    TaskService& tasks,
    std::function<std::optional<KnowledgeRequest>()> requested,
    std::function<void(std::int64_t)> on_project,
    std::function<void(std::int64_t)> on_task,
    std::function<void()> on_back,
    std::function<void(const std::function<void()>&)> with_restored_io);

}  // namespace modra
