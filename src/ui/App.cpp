#include "ui/App.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include "application/AppInfo.h"
#include "application/DashboardService.h"
#include "application/NoteService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "domain/Project.h"
#include "ui/ConfigurationScreen.h"
#include "ui/DashboardScreen.h"
#include "ui/KnowledgeScreen.h"
#include "ui/KeyEvent.h"
#include "ui/ProjectScreen.h"
#include "ui/TaskScreen.h"
#include "ui/ToolsScreen.h"
#include "ui/WorkScreen.h"

namespace modra {

void run_ui(ProjectService& projects,
            TaskService& tasks,
            DashboardService& dashboard,
            NoteService& notes,
            DataPaths paths,
            std::string sqlite_version) {
    using namespace ftxui;

    const std::vector<std::string> sections{
        "Dashboard", "Proyectos", "Mi trabajo", "Conocimiento", "Herramientas", "Configuración"};
    const std::vector<std::string> descriptions{
        "Resumen general y elementos que requieren atención.",
        "Gestión persistente de proyectos y sus tareas.",
        "Tarjetas personales, revisiones, bloqueos y próximos seguimientos.",
        "Notas personales, soluciones y conocimiento reutilizable.",
        "Estado de herramientas y repositorios locales.",
        "Rutas locales, editor externo y entorno detectado.",
    };

    MainNavigationState navigation;
    bool help_visible = false;
    bool projects_visible = false;
    bool tasks_visible = false;
    bool work_visible = false;
    bool knowledge_visible = false;
    bool exit_confirmation = false;
    bool project_return_to_work = false;
    bool project_return_to_knowledge = false;
    bool work_return_to_knowledge = false;
    bool knowledge_return_to_project = false;
    bool knowledge_return_to_task = false;
    bool knowledge_return_to_work = false;
    bool knowledge_return_to_dashboard = false;
    int root_tab = 0;
    std::optional<Project> task_project;
    std::optional<Project> requested_project;
    std::optional<WorkOpenRequest> requested_work;
    std::optional<KnowledgeRequest> requested_knowledge;
    auto menu = Menu(&sections, &navigation.selected);
    auto screen = ScreenInteractive::Fullscreen();

    ftxui::Component task_screen;
    ftxui::Component work_screen;
    ftxui::Component dashboard_screen;
    ftxui::Component knowledge_screen;
    ftxui::Component configuration_screen =
        create_configuration_screen(std::move(paths), std::move(sqlite_version));
    auto project_screen = create_project_screen(
        projects,
        notes,
        [&] { return std::exchange(requested_project, std::nullopt); },
        [&](const Project& project) {
            task_project = project;
            projects_visible = false;
            tasks_visible = true;
            navigation.focus = MainFocus::content;
            root_tab = 2;
            task_screen->TakeFocus();
        },
        [&](KnowledgeRequest request) {
            requested_knowledge = std::move(request);
            knowledge_return_to_project = true;
            projects_visible = false;
            knowledge_visible = true;
            navigation.selected = 3;
            navigation.active = 3;
            navigation.focus = MainFocus::content;
            root_tab = 4;
            knowledge_screen->TakeFocus();
        },
        [&] {
            projects_visible = false;
            if (project_return_to_work) {
                project_return_to_work = false;
                work_visible = true;
                navigation.selected = 2;
                navigation.active = 2;
                root_tab = 3;
                work_screen->TakeFocus();
            } else if (project_return_to_knowledge) {
                project_return_to_knowledge = false;
                knowledge_visible = true;
                navigation.selected = 3;
                navigation.active = 3;
                root_tab = 4;
                knowledge_screen->TakeFocus();
            } else {
                navigation.selected = 1;
                navigation.active = 1;
                navigation.focus = MainFocus::menu;
                root_tab = 0;
                menu->TakeFocus();
            }
        });
    task_screen = create_task_screen(
        tasks,
        notes,
        [&] { return task_project; },
        [&](KnowledgeRequest request) {
            requested_knowledge = std::move(request);
            knowledge_return_to_task = true;
            tasks_visible = false;
            knowledge_visible = true;
            navigation.selected = 3;
            navigation.active = 3;
            navigation.focus = MainFocus::content;
            root_tab = 4;
            knowledge_screen->TakeFocus();
        },
        [&] {
            tasks_visible = false;
            projects_visible = true;
            navigation.selected = 1;
            navigation.active = 1;
            navigation.focus = MainFocus::content;
            root_tab = 1;
            project_screen->TakeFocus();
        });
    work_screen = create_work_screen(
        tasks,
        projects,
        notes,
        [&](std::int64_t project_id) {
            requested_project = projects.find_by_id(project_id);
            project_return_to_work = true;
            work_visible = false;
            projects_visible = true;
            navigation.selected = 1;
            navigation.active = 1;
            navigation.focus = MainFocus::content;
            root_tab = 1;
            project_screen->TakeFocus();
        },
        [&](KnowledgeRequest request) {
            requested_knowledge = std::move(request);
            knowledge_return_to_work = true;
            work_visible = false;
            knowledge_visible = true;
            navigation.selected = 3;
            navigation.active = 3;
            navigation.focus = MainFocus::content;
            root_tab = 4;
            knowledge_screen->TakeFocus();
        },
        [&] { return std::exchange(requested_work, std::nullopt); },
        [&] {
            work_visible = false;
            if (work_return_to_knowledge) {
                work_return_to_knowledge = false;
                knowledge_visible = true;
                navigation.selected = 3;
                navigation.active = 3;
                root_tab = 4;
                knowledge_screen->TakeFocus();
            } else {
                navigation.selected = 2;
                navigation.active = 2;
                navigation.focus = MainFocus::menu;
                root_tab = 0;
                menu->TakeFocus();
            }
        });
    knowledge_screen = create_knowledge_screen(
        notes,
        projects,
        tasks,
        [&] { return std::exchange(requested_knowledge, std::nullopt); },
        [&](std::int64_t project_id) {
            requested_project = projects.find_by_id(project_id);
            project_return_to_knowledge = true;
            knowledge_visible = false;
            projects_visible = true;
            navigation.selected = 1;
            navigation.active = 1;
            navigation.focus = MainFocus::content;
            root_tab = 1;
            project_screen->TakeFocus();
        },
        [&](std::int64_t task_id) {
            requested_work = WorkOpenRequest{TaskQuickView::all, task_id};
            work_return_to_knowledge = true;
            knowledge_visible = false;
            work_visible = true;
            navigation.selected = 2;
            navigation.active = 2;
            navigation.focus = MainFocus::content;
            root_tab = 3;
            work_screen->TakeFocus();
        },
        [&] {
            knowledge_visible = false;
            if (knowledge_return_to_dashboard) {
                knowledge_return_to_dashboard = false;
                navigation.selected = 0;
                navigation.active = 0;
                navigation.focus = MainFocus::content;
                root_tab = 5;
                dashboard_screen->OnEvent(Event::Custom);
                dashboard_screen->TakeFocus();
            } else if (knowledge_return_to_work) {
                knowledge_return_to_work = false;
                work_visible = true;
                navigation.selected = 2;
                navigation.active = 2;
                root_tab = 3;
                work_screen->OnEvent(Event::Custom);
                work_screen->TakeFocus();
            } else if (knowledge_return_to_task) {
                knowledge_return_to_task = false;
                tasks_visible = true;
                navigation.selected = 1;
                navigation.active = 1;
                root_tab = 2;
                task_screen->OnEvent(Event::Custom);
                task_screen->TakeFocus();
            } else if (knowledge_return_to_project) {
                knowledge_return_to_project = false;
                projects_visible = true;
                navigation.selected = 1;
                navigation.active = 1;
                root_tab = 1;
                project_screen->OnEvent(Event::Custom);
                project_screen->TakeFocus();
            } else {
                navigation.selected = 3;
                navigation.active = 3;
                navigation.focus = MainFocus::menu;
                root_tab = 0;
                menu->TakeFocus();
            }
        },
        [&](const std::function<void()>& action) { screen.WithRestoredIO(action)(); });
    auto open_dashboard_knowledge = [&](KnowledgeRequest request) {
        requested_knowledge = std::move(request);
        knowledge_return_to_dashboard = true;
        knowledge_visible = true;
        navigation.selected = 3;
        navigation.active = 3;
        navigation.focus = MainFocus::content;
        root_tab = 4;
        knowledge_screen->OnEvent(Event::Custom);
        knowledge_screen->TakeFocus();
    };
    dashboard_screen = create_dashboard_screen(
        dashboard,
        [&](std::int64_t note_id) {
            KnowledgeRequest request;
            request.note_id = note_id;
            open_dashboard_knowledge(std::move(request));
        },
        [&] {
            KnowledgeRequest request;
            request.favorites = true;
            open_dashboard_knowledge(std::move(request));
        });
    auto tools_screen = create_tools_screen(projects);
    auto root = Container::Tab(
        {menu, project_screen, task_screen, work_screen, knowledge_screen, dashboard_screen, configuration_screen,
         tools_screen},
        &root_tab);

    auto activate_section = [&] {
        projects_visible = navigation.active == 1;
        tasks_visible = false;
        work_visible = navigation.active == 2;
        knowledge_visible = navigation.active == 3;
        project_return_to_work = false;
        project_return_to_knowledge = false;
        work_return_to_knowledge = false;
        knowledge_return_to_project = false;
        knowledge_return_to_task = false;
        knowledge_return_to_work = false;
        knowledge_return_to_dashboard = false;
        root_tab = 0;
        menu->TakeFocus();

        if (navigation.active == 0) dashboard_screen->OnEvent(Event::Custom);
        else if (navigation.active == 1) project_screen->OnEvent(Event::Custom);
        else if (navigation.active == 2) work_screen->OnEvent(Event::Custom);
        else if (navigation.active == 3) knowledge_screen->OnEvent(Event::Custom);
        else if (navigation.active == 4) tools_screen->OnEvent(Event::Custom);
        else if (navigation.active == 5) configuration_screen->OnEvent(Event::Custom);
    };

    auto focus_content = [&] {
        if (Terminal::Size().dimx < 52) return;
        navigation.enter_content();
        if (navigation.active == 0) {
            root_tab = 5;
            dashboard_screen->TakeFocus();
        } else if (navigation.active == 1) {
            projects_visible = true;
            root_tab = 1;
            project_screen->TakeFocus();
        } else if (navigation.active == 2) {
            work_visible = true;
            root_tab = 3;
            work_screen->TakeFocus();
        } else if (navigation.active == 3) {
            knowledge_visible = true;
            root_tab = 4;
            knowledge_screen->TakeFocus();
        } else if (navigation.active == 4) {
            root_tab = 7;
            tools_screen->TakeFocus();
        } else if (navigation.active == 5) {
            root_tab = 6;
            configuration_screen->TakeFocus();
        } else {
            root_tab = 0;
        }
    };

    auto layout = Renderer(root, [&] {
        auto header = hbox({text(" MODRA ") | bold | color(Color::Cyan), filler(),
                            text("organización local") | dim}) |
                      border;
        const int terminal_width = Terminal::Size().dimx;
        const int menu_width = terminal_width < 80 ? 18 : 24;
        auto sidebar = vbox({text(" NAVEGACIÓN ") | bold, separator(), menu->Render() | frame}) |
                       size(WIDTH, EQUAL, menu_width) | border;

        Element content;
        if (terminal_width < 52) {
            content = vbox({text(" Terminal demasiado estrecha ") | bold | color(Color::Yellow), separator(),
                            paragraph("Ampliá la terminal para mostrar el contenido de MODRA."), filler()}) |
                      border | flex;
        } else if (tasks_visible) {
            content = task_screen->Render() | flex;
        } else if (projects_visible || navigation.active == 1) {
            content = project_screen->Render() | flex;
        } else if (work_visible || navigation.active == 2) {
            content = work_screen->Render() | flex;
        } else if (knowledge_visible || navigation.active == 3) {
            content = knowledge_screen->Render() | flex;
        } else if (navigation.active == 0) {
            content = dashboard_screen->Render() | flex | border;
        } else if (navigation.active == 4) {
            content = tools_screen->Render() | flex | border;
        } else if (navigation.active == 5) {
            content = configuration_screen->Render() | flex | border;
        } else {
            content = vbox({text(" " + sections[static_cast<std::size_t>(navigation.active)]) | bold |
                                 color(Color::Cyan),
                            separator(), text(" Esta sección todavía no está implementada."),
                            text(" " + descriptions[static_cast<std::size_t>(navigation.active)]) | dim,
                            filler(), text(" MODRA " + std::string(application_version()) + " ") | dim}) |
                      flex | border;
        }

        auto footer = navigation.focus == MainFocus::menu
                          ? text(" [↑/↓] Sección  [Enter/Tab/→] Entrar  [?] Ayuda  [q] Salir ") | border
                          : navigation.active == 0
                                ? text(" [↑/↓] Favorita  [Enter] Abrir  [r] Actualizar  [←/Esc] Menú  [?] Ayuda ") |
                                      border
                            : navigation.active == 5
                                ? text(" [Enter] Cambiar editor  [Ctrl+S] Confirmar  [Ctrl+R] Refrescar  [←/Esc] Menú  [?] Ayuda ") |
                                      border
                            : navigation.active == 4
                                ? text(" [↑/↓] Proyecto  [v] Todos/atención  [r] Refrescar  [←/Esc] Menú  [?] Ayuda ") |
                                      border
                                : text(" [←/Esc] Menú  [Enter] Abrir  [r] Actualizar  [?] Ayuda  [q] Volver ") |
                                      border;

        auto base = vbox({header, hbox({sidebar, content}) | flex, footer});
        if (exit_confirmation) {
            auto confirmation = window(
                                    text(" CONFIRMAR SALIDA ") | bold | color(Color::Yellow),
                                    vbox({text("¿Querés cerrar MODRA?") | bold | center,
                                          text("Presioná q nuevamente para confirmar.") | center,
                                          text("Esc o n cancela y vuelve a la navegación.") | dim | center})) |
                                size(WIDTH, EQUAL, 58);
            return dbox({base, confirmation | clear_under | center});
        }
        if (!help_visible) return base;

        auto help = window(
                        text(" GUÍA DE USO ") | bold | color(Color::Cyan),
                        vbox({text("MODRA se maneja completamente con el teclado."), separator(),
                              text("Cómo navegar") | bold,
                              text("1. Usá ↑/↓: el panel cambia inmediatamente."),
                              text("2. Usá Tab o → para entrar al contenido."),
                              text("3. Usá Esc para retroceder; q vuelve o solicita salir en la raíz."), separator(),
                              text("Teclas disponibles") | bold,
                              hbox({text("↑") | bold | size(WIDTH, EQUAL, 14), text("Selección anterior")}),
                              hbox({text("↓") | bold | size(WIDTH, EQUAL, 14), text("Selección siguiente")}),
                              hbox({text("Enter") | bold | size(WIDTH, EQUAL, 14), text("Entrar en la sección")}),
                              hbox({text("Tab / →") | bold | size(WIDTH, EQUAL, 14), text("Entrar en la sección")}),
                              hbox({text("?") | bold | size(WIDTH, EQUAL, 14), text("Abrir o cerrar esta ayuda")}),
                              hbox({text("Esc") | bold | size(WIDTH, EQUAL, 14), text("Retroceder o cerrar ayuda")}),
                              hbox({text("q") | bold | size(WIDTH, EQUAL, 14),
                                    text("Volver; en la raíz solicita confirmación para salir")}),
                              separator(), text("Presioná ?, Esc o q para cerrar") | dim | center})) |
                    size(WIDTH, EQUAL, 62);
        return dbox({base, help | clear_under | center});
    });

    auto app = CatchEvent(layout, [&](Event event) {
        if (exit_confirmation) {
            if (shortcut(event, 'q')) {
                screen.ExitLoopClosure()();
            } else if (event == Event::Escape || shortcut(event, 'n')) {
                exit_confirmation = false;
            }
            return true;
        }

        if (help_visible) {
            if (event == Event::Character('?') || shortcut(event, 'q') || event == Event::Escape) {
                help_visible = false;
            }
            return true;
        }
        if (navigation.focus == MainFocus::content) {
            if (navigation.active == 0) {
                if (event == Event::Character('?')) {
                    help_visible = true;
                } else if (event == Event::Escape || event == Event::ArrowLeft || shortcut(event, 'q')) {
                    navigation.return_to_menu();
                    root_tab = 0;
                    menu->TakeFocus();
                    return true;
                }
                return false;
            }
            if (navigation.active >= 4) {
                if (navigation.active == 4 && tools_screen->OnEvent(event)) {
                    return true;
                } else if (navigation.active == 5 && configuration_screen->OnEvent(event)) {
                    return true;
                } else if (event == Event::Character('?')) {
                    help_visible = true;
                } else if (event == Event::Escape || event == Event::ArrowLeft || shortcut(event, 'q')) {
                    navigation.return_to_menu();
                    root_tab = 0;
                    menu->TakeFocus();
                }
                return true;
            }
            return false;
        }
        if (event == Event::Character('?')) {
            help_visible = true;
            return true;
        }
        if (event == Event::Escape) {
            return true;
        }
        if (shortcut(event, 'q')) {
            exit_confirmation = true;
            return true;
        }
        if (event == Event::ArrowUp || event == Event::ArrowDown) {
            const int previous_selection = navigation.selected;
            menu->OnEvent(event);
            if (navigation.activate_selected(previous_selection)) activate_section();
            return true;
        }
        if (event == Event::Return || event == Event::Tab || event == Event::ArrowRight) {
            focus_content();
            return true;
        }
        return true;
    });

    screen.Loop(app);
}

}  // namespace modra
