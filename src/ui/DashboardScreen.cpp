#include "ui/DashboardScreen.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <spdlog/spdlog.h>

#include "application/DashboardService.h"
#include "application/TaskService.h"

namespace modra {
namespace {

using namespace ftxui;

std::string shortened(std::string value, std::size_t limit) {
    if (value.size() <= limit) return value;
    if (limit <= 3) return value.substr(0, limit);
    return value.substr(0, limit - 3) + "...";
}

std::string short_date(const std::optional<std::string>& date) {
    if (!date || date->size() < 10) return "Sin fecha";
    return date->substr(8, 2) + "/" + date->substr(5, 2);
}

const char* attention_label(AttentionReason reason) {
    switch (reason) {
        case AttentionReason::overdue: return "ATRASADA";
        case AttentionReason::blocked: return "BLOQUEADA";
        case AttentionReason::critical: return "CRÍTICA";
    }
    return "ATENCIÓN";
}

Color attention_color(AttentionReason reason) {
    switch (reason) {
        case AttentionReason::overdue: return Color::Red;
        case AttentionReason::blocked: return Color::Yellow;
        case AttentionReason::critical: return Color::RedLight;
    }
    return Color::Default;
}

Element metric_card(const std::string& title,
                    std::size_t value,
                    const std::string& secondary,
                    Color color_value) {
    return vbox({text(title) | bold | center,
                 text(std::to_string(value)) | bold | color(color_value) | center,
                 text(secondary) | dim | center}) |
           size(HEIGHT, EQUAL, 5) | border;
}

Element distribution_bar(const std::string& label,
                         std::size_t value,
                         std::size_t maximum,
                         Color color_value) {
    const float ratio = maximum == 0 ? 0.0F : static_cast<float>(value) / static_cast<float>(maximum);
    return hbox({text(label) | size(WIDTH, EQUAL, 13),
                 gauge(ratio) | color(color_value) | size(WIDTH, EQUAL, 14),
                 text(" " + std::to_string(value)) | bold});
}

struct DashboardState {
    explicit DashboardState(DashboardService& service_value) : service(service_value) {
        reload();
    }

    void reload() {
        try {
            today = TaskService::current_local_date();
            data = service.load(today);
            error.clear();
        } catch (const std::exception& exception) {
            error = "No se pudo cargar el dashboard. Volvé a ingresar para reintentar.";
            spdlog::error("Dashboard load error: {}", exception.what());
        }
    }

    DashboardService& service;
    DashboardData data;
    std::string today;
    std::string error;
};

}  // namespace

ftxui::Component create_dashboard_screen(DashboardService& dashboard) {
    using namespace ftxui;
    auto state = std::make_shared<DashboardState>(dashboard);
    auto component = Renderer([state] {
        const int terminal_width = Terminal::Size().dimx;
        const bool wide = terminal_width >= 125;
        const bool medium = terminal_width >= 88;
        const bool cards_in_rows = terminal_width >= 60;

        std::array<Element, 4> cards{
            metric_card("Proyectos", state->data.active_project_count, "activos", Color::Cyan),
            metric_card("Tareas", state->data.active_task_count, "en radar", Color::BlueLight),
            metric_card("Para hoy", state->data.due_today_count, "revisar hoy",
                        state->data.due_today_count == 0 ? Color::Green : Color::Yellow),
            metric_card("Atrasadas", state->data.overdue_count,
                        state->data.overdue_count == 0 ? "sin incidencias" : "requieren atención",
                        state->data.overdue_count == 0 ? Color::Green : Color::Red),
        };
        Element card_layout;
        if (wide) {
            card_layout = hbox({cards[0] | flex, cards[1] | flex, cards[2] | flex, cards[3] | flex});
        } else if (cards_in_rows) {
            card_layout = vbox({hbox({cards[0] | flex, cards[1] | flex}),
                                hbox({cards[2] | flex, cards[3] | flex})});
        } else {
            card_layout = vbox({cards[0], cards[1], cards[2], cards[3]});
        }

        Elements body{text(" MODRA — Dashboard ") | bold | color(Color::Cyan), card_layout};
        if (!state->error.empty()) {
            body.push_back(text(state->error) | color(Color::Red) | bold);
        }

        if (state->data.active_project_count == 0 && state->data.active_task_count == 0) {
            body.push_back(window(text(" COMENZAR ") | bold,
                                  vbox({text("MODRA todavía no tiene información.") | center,
                                        text("Creá un proyecto desde la sección Proyectos para comenzar.") | center | dim})));
        } else {
            const std::array<std::pair<std::string, std::size_t>, 4> statuses{{
                {"Pendientes", state->data.task_count_by_status[static_cast<std::size_t>(TaskStatus::pending)]},
                {"En curso", state->data.task_count_by_status[static_cast<std::size_t>(TaskStatus::in_progress)]},
                {"Bloqueadas", state->data.task_count_by_status[static_cast<std::size_t>(TaskStatus::blocked)]},
                {"En revisión", state->data.task_count_by_status[static_cast<std::size_t>(TaskStatus::in_review)]},
            }};
            std::size_t status_maximum = 0;
            for (const auto& [label, value] : statuses) {
                static_cast<void>(label);
                status_maximum = std::max(status_maximum, value);
            }
            Elements status_rows;
            if (status_maximum == 0) {
                status_rows.push_back(text("No hay tareas activas para representar.") | dim);
            } else {
                const Color colors[]{Color::BlueLight, Color::Cyan, Color::Yellow, Color::MagentaLight};
                for (std::size_t index = 0; index < statuses.size(); ++index) {
                    status_rows.push_back(distribution_bar(statuses[index].first, statuses[index].second,
                                                           status_maximum, colors[index]));
                }
            }
            auto status_panel = window(text(" Estado de tareas ") | bold, vbox(std::move(status_rows)));

            const std::array<std::pair<std::string, std::size_t>, 4> priorities{{
                {"Críticas", state->data.task_count_by_priority[static_cast<std::size_t>(TaskPriority::critical)]},
                {"Altas", state->data.task_count_by_priority[static_cast<std::size_t>(TaskPriority::high)]},
                {"Normales", state->data.task_count_by_priority[static_cast<std::size_t>(TaskPriority::normal)]},
                {"Bajas", state->data.task_count_by_priority[static_cast<std::size_t>(TaskPriority::low)]},
            }};
            std::size_t priority_maximum = 0;
            for (const auto& [label, value] : priorities) {
                static_cast<void>(label);
                priority_maximum = std::max(priority_maximum, value);
            }
            Elements priority_rows;
            if (priority_maximum == 0) {
                priority_rows.push_back(text("No hay prioridades activas para representar.") | dim);
            } else {
                const Color colors[]{Color::Red, Color::Yellow, Color::Cyan, Color::Green};
                for (std::size_t index = 0; index < priorities.size(); ++index) {
                    priority_rows.push_back(distribution_bar(priorities[index].first, priorities[index].second,
                                                             priority_maximum, colors[index]));
                }
            }
            auto priority_panel = window(text(" Prioridades ") | bold, vbox(std::move(priority_rows)));
            body.push_back(medium ? hbox({status_panel | flex, priority_panel | flex})
                                  : vbox({status_panel, priority_panel}));

            Elements upcoming_rows;
            if (state->data.upcoming_tasks.empty()) {
                upcoming_rows.push_back(text("No hay seguimientos próximos.") | dim);
            } else {
                for (std::size_t index = 0; index < state->data.upcoming_tasks.size(); ++index) {
                    const auto& item = state->data.upcoming_tasks[index];
                    const std::string responsible = item.task.assignee_name.value_or("Sin responsable");
                    Element row;
                    if (medium) {
                        row = hbox({text(short_date(item.task.due_date) + " ") | bold,
                                    text(shortened(item.project_alias, 10) + " ") | color(Color::Cyan),
                                    text(shortened(item.task.title, wide ? 28 : 20)) | flex,
                                    text(" " + std::string(task_priority_label(item.task.priority))) | dim,
                                    text(" " + shortened(responsible, 14)) | dim});
                    } else {
                        row = vbox({hbox({text(short_date(item.task.due_date) + " ") | bold,
                                          text(shortened(item.project_alias, 12)) | color(Color::Cyan), filler(),
                                          text(std::string(task_priority_label(item.task.priority))) | dim}),
                                    hbox({text(shortened(item.task.title, 28)) | flex,
                                          text(" " + shortened(responsible, 14)) | dim})});
                    }
                    upcoming_rows.push_back(row);
                }
            }
            auto upcoming_panel = window(text(" Próximos seguimientos ") | bold, vbox(std::move(upcoming_rows)));
            body.push_back(upcoming_panel);

            Elements attention_rows;
            if (state->data.attention_tasks.empty()) {
                attention_rows.push_back(text("No hay tareas que requieran atención.") | color(Color::Green));
            } else {
                for (std::size_t index = 0; index < state->data.attention_tasks.size(); ++index) {
                    const auto& item = state->data.attention_tasks[index];
                    const auto& task = item.summary.task;
                    Element row;
                    if (medium) {
                        row = hbox({text(attention_label(item.reason)) | bold | color(attention_color(item.reason)) |
                                        size(WIDTH, EQUAL, 17),
                                    text(shortened(item.summary.project_alias, 12)) | color(Color::Cyan) |
                                        size(WIDTH, EQUAL, 14),
                                    text(shortened(task.title, wide ? 36 : 22)) | flex,
                                    text(shortened(task.assignee_name.value_or("Sin responsable"), 16)) | dim |
                                        size(WIDTH, EQUAL, 18),
                                    text(short_date(task.due_date)) | dim});
                    } else {
                        row = vbox({hbox({text(attention_label(item.reason)) | bold |
                                              color(attention_color(item.reason)),
                                          text("  " + shortened(item.summary.project_alias, 12)) |
                                              color(Color::Cyan),
                                          filler(), text(short_date(task.due_date)) | dim}),
                                    hbox({text(shortened(task.title, 28)) | flex,
                                          text(" " + shortened(task.assignee_name.value_or("Sin responsable"), 14)) |
                                              dim})});
                    }
                    attention_rows.push_back(row);
                }
            }
            body.push_back(window(text(" Requieren atención ") | bold, vbox(std::move(attention_rows))));
        }

        body.push_back(text("Vista informativa") | dim | center);
        return vbox(std::move(body)) | vscroll_indicator | frame | flex;
    });

    return CatchEvent(component, [state](Event event) {
        if (event == Event::Custom) {
            state->reload();
            return true;
        }
        return false;
    });
}

}  // namespace modra
