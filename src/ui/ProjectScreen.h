#pragma once

#include <functional>
#include <optional>

#include <ftxui/component/component.hpp>

#include "domain/Project.h"

namespace modra {

class ProjectService;
class NoteService;
struct KnowledgeRequest;

ftxui::Component create_project_screen(ProjectService& projects,
                                       NoteService& notes,
                                       std::function<std::optional<Project>()> requested_project,
                                       std::function<void(const Project&)> on_tasks,
                                       std::function<void(KnowledgeRequest)> on_knowledge,
                                       std::function<void()> on_back);

}  // namespace modra
