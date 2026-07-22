#pragma once

#include <cstdint>
#include <functional>

#include <ftxui/component/component.hpp>

namespace modra {

class DashboardService;

ftxui::Component create_dashboard_screen(DashboardService& dashboard,
                                         std::function<void(std::int64_t)> on_note,
                                         std::function<void()> on_all_favorites);

}  // namespace modra
