#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include <ftxui/component/component.hpp>

#include "application/TaskService.h"

namespace modra {

class ProjectService;
class TaskService;
class NoteService;
struct KnowledgeRequest;

struct WorkOpenRequest {
    TaskQuickView view = TaskQuickView::all;
    std::optional<std::int64_t> task_id;
};

ftxui::Component create_work_screen(TaskService& tasks,
                                    ProjectService& projects,
                                    NoteService& notes,
                                    std::function<void(std::int64_t)> on_open_project,
                                    std::function<void(KnowledgeRequest)> on_knowledge,
                                    std::function<std::optional<WorkOpenRequest>()> requested_open,
                                    std::function<void()> on_back);

}  // namespace modra
