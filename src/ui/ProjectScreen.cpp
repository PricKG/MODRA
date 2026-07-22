#include "ui/ProjectScreen.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "application/ProjectService.h"
#include "application/NoteService.h"
#include "domain/Project.h"
#include "ui/KnowledgeScreen.h"
#include "ui/KeyEvent.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

enum class ProjectView {
    list,
    detail,
    form,
    archive_confirmation,
};

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

std::string generated_alias(const std::string& name) {
    std::string alias;
    bool previous_separator = false;
    for (unsigned char character : name) {
        if (std::isalnum(character)) {
            alias.push_back(static_cast<char>(std::tolower(character)));
            previous_separator = false;
        } else if (character == '_') {
            alias.push_back('_');
            previous_separator = false;
        } else if ((std::isspace(character) || character == '-') && !alias.empty() && !previous_separator) {
            alias.push_back('-');
            previous_separator = true;
        }
    }
    if (!alias.empty() && alias.back() == '-') {
        alias.pop_back();
    }
    return alias;
}

std::string shortened(const std::string& value, std::size_t width) {
    if (value.size() <= width) {
        return value;
    }
    if (width <= 3) {
        return value.substr(0, width);
    }
    return value.substr(0, width - 3) + "...";
}

std::optional<std::string> optional_value(const std::string& value) {
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

struct ProjectScreenState {
    ProjectScreenState(ProjectService& project_service,
                       NoteService& note_service,
                       std::function<std::optional<Project>()> requested,
                       std::function<void(const Project&)> open_tasks,
                       std::function<void(KnowledgeRequest)> open_knowledge,
                       std::function<void()> back)
        : projects(project_service), notes(note_service), requested_project(std::move(requested)),
          on_tasks(std::move(open_tasks)), on_knowledge(std::move(open_knowledge)), on_back(std::move(back)) {}

    void sync_requested_project() {
        if (auto requested = requested_project()) {
            current_project = *requested;
            showing_archived = requested->status == ProjectStatus::archived;
            view = ProjectView::detail;
            load_related_notes(requested->id);
            message.clear();
            error.clear();
        }
    }

    void apply_filter() {
        visible_projects.clear();
        const std::string normalized_query = lowercase(search_query);
        for (const auto& project : loaded_projects) {
            if (normalized_query.empty() || lowercase(project.name).find(normalized_query) != std::string::npos ||
                lowercase(project.alias).find(normalized_query) != std::string::npos) {
                visible_projects.push_back(project);
            }
        }
        if (visible_projects.empty()) {
            selected = 0;
        } else {
            selected = std::clamp(selected, 0, static_cast<int>(visible_projects.size()) - 1);
        }
    }

    void reload() {
        try {
            loaded_projects = showing_archived ? projects.list_archived() : projects.list_active();
            apply_filter();
            error.clear();
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void load_related_notes(std::int64_t project_id) {
        try {
            related_notes = notes.list_by_project(project_id);
            if (related_notes.size() > 5) related_notes.resize(5);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    const Project* selected_project() const {
        if (visible_projects.empty() || selected < 0 || selected >= static_cast<int>(visible_projects.size())) {
            return nullptr;
        }
        return &visible_projects[static_cast<std::size_t>(selected)];
    }

    void begin_create() {
        editing = false;
        editing_id = 0;
        form_name.clear();
        form_alias.clear();
        form_description.clear();
        form_start_date.clear();
        form_target_date.clear();
        form_local_path.clear();
        status_entries = {"Planificado"};
        status_selected = 0;
        alias_edited = false;
        error.clear();
        view = ProjectView::form;
    }

    void begin_edit(const Project& project) {
        editing = true;
        editing_id = project.id;
        form_name = project.name;
        form_alias = project.alias;
        form_description = project.description.value_or("");
        form_start_date = project.start_date.value_or("");
        form_target_date = project.target_date.value_or("");
        form_local_path = project.local_path.value_or("");
        alias_edited = true;
        error.clear();
        if (project.status == ProjectStatus::archived) {
            status_entries = {"Archivado"};
            status_selected = 0;
        } else {
            status_entries = {"Planificado", "Activo", "En pausa", "Finalizado"};
            switch (project.status) {
                case ProjectStatus::planned:
                    status_selected = 0;
                    break;
                case ProjectStatus::active:
                    status_selected = 1;
                    break;
                case ProjectStatus::paused:
                    status_selected = 2;
                    break;
                case ProjectStatus::completed:
                    status_selected = 3;
                    break;
                case ProjectStatus::archived:
                    status_selected = 0;
                    break;
            }
        }
        view = ProjectView::form;
    }

    void save() {
        ProjectInput input{
            form_name,
            form_alias,
            optional_value(form_description),
            ProjectStatus::planned,
            optional_value(form_start_date),
            optional_value(form_target_date),
            optional_value(form_local_path),
        };
        if (editing) {
            if (status_entries.size() == 1 && status_entries.front() == "Archivado") {
                input.status = ProjectStatus::archived;
            } else {
                constexpr ProjectStatus editable_statuses[]{
                    ProjectStatus::planned,
                    ProjectStatus::active,
                    ProjectStatus::paused,
                    ProjectStatus::completed,
                };
                input.status = editable_statuses[static_cast<std::size_t>(status_selected)];
            }
        }

        try {
            current_project = editing ? projects.update(editing_id, input) : projects.create(input);
            message = editing ? "Proyecto actualizado." : "Proyecto creado.";
            error.clear();
            reload();
            view = ProjectView::detail;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void request_archive(const Project& project) {
        current_project = project;
        error.clear();
        archive_return_view = view;
        view = ProjectView::archive_confirmation;
    }

    void confirm_archive() {
        if (!current_project) {
            view = ProjectView::list;
            return;
        }
        try {
            projects.archive(current_project->id);
            message = "Proyecto archivado.";
            error.clear();
            reload();
            current_project.reset();
            view = ProjectView::list;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    ProjectService& projects;
    NoteService& notes;
    std::function<std::optional<Project>()> requested_project;
    std::function<void(const Project&)> on_tasks;
    std::function<void(KnowledgeRequest)> on_knowledge;
    std::function<void()> on_back;
    ProjectView view = ProjectView::list;
    ProjectView archive_return_view = ProjectView::list;
    bool showing_archived = false;
    bool searching = false;
    bool help_visible = false;
    bool editing = false;
    bool alias_edited = false;
    int control_tab = 0;
    int selected = 0;
    std::int64_t editing_id = 0;
    std::vector<Project> loaded_projects;
    std::vector<Project> visible_projects;
    std::vector<NoteSummary> related_notes;
    std::optional<Project> current_project;
    std::string search_query;
    std::string error;
    std::string message;
    std::string form_name;
    std::string form_alias;
    std::string form_description;
    std::string form_start_date;
    std::string form_target_date;
    std::string form_local_path;
    std::vector<std::string> status_entries{"Planificado"};
    int status_selected = 0;
};

ftxui::Element render_help() {
    using namespace ftxui;
    return window(
               text(" AYUDA · PROYECTOS ") | bold | color(Color::Cyan),
               vbox({
                   hbox({text("↑") | bold | size(WIDTH, EQUAL, 12), text("Selección anterior")}),
                   hbox({text("↓") | bold | size(WIDTH, EQUAL, 12), text("Selección siguiente")}),
                   hbox({text("Enter") | bold | size(WIDTH, EQUAL, 12), text("Abrir detalle")}),
                   hbox({text("n") | bold | size(WIDTH, EQUAL, 12), text("Crear proyecto")}),
                   hbox({text("e") | bold | size(WIDTH, EQUAL, 12), text("Editar proyecto")}),
                   hbox({text("t") | bold | size(WIDTH, EQUAL, 12), text("Abrir tareas del proyecto")}),
                   hbox({text("a") | bold | size(WIDTH, EQUAL, 12), text("Archivar con confirmación")}),
                   hbox({text("v") | bold | size(WIDTH, EQUAL, 12), text("Alternar activos / archivados")}),
                   hbox({text("/") | bold | size(WIDTH, EQUAL, 12), text("Buscar por nombre o alias")}),
                   hbox({text("r") | bold | size(WIDTH, EQUAL, 12), text("Recargar")}),
                   hbox({text("Ctrl+S") | bold | size(WIDTH, EQUAL, 12), text("Guardar formulario")}),
                   hbox({text("Esc / q") | bold | size(WIDTH, EQUAL, 12), text("Cancelar o volver")}),
                   separator(),
                   text("Presioná ?, Esc o q para cerrar") | dim | center,
               })) |
           size(WIDTH, EQUAL, 58);
}

}  // namespace

ftxui::Component create_project_screen(ProjectService& projects,
                                       NoteService& notes,
                                       std::function<std::optional<Project>()> requested_project,
                                       std::function<void(const Project&)> on_tasks,
                                       std::function<void(KnowledgeRequest)> on_knowledge,
                                       std::function<void()> on_back) {
    using namespace ftxui;

    auto state = std::make_shared<ProjectScreenState>(projects, notes, std::move(requested_project),
                                                       std::move(on_tasks), std::move(on_knowledge), std::move(on_back));

    InputOption search_options;
    search_options.multiline = false;
    search_options.on_change = [state] { state->apply_filter(); };
    auto search_input = Input(&state->search_query, "nombre o alias", search_options);

    InputOption name_options;
    name_options.multiline = false;
    name_options.on_change = [state] {
        if (!state->alias_edited) {
            state->form_alias = generated_alias(state->form_name);
        }
    };
    auto name_input = Input(&state->form_name, "Nombre obligatorio", name_options);

    InputOption alias_options;
    alias_options.multiline = false;
    alias_options.on_change = [state] { state->alias_edited = true; };
    auto alias_input = Input(&state->form_alias, "alias-unico", alias_options);

    InputOption description_options;
    description_options.multiline = true;
    auto description_input = Input(&state->form_description, "Descripción opcional", description_options);

    InputOption single_line_options;
    single_line_options.multiline = false;
    auto start_date_input = Input(&state->form_start_date, "YYYY-MM-DD", single_line_options);
    auto target_date_input = Input(&state->form_target_date, "YYYY-MM-DD", single_line_options);
    auto local_path_input = Input(&state->form_local_path, "Ruta opcional", single_line_options);
    auto status_dropdown = Dropdown(&state->status_entries, &state->status_selected);

    auto form_container = Container::Vertical({
        name_input,
        alias_input,
        description_input,
        status_dropdown,
        start_date_input,
        target_date_input,
        local_path_input,
    });
    auto controls = Container::Tab({search_input, form_container}, &state->control_tab);

    state->reload();

    auto renderer = Renderer(controls, [=] {
        state->sync_requested_project();
        Element body;
        std::string footer;

        if (state->view == ProjectView::list) {
            Elements rows;
            if (state->visible_projects.empty()) {
                if (!state->search_query.empty()) {
                    rows.push_back(text("No hay proyectos que coincidan con la búsqueda.") | center | dim);
                } else if (state->showing_archived) {
                    rows.push_back(text("No hay proyectos archivados.") | center | dim);
                } else {
                    rows.push_back(vbox({
                        text("Todavía no hay proyectos.") | center,
                        text("Presioná n para crear el primero.") | center | dim,
                    }));
                }
            } else {
                rows.push_back(
                    hbox({
                        text(" Alias") | bold | size(WIDTH, EQUAL, 18),
                        text("Nombre") | bold | size(WIDTH, EQUAL, 26),
                        text("Estado") | bold | size(WIDTH, EQUAL, 14),
                        text("Objetivo") | bold | size(WIDTH, EQUAL, 12),
                        text("Actualizado") | bold,
                    }) |
                    color(Color::Cyan)
                );
                rows.push_back(separator());
                for (std::size_t index = 0; index < state->visible_projects.size(); ++index) {
                    const auto& project = state->visible_projects[index];
                    auto row = hbox({
                        text(" " + shortened(project.alias, 16)) | size(WIDTH, EQUAL, 18),
                        text(shortened(project.name, 24)) | size(WIDTH, EQUAL, 26),
                        text(std::string(project_status_label(project.status))) | size(WIDTH, EQUAL, 14),
                        text(project.target_date.value_or("—")) | size(WIDTH, EQUAL, 12),
                        text(project.updated_at.substr(0, std::min<std::size_t>(10, project.updated_at.size()))),
                    });
                    if (static_cast<int>(index) == state->selected) {
                        row = row | inverted;
                    }
                    rows.push_back(row);
                }
            }

            Elements list_elements{
                hbox({
                    text(state->showing_archived ? " PROYECTOS ARCHIVADOS " : " PROYECTOS ACTIVOS ") | bold |
                        color(Color::Cyan),
                    filler(),
                    text(std::to_string(state->visible_projects.size()) + " proyecto(s) ") | dim,
                }),
                separator(),
            };
            if (state->searching || !state->search_query.empty()) {
                list_elements.push_back(hbox({text(" Buscar: ") | bold, search_input->Render() | flex}));
                list_elements.push_back(separator());
            }
            list_elements.push_back(vbox(std::move(rows)) | frame | flex);
            if (!state->message.empty()) {
                list_elements.push_back(text(" " + state->message) | color(Color::Green));
            }
            if (!state->error.empty()) {
                list_elements.push_back(text(" " + state->error) | color(Color::Red));
            }
            body = vbox(std::move(list_elements)) | border | flex;
            footer = "↑/↓ navegar  Enter detalle  t tareas  n nuevo  e editar  a archivar  v vista  / buscar  ? ayuda";
        } else if (state->view == ProjectView::detail && state->current_project) {
            const auto& project = *state->current_project;
            body = vbox({
                       hbox({text(" PROYECTO ") | bold | color(Color::Cyan), filler(),
                             text(std::string(project_status_label(project.status)) + " ") | bold}),
                       separator(),
                       hbox({text("Nombre:       ") | bold, text(project.name)}),
                       hbox({text("Alias:        ") | bold, text(project.alias)}),
                       hbox({text("Descripción:  ") | bold, paragraph(project.description.value_or("—")) | flex}),
                       hbox({text("Fecha inicial:") | bold, text(" " + project.start_date.value_or("—"))}),
                       hbox({text("Fecha objetivo:") | bold, text(" " + project.target_date.value_or("—"))}),
                       hbox({text("Ruta local:   ") | bold, text(project.local_path.value_or("—"))}),
                       separator(),
                       text("Notas relacionadas") | bold | color(Color::Cyan),
                       state->related_notes.empty()
                           ? text("No hay notas activas relacionadas.") | dim
                           : vbox([&] {
                                 Elements items;
                                 for (std::size_t index = 0; index < state->related_notes.size(); ++index) {
                                     const auto& note = state->related_notes[index].note;
                                     items.push_back(text(std::to_string(index + 1) + ". " +
                                                          (note.is_favorite ? "* " : "  ") +
                                                          std::string(note_type_label(note.type)) + " · " +
                                                          shortened(note.title, 44) + " · " +
                                                          note.updated_at.substr(0, 10)));
                                 }
                                 return items;
                             }()),
                       separator(),
                       text("Creado: " + project.created_at) | dim,
                       text("Modificado: " + project.updated_at) | dim,
                       project.archived_at ? text("Archivado: " + *project.archived_at) | dim : text(""),
                       filler(),
                       !state->message.empty() ? text(state->message) | color(Color::Green) : text(""),
                       !state->error.empty() ? text(state->error) | color(Color::Red) : text(""),
                   }) |
                   border | flex;
            footer = project.status == ProjectStatus::archived
                         ? "t tareas  n nueva nota  1-5 abrir nota  e editar  Esc/q volver"
                         : "t tareas  n nueva nota  1-5 abrir nota  e editar  a archivar  Esc/q volver";
        } else if (state->view == ProjectView::form) {
            body = vbox({
                       text(state->editing ? " EDITAR PROYECTO " : " NUEVO PROYECTO ") | bold | color(Color::Cyan),
                       separator(),
                       hbox({text("Nombre:        ") | size(WIDTH, EQUAL, 17), name_input->Render() | flex}),
                       hbox({text("Alias:         ") | size(WIDTH, EQUAL, 17), alias_input->Render() | flex}),
                       hbox({text("Descripción:   ") | size(WIDTH, EQUAL, 17),
                             description_input->Render() | size(HEIGHT, LESS_THAN, 4) | flex}),
                       hbox({text("Estado:        ") | size(WIDTH, EQUAL, 17), status_dropdown->Render() | flex}),
                       hbox({text("Fecha inicial: ") | size(WIDTH, EQUAL, 17), start_date_input->Render() | flex}),
                       hbox({text("Fecha objetivo:") | size(WIDTH, EQUAL, 17), target_date_input->Render() | flex}),
                       hbox({text("Ruta local:    ") | size(WIDTH, EQUAL, 17), local_path_input->Render() | flex}),
                       separator(),
                       !state->error.empty() ? text("Error: " + state->error) | color(Color::Red) : text(""),
                       filler(),
                       text("Los campos Nombre y Alias son obligatorios.") | dim,
                   }) |
                   border | flex;
            footer = "Tab cambiar campo  Ctrl+S guardar  Esc cancelar  ? ayuda";
        } else if (state->view == ProjectView::archive_confirmation && state->current_project) {
            body = vbox({
                       filler(),
                       window(
                           text(" CONFIRMAR ARCHIVADO ") | bold | color(Color::Yellow),
                           vbox({
                               text("Se archivará el proyecto:") | center,
                               text(state->current_project->name + " (" + state->current_project->alias + ")") |
                                   bold | center,
                               separator(),
                               text("El proyecto dejará de aparecer en la lista activa.") | center,
                               text("Presioná Enter o s para confirmar; Esc, q o n para cancelar.") | dim | center,
                               !state->error.empty() ? text(state->error) | color(Color::Red) | center : text(""),
                           }) |
                               size(WIDTH, EQUAL, 68)),
                       filler(),
                   }) |
                   flex;
            footer = "Enter/s confirmar  Esc/q/n cancelar";
        } else {
            body = text("No se pudo mostrar el proyecto.") | center | border | flex;
            footer = "Esc/q volver";
        }

        auto base = vbox({
            hbox({text(" MODRA · PROYECTOS ") | bold | color(Color::Cyan), filler(),
                  text(state->showing_archived ? "vista archivados " : "vista activos ") | dim}) |
                border,
            body,
            text(" " + footer + " ") | border,
        });
        if (!state->help_visible) {
            return base;
        }
        return dbox({base, render_help() | clear_under | center});
    });

    return CatchEvent(renderer, [=](Event event) {
        state->sync_requested_project();
        if (event == Event::Custom) {
            state->reload();
            if (state->current_project) state->load_related_notes(state->current_project->id);
            return true;
        }
        if (state->help_visible) {
            if (event == Event::Character('?') || event == Event::Escape || shortcut(event, 'q')) {
                state->help_visible = false;
            }
            return true;
        }
        if (event == Event::Character('?')) {
            state->help_visible = true;
            return true;
        }

        if (state->view == ProjectView::form) {
            if (event == Event::Escape) {
                state->error.clear();
                state->view = state->editing ? ProjectView::detail : ProjectView::list;
                return true;
            }
            if (event == Event::CtrlS) {
                state->save();
                return true;
            }
            return false;
        }

        if (state->view == ProjectView::archive_confirmation) {
            if (event == Event::Return || shortcut(event, 's')) {
                state->confirm_archive();
            } else if (event == Event::Escape || shortcut(event, 'q') || shortcut(event, 'n')) {
                state->view = state->archive_return_view;
            }
            return true;
        }

        if (state->view == ProjectView::detail) {
            if (event == Event::Escape || shortcut(event, 'q')) {
                state->view = ProjectView::list;
                state->current_project.reset();
                return true;
            }
            if (shortcut(event, 'e') && state->current_project) {
                state->begin_edit(*state->current_project);
                state->control_tab = 1;
                name_input->TakeFocus();
                return true;
            }
            if (shortcut(event, 't') && state->current_project) {
                state->on_tasks(*state->current_project);
                return true;
            }
            if (shortcut(event, 'n') && state->current_project) {
                state->on_knowledge(KnowledgeRequest{std::nullopt, state->current_project->id, std::nullopt, true});
                return true;
            }
            const std::string pressed = event.character();
            if (pressed.size() == 1 && pressed[0] >= '1' && pressed[0] <= '5') {
                const std::size_t index = static_cast<std::size_t>(pressed[0] - '1');
                if (index < state->related_notes.size()) {
                    state->on_knowledge(KnowledgeRequest{state->related_notes[index].note.id, std::nullopt,
                                                         std::nullopt, false});
                }
                return true;
            }
            if (shortcut(event, 'a') && state->current_project &&
                state->current_project->status != ProjectStatus::archived) {
                state->request_archive(*state->current_project);
                return true;
            }
            return true;
        }

        if (state->searching) {
            if (event == Event::Escape || event == Event::Return) {
                state->searching = false;
                return true;
            }
            return false;
        }

        if (event == Event::Escape || shortcut(event, 'q')) {
            state->on_back();
            return true;
        }
        if (event == Event::ArrowDown) {
            if (!state->visible_projects.empty()) {
                state->selected = std::min(state->selected + 1, static_cast<int>(state->visible_projects.size()) - 1);
            }
            return true;
        }
        if (event == Event::ArrowUp) {
            state->selected = std::max(state->selected - 1, 0);
            return true;
        }
        if (event == Event::Return) {
            if (const auto* project = state->selected_project()) {
                state->current_project = *project;
                state->load_related_notes(project->id);
                state->message.clear();
                state->view = ProjectView::detail;
            }
            return true;
        }
        if (shortcut(event, 'n') && !state->showing_archived) {
            state->begin_create();
            state->control_tab = 1;
            name_input->TakeFocus();
            return true;
        }
        if (shortcut(event, 'e')) {
            if (const auto* project = state->selected_project()) {
                state->current_project = *project;
                state->begin_edit(*project);
                state->control_tab = 1;
                name_input->TakeFocus();
            }
            return true;
        }
        if (shortcut(event, 't')) {
            if (const auto* project = state->selected_project()) {
                state->on_tasks(*project);
            }
            return true;
        }
        if (shortcut(event, 'a') && !state->showing_archived) {
            if (const auto* project = state->selected_project()) {
                state->request_archive(*project);
            }
            return true;
        }
        if (shortcut(event, 'v')) {
            state->showing_archived = !state->showing_archived;
            state->selected = 0;
            state->message.clear();
            state->reload();
            return true;
        }
        if (event == Event::Character('/')) {
            state->searching = true;
            state->control_tab = 0;
            search_input->TakeFocus();
            return true;
        }
        if (shortcut(event, 'r')) {
            state->reload();
            state->message = "Listado actualizado.";
            return true;
        }
        return true;
    });
}

}  // namespace modra
