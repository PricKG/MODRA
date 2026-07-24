#include "ui/DashboardScreen.h"

#include <algorithm>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <spdlog/spdlog.h>

#include "application/DashboardService.h"
#include "application/TaskService.h"
#include "domain/Note.h"
#include "ui/KeyEvent.h"

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

std::string short_timestamp(const std::string& timestamp) {
    if (timestamp.size() < 10) return timestamp;
    return timestamp.substr(8, 2) + "/" + timestamp.substr(5, 2) + "/" + timestamp.substr(0, 4);
}

std::string note_excerpt(const std::string& content, std::size_t limit) {
    std::string result;
    result.reserve(std::min(content.size(), limit));
    bool pending_space = false;
    bool line_start = true;
    bool skipping_heading = false;
    for (char character : content) {
        if (skipping_heading) {
            if (character == '\r' || character == '\n') {
                skipping_heading = false;
                line_start = true;
            }
            continue;
        }
        if (line_start && character == '#') {
            skipping_heading = true;
            continue;
        }
        if (line_start && (character == '-' || character == '*' || character == '>')) {
            pending_space = !result.empty();
            continue;
        }
        if (character == '\r' || character == '\n' || character == '\t' || character == ' ') {
            pending_space = !result.empty();
            line_start = character == '\r' || character == '\n';
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
            pending_space = false;
        }
        result.push_back(character);
        line_start = false;
        if (result.size() >= limit) {
            return shortened(result, limit);
        }
    }
    return result.empty() ? "Sin contenido visible." : result;
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
    DashboardState(DashboardService& service_value,
                   std::function<void(std::int64_t)> open_note,
                   std::function<void()> open_all_favorites)
        : service(service_value), on_note(std::move(open_note)), on_all_favorites(std::move(open_all_favorites)) {
        reload();
    }

    void reload() {
        try {
            today = TaskService::current_local_date();
            data = service.load(today);
            favorite_selected = data.favorite_notes.empty()
                                    ? 0
                                    : std::clamp(favorite_selected, 0, static_cast<int>(data.favorite_notes.size()));
            error.clear();
        } catch (const std::exception& exception) {
            error = "No se pudo cargar el dashboard. Volvé a ingresar para reintentar.";
            spdlog::error("Dashboard load error: {}", exception.what());
        }
    }

    DashboardService& service;
    DashboardData data;
    std::function<void(std::int64_t)> on_note;
    std::function<void()> on_all_favorites;
    int favorite_selected = 0;
    std::string today;
    std::string error;
};

}  // namespace

ftxui::Component create_dashboard_screen(DashboardService& dashboard,
                                         std::function<void(std::int64_t)> on_note,
                                         std::function<void()> on_all_favorites) {
    using namespace ftxui;
    auto state = std::make_shared<DashboardState>(dashboard, std::move(on_note), std::move(on_all_favorites));
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

        Elements favorite_rows;
        if (state->data.favorite_notes.empty()) {
            favorite_rows.push_back(text(medium ? "No hay notas favoritas." : "Sin notas favoritas.") | dim);
            if (medium) favorite_rows.push_back(text("Marcá una nota con f para verla acá.") | dim);
        } else {
            for (std::size_t index = 0; index < state->data.favorite_notes.size(); ++index) {
                const auto& summary = state->data.favorite_notes[index];
                const bool selected = state->favorite_selected == static_cast<int>(index);
                const std::string project = summary.project_name.value_or(summary.project_alias.value_or("Global"));
                std::string relation = std::string(note_type_label(summary.note.type)) + " · ";
                if (summary.note.task_id) {
                    relation += "Proyecto: " + project + " · Tarea: " + summary.task_title.value_or("No disponible");
                } else if (summary.note.project_id) {
                    relation += "Proyecto: " + project;
                } else {
                    relation += "Global";
                }
                if (summary.project_archived) relation += " · Proyecto archivado";
                if (summary.task_archived) relation += " · Tarea archivada";
                const std::string excerpt = note_excerpt(summary.note.content, wide ? 110 : medium ? 72 : 32);

                auto title = hbox({text(selected ? "> * " : "  * ") | color(Color::Yellow),
                                   text(shortened(summary.note.title, wide ? 54 : medium ? 38 : 22)) | bold | flex});
                auto relation_row = text("    " + shortened(relation, wide ? 100 : medium ? 64 : 26)) | dim;
                auto excerpt_row = text("    " + excerpt);
                auto updated = text("    Actualizada: " + short_timestamp(summary.note.updated_at)) | dim;
                favorite_rows.push_back(
                    (medium ? vbox({hbox({title | flex, updated}), excerpt_row, relation_row})
                            : vbox({title, excerpt_row, relation_row, updated})) |
                    (selected ? color(Color::Cyan) : color(Color::Default)));
            }

            const bool all_selected = state->favorite_selected == static_cast<int>(state->data.favorite_notes.size());
            const std::string more = state->data.additional_favorite_count > 0
                                         ? "+ " + std::to_string(state->data.additional_favorite_count) + " favoritas más"
                                         : "Ver todas las favoritas";
            favorite_rows.push_back(text(std::string(all_selected ? "> " : "  ") + more) |
                                    (all_selected ? color(Color::Cyan) : color(Color::Default)) | bold);
        }
        body.push_back(window(text(" Notas favoritas ") | bold, vbox(std::move(favorite_rows))));

        body.push_back(text("Las métricas y tareas son informativas · Tab/→ entra a Notas favoritas") | dim | center);
        return vbox(std::move(body)) | vscroll_indicator | frame | flex;
    });

    return CatchEvent(component, [state](Event event) {
        if (event == Event::Custom) {
            state->reload();
            return true;
        }
        if (shortcut(event, 'r')) {
            state->reload();
            return true;
        }
        if (!state->data.favorite_notes.empty() && (event == Event::ArrowUp || event == Event::ArrowDown)) {
            const int last = static_cast<int>(state->data.favorite_notes.size());
            state->favorite_selected = std::clamp(
                state->favorite_selected + (event == Event::ArrowDown ? 1 : -1), 0, last);
            return true;
        }
        if (!state->data.favorite_notes.empty() && event == Event::Return) {
            if (state->favorite_selected < static_cast<int>(state->data.favorite_notes.size())) {
                state->on_note(state->data.favorite_notes[static_cast<std::size_t>(state->favorite_selected)].note.id);
            } else {
                state->on_all_favorites();
            }
            return true;
        }
        return false;
    });
}

}  // namespace modra
