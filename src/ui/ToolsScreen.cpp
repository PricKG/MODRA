#include "ui/ToolsScreen.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>
#include <spdlog/spdlog.h>

#include "application/ProjectService.h"
#include "domain/Project.h"
#include "infrastructure/system/RepositoryInspector.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

using namespace ftxui;

struct LocalProject {
    Project project;
    RepositoryStatus repository;
};

std::string repository_label(RepositoryKind kind) {
    switch (kind) {
        case RepositoryKind::git: return "Git";
        case RepositoryKind::svn: return "SVN";
        case RepositoryKind::none: return "Carpeta";
    }
    return "Carpeta";
}

std::size_t change_count(const RepositoryStatus& status) {
    return status.staged + status.modified + status.untracked;
}

std::string project_status(const LocalProject& local) {
    if (!local.project.local_path || local.project.local_path->empty()) return "Sin ruta local";
    if (!local.repository.path_exists) return "Ruta no encontrada";
    if (local.repository.kind == RepositoryKind::none) return "Sin repositorio";
    if (!local.repository.error.empty()) return "Error de consulta";
    if (local.repository.has_changes()) return std::to_string(change_count(local.repository)) + " cambio(s)";
    if (local.repository.behind > 0) return "Requiere actualizar";
    return "Limpio";
}

Color project_color(const LocalProject& local) {
    if (!local.project.local_path || !local.repository.path_exists || !local.repository.error.empty())
        return Color::Yellow;
    if (local.repository.has_changes() || local.repository.behind > 0) return Color::MagentaLight;
    if (local.repository.kind == RepositoryKind::none) return Color::GrayLight;
    return Color::Green;
}

Element metric_card(const std::string& label, std::size_t value, const std::string& detail, Color accent) {
    return window(text(" " + label + " ") | bold,
                  vbox({
                      text(std::to_string(value)) | bold | color(accent) | center,
                      text(detail) | dim | center,
                  })) |
           flex;
}

Element tool_card(const std::string& name, const ToolInformation& information) {
    const std::string detail = information.available
                                   ? (information.version.empty() ? "Versión no informada" : information.version)
                                   : "No está disponible en PATH";
    return window(text(" " + name + " ") | bold,
                  vbox({
                      text(information.available ? "Disponible" : "No disponible") | bold |
                          color(information.available ? Color::Green : Color::Yellow),
                      text(detail) | dim,
                  })) |
           flex;
}

struct ToolsState {
    explicit ToolsState(ProjectService& projects_value) : projects(projects_value) {
        reload();
    }

    void rebuild_visible() {
        visible.clear();
        for (std::size_t index = 0; index < local_projects.size(); ++index) {
            if (!attention_only || local_projects[index].repository.needs_attention() ||
                !local_projects[index].project.local_path) {
                visible.push_back(index);
            }
        }
        selected = visible.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(visible.size()) - 1);
    }

    void reload() {
        try {
            git = inspect_tool("git");
            svn = inspect_tool("svn");
            local_projects.clear();
            for (auto project : projects.list_active()) {
                RepositoryStatus repository;
                if (project.local_path && !project.local_path->empty()) {
                    repository = inspect_repository(std::filesystem::path(*project.local_path),
                                                    git.available, svn.available);
                }
                local_projects.push_back({std::move(project), std::move(repository)});
            }
            rebuild_visible();
            error.clear();
        } catch (const std::exception& exception) {
            error = "No se pudo actualizar la información de herramientas.";
            spdlog::error("Tools screen reload error: {}", exception.what());
        }
    }

    void toggle_attention() {
        attention_only = !attention_only;
        selected = 0;
        rebuild_visible();
    }

    ProjectService& projects;
    ToolInformation git;
    ToolInformation svn;
    std::vector<LocalProject> local_projects;
    std::vector<std::size_t> visible;
    int selected = 0;
    bool attention_only = false;
    std::string error;
};

}  // namespace

ftxui::Component create_tools_screen(ProjectService& projects) {
    auto state = std::make_shared<ToolsState>(projects);
    auto component = Renderer([state] {
        const int terminal_width = Terminal::Size().dimx;
        const bool wide = terminal_width >= 105;

        std::size_t repositories = 0;
        std::size_t changed = 0;
        std::size_t attention = 0;
        for (const auto& local : state->local_projects) {
            if (local.repository.kind != RepositoryKind::none) ++repositories;
            if (local.repository.has_changes()) ++changed;
            if (local.repository.needs_attention() || !local.project.local_path) ++attention;
        }

        std::array<Element, 4> metrics{
            metric_card("Proyectos", state->local_projects.size(), "activos", Color::Cyan),
            metric_card("Repositorios", repositories, "detectados", Color::BlueLight),
            metric_card("Con cambios", changed, "sin confirmar", changed == 0 ? Color::Green : Color::MagentaLight),
            metric_card("Atención", attention, "para revisar", attention == 0 ? Color::Green : Color::Yellow),
        };
        auto metric_layout = wide
                                 ? hbox({metrics[0], metrics[1], metrics[2], metrics[3]})
                                 : vbox({hbox({metrics[0], metrics[1]}), hbox({metrics[2], metrics[3]})});

        Elements body{
            hbox({
                text(" MODRA · Herramientas ") | bold | color(Color::Cyan),
                filler(),
                text(state->attention_only ? "Vista: requieren atención" : "Vista: todos") | dim,
            }),
            separator(),
            metric_layout,
            hbox({tool_card("Git", state->git), tool_card("SVN", state->svn)}),
        };
        if (!state->error.empty()) body.push_back(text(state->error) | bold | color(Color::Red));

        Elements project_rows;
        for (std::size_t visible_index = 0; visible_index < state->visible.size(); ++visible_index) {
            const auto& local = state->local_projects[state->visible[visible_index]];
            const bool selected = static_cast<int>(visible_index) == state->selected;
            const std::string type = local.repository.path_exists
                                         ? repository_label(local.repository.kind)
                                         : "—";
            const std::string branch = local.repository.branch.empty() ? "—" : local.repository.branch;
            Elements columns{
                text(selected ? "> " : "  ") | size(WIDTH, EQUAL, 2),
                text(local.project.name) | bold | size(WIDTH, EQUAL, wide ? 25 : 20),
                text(type) | size(WIDTH, EQUAL, 10),
                text(project_status(local)) | color(project_color(local)) | size(WIDTH, EQUAL, 21),
            };
            if (wide) {
                columns.push_back(text(branch) | color(Color::Cyan) | size(WIDTH, EQUAL, 18));
                columns.push_back(text(local.project.local_path.value_or("—")) | dim | flex);
            }
            auto row = hbox(std::move(columns));
            if (selected) row = row | inverted | focus;
            project_rows.push_back(row);
        }

        if (project_rows.empty()) {
            const std::string title = state->local_projects.empty()
                                          ? "No hay proyectos activos."
                                          : "No hay proyectos que requieran atención.";
            project_rows.push_back(text(title) | center);
            if (state->local_projects.empty()) {
                project_rows.push_back(
                    text("Creá un proyecto y agregale una ruta local desde Proyectos.") | dim | center);
            } else {
                project_rows.push_back(text("Presioná v para volver a mostrar todos.") | dim | center);
            }
        }
        body.push_back(window(text(" Proyectos locales ") | bold,
                              vbox(std::move(project_rows)) | frame) |
                       flex);

        if (!state->visible.empty()) {
            const auto& local =
                state->local_projects[state->visible[static_cast<std::size_t>(state->selected)]];
            Elements details{
                hbox({text("Proyecto: ") | bold, text(local.project.name), filler(),
                      text(project_status(local)) | bold | color(project_color(local))}),
                hbox({text("Ruta:     ") | bold,
                      text(local.project.local_path.value_or("Sin configurar")) | flex}),
            };
            if (!local.project.local_path || local.project.local_path->empty()) {
                details.push_back(
                    text("Configurá una ruta local desde Proyectos para habilitar el diagnóstico.") |
                    color(Color::Yellow));
            } else if (!local.repository.path_exists) {
                details.push_back(
                    text("La ruta configurada no existe o no es un directorio.") | color(Color::Yellow));
            } else if (local.repository.kind == RepositoryKind::none) {
                details.push_back(text("La carpeta no contiene metadatos .git ni .svn.") | dim);
            } else {
                details.push_back(hbox({
                    text("Repositorio: ") | bold,
                    text(repository_label(local.repository.kind)) | color(Color::Cyan),
                    text("   Rama: ") | bold,
                    text(local.repository.branch.empty() ? "No aplica" : local.repository.branch) |
                        color(Color::Cyan),
                }));
                details.push_back(hbox({
                    text("Preparados: ") | bold,
                    text(std::to_string(local.repository.staged)),
                    text("   Modificados: ") | bold,
                    text(std::to_string(local.repository.modified)),
                    text("   Sin rastrear: ") | bold,
                    text(std::to_string(local.repository.untracked)),
                    text("   Adelante/Atrás: ") | bold,
                    text(std::to_string(local.repository.ahead) + "/" +
                         std::to_string(local.repository.behind)),
                }));
                if (!local.repository.error.empty()) {
                    details.push_back(paragraph(local.repository.error) | color(Color::Yellow));
                }
            }
            body.push_back(window(text(" Diagnóstico ") | bold, vbox(std::move(details))));
        }

        body.push_back(
            text("↑/↓ seleccionar · v todos/atención · r refrescar · Esc volver · ? ayuda") | dim | center);
        return vbox(std::move(body)) | vscroll_indicator | frame | flex;
    });

    return CatchEvent(component, [state](Event event) {
        if (event == Event::Custom || shortcut(event, 'r')) {
            state->reload();
            return true;
        }
        if (shortcut(event, 'v')) {
            state->toggle_attention();
            return true;
        }
        if (!state->visible.empty() && event == Event::ArrowDown) {
            state->selected = std::min(state->selected + 1, static_cast<int>(state->visible.size()) - 1);
            return true;
        }
        if (!state->visible.empty() && event == Event::ArrowUp) {
            state->selected = std::max(state->selected - 1, 0);
            return true;
        }
        return false;
    });
}

}  // namespace modra
