#pragma once

#include <ftxui/component/component_base.hpp>

namespace modra {

class ProjectService;

ftxui::Component create_tools_screen(ProjectService& projects);

}  // namespace modra
