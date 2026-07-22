#pragma once

#include <functional>
#include <optional>

#include <ftxui/component/component.hpp>

#include "domain/Project.h"

namespace modra {

class TaskService;
class NoteService;
struct KnowledgeRequest;

ftxui::Component create_task_screen(TaskService& tasks,
                                    NoteService& notes,
                                    std::function<std::optional<Project>()> current_project,
                                    std::function<void(KnowledgeRequest)> on_knowledge,
                                    std::function<void()> on_back);

}  // namespace modra
