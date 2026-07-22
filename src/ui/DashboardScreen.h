#pragma once

#include <ftxui/component/component.hpp>

namespace modra {

class DashboardService;

ftxui::Component create_dashboard_screen(DashboardService& dashboard);

}  // namespace modra
