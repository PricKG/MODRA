#include "ui/TaskScreen.h"

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

#include "application/TaskService.h"
#include "application/NoteService.h"
#include "domain/Task.h"
#include "ui/KnowledgeScreen.h"
#include "ui/KeyEvent.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

enum class TaskView {
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
    return value.empty() ? std::nullopt : std::optional<std::string>{value};
}

struct TaskScreenState {
    TaskScreenState(TaskService& task_service,
                    NoteService& note_service,
                    std::function<std::optional<Project>()> project_provider,
                    std::function<void(KnowledgeRequest)> open_knowledge,
                    std::function<void()> back)
        : tasks(task_service), notes(note_service), current_project(std::move(project_provider)),
          on_knowledge(std::move(open_knowledge)), on_back(std::move(back)) {}

    void sync_project() {
        const auto selected_project = current_project();
        if (!selected_project) {
            project.reset();
            return;
        }
        if (!project || project->id != selected_project->id) {
            project = *selected_project;
            view = TaskView::list;
            showing_archived = false;
            searching = false;
            selected = 0;
            search_query.clear();
            current_task.reset();
            message.clear();
            reload();
        } else {
            project = *selected_project;
        }
    }

    void apply_filter() {
        visible_tasks.clear();
        const std::string query = lowercase(search_query);
        for (const auto& task : loaded_tasks) {
            if (query.empty() || lowercase(task.title).find(query) != std::string::npos) {
                visible_tasks.push_back(task);
            }
        }
        selected = visible_tasks.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(visible_tasks.size()) - 1);
    }

    void reload() {
        if (!project) {
            return;
        }
        try {
            loaded_tasks = showing_archived ? tasks.list_archived(project->id) : tasks.list_active(project->id);
            apply_filter();
            error.clear();
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    const Task* selected_task() const {
        if (visible_tasks.empty() || selected < 0 || selected >= static_cast<int>(visible_tasks.size())) {
            return nullptr;
        }
        return &visible_tasks[static_cast<std::size_t>(selected)];
    }

    void begin_create() {
        editing = false;
        editing_id = 0;
        form_title.clear();
        form_description.clear();
        form_assignee_name.clear();
        form_due_date.clear();
        form_blocked_reason.clear();
        type_selected = 0;
        status_entries = {"Pendiente"};
        status_selected = 0;
        priority_selected = 1;
        error.clear();
        view = TaskView::form;
    }

    void begin_edit(const Task& task) {
        editing = true;
        editing_id = task.id;
        form_title = task.title;
        form_description = task.description.value_or("");
        form_assignee_name = task.assignee_name.value_or("");
        form_due_date = task.due_date.value_or("");
        form_blocked_reason = task.blocked_reason.value_or("");
        type_selected = static_cast<int>(task.type);
        status_entries = {"Pendiente", "En curso", "Bloqueada", "En revisión", "Finalizada", "Cancelada"};
        status_selected = static_cast<int>(task.status);
        priority_selected = static_cast<int>(task.priority);
        error.clear();
        view = TaskView::form;
    }

    void save() {
        if (!project) {
            error = "No hay un proyecto seleccionado.";
            return;
        }
        constexpr TaskType types[]{TaskType::technical,
                                   TaskType::administrative,
                                   TaskType::management,
                                   TaskType::research,
                                   TaskType::documentation,
                                   TaskType::follow_up};
        constexpr TaskStatus statuses[]{TaskStatus::pending,
                                       TaskStatus::in_progress,
                                       TaskStatus::blocked,
                                       TaskStatus::in_review,
                                       TaskStatus::completed,
                                       TaskStatus::cancelled};
        constexpr TaskPriority priorities[]{
            TaskPriority::low, TaskPriority::normal, TaskPriority::high, TaskPriority::critical};
        TaskInput input{
            project->id,
            form_title,
            optional_value(form_description),
            types[static_cast<std::size_t>(type_selected)],
            editing ? statuses[static_cast<std::size_t>(status_selected)] : TaskStatus::pending,
            priorities[static_cast<std::size_t>(priority_selected)],
            optional_value(form_assignee_name),
            optional_value(form_due_date),
            optional_value(form_blocked_reason),
        };
        try {
            current_task = editing ? tasks.update(editing_id, input) : tasks.create(input);
            message = editing ? "Tarea actualizada." : "Tarea creada.";
            error.clear();
            reload();
            view = TaskView::detail;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void request_archive(const Task& task) {
        current_task = task;
        archive_return_view = view;
        error.clear();
        view = TaskView::archive_confirmation;
    }

    void confirm_archive() {
        if (!current_task) {
            view = TaskView::list;
            return;
        }
        try {
            tasks.archive(current_task->id);
            message = "Tarea archivada.";
            error.clear();
            reload();
            current_task.reset();
            view = TaskView::list;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void restore_task(const Task& task) {
        try {
            tasks.restore(task.id);
            message = "Tarea desarchivada.";
            error.clear();
            current_task.reset();
            reload();
            view = TaskView::list;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void load_related_notes(std::int64_t task_id) {
        try {
            related_notes = notes.list_by_task(task_id);
            if (related_notes.size() > 5) related_notes.resize(5);
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    bool project_is_archived() const {
        return project && project->status == ProjectStatus::archived;
    }

    TaskService& tasks;
    NoteService& notes;
    std::function<std::optional<Project>()> current_project;
    std::function<void(KnowledgeRequest)> on_knowledge;
    std::function<void()> on_back;
    std::optional<Project> project;
    TaskView view = TaskView::list;
    TaskView archive_return_view = TaskView::list;
    bool showing_archived = false;
    bool searching = false;
    bool help_visible = false;
    bool editing = false;
    int control_tab = 0;
    int selected = 0;
    std::int64_t editing_id = 0;
    std::vector<Task> loaded_tasks;
    std::vector<Task> visible_tasks;
    std::vector<NoteSummary> related_notes;
    std::optional<Task> current_task;
    std::string search_query;
    std::string error;
    std::string message;
    std::string form_title;
    std::string form_description;
    std::string form_assignee_name;
    std::string form_due_date;
    std::string form_blocked_reason;
    std::vector<std::string> type_entries{
        "Técnica", "Administrativa", "Gestión", "Investigación", "Documentación", "Seguimiento"};
    std::vector<std::string> status_entries{"Pendiente"};
    std::vector<std::string> priority_entries{"Baja", "Normal", "Alta", "Crítica"};
    int type_selected = 0;
    int status_selected = 0;
    int priority_selected = 1;
};

ftxui::Element render_help() {
    using namespace ftxui;
    return window(
               text(" AYUDA · TAREAS ") | bold | color(Color::Cyan),
               vbox({
                   hbox({text("↑") | bold | size(WIDTH, EQUAL, 12), text("Selección anterior")}),
                   hbox({text("↓") | bold | size(WIDTH, EQUAL, 12), text("Selección siguiente")}),
                   hbox({text("Enter") | bold | size(WIDTH, EQUAL, 12), text("Abrir detalle")}),
                   hbox({text("n") | bold | size(WIDTH, EQUAL, 12), text("Crear tarea")}),
                   hbox({text("e") | bold | size(WIDTH, EQUAL, 12), text("Editar tarea")}),
                   hbox({text("a") | bold | size(WIDTH, EQUAL, 12), text("Archivar con confirmación")}),
                   hbox({text("u") | bold | size(WIDTH, EQUAL, 12), text("Desarchivar tarea")}),
                   hbox({text("v") | bold | size(WIDTH, EQUAL, 12), text("Alternar activas / archivadas")}),
                   hbox({text("/") | bold | size(WIDTH, EQUAL, 12), text("Buscar por título")}),
                   hbox({text("r") | bold | size(WIDTH, EQUAL, 12), text("Recargar")}),
                   hbox({text("Ctrl+S") | bold | size(WIDTH, EQUAL, 12), text("Guardar formulario")}),
                   hbox({text("Esc / q") | bold | size(WIDTH, EQUAL, 12), text("Cancelar o volver")}),
                   separator(),
                   text("Presioná ?, Esc o q para cerrar") | dim | center,
               })) |
           size(WIDTH, EQUAL, 58);
}

}  // namespace

ftxui::Component create_task_screen(TaskService& tasks,
                                    NoteService& notes,
                                    std::function<std::optional<Project>()> current_project,
                                    std::function<void(KnowledgeRequest)> on_knowledge,
                                    std::function<void()> on_back) {
    using namespace ftxui;
    auto state = std::make_shared<TaskScreenState>(tasks, notes, std::move(current_project),
                                                    std::move(on_knowledge), std::move(on_back));

    InputOption search_options;
    search_options.multiline = false;
    search_options.on_change = [state] { state->apply_filter(); };
    auto search_input = Input(&state->search_query, "título", search_options);

    InputOption single_line;
    single_line.multiline = false;
    auto title_input = Input(&state->form_title, "Título obligatorio", single_line);
    InputOption multiline;
    multiline.multiline = true;
    auto description_input = Input(&state->form_description, "Descripción opcional", multiline);
    auto assignee_input = Input(&state->form_assignee_name, "Nombre opcional", single_line);
    auto due_date_input = Input(&state->form_due_date, "YYYY-MM-DD", single_line);
    auto blocked_reason_input = Input(&state->form_blocked_reason, "Obligatorio si está bloqueada", multiline);
    auto type_dropdown = Dropdown(&state->type_entries, &state->type_selected);
    auto status_dropdown = Dropdown(&state->status_entries, &state->status_selected);
    auto priority_dropdown = Dropdown(&state->priority_entries, &state->priority_selected);

    auto form = Container::Vertical({title_input,
                                     description_input,
                                     type_dropdown,
                                     status_dropdown,
                                     priority_dropdown,
                                     assignee_input,
                                     due_date_input,
                                     blocked_reason_input});
    auto controls = Container::Tab({search_input, form}, &state->control_tab);

    auto renderer = Renderer(controls, [=] {
        state->sync_project();
        if (!state->project) {
            return vbox({text(" MODRA · TAREAS ") | bold | border,
                         text("No hay un proyecto seleccionado.") | center | flex | border,
                         text(" Esc/q volver ") | border});
        }

        Element body;
        std::string footer;
        if (state->view == TaskView::list) {
            Elements rows;
            if (state->visible_tasks.empty()) {
                if (!state->search_query.empty()) {
                    rows.push_back(text("No hay tareas que coincidan con la búsqueda.") | center | dim);
                } else if (state->showing_archived) {
                    rows.push_back(text("No hay tareas archivadas en este proyecto.") | center | dim);
                } else {
                    rows.push_back(vbox({text("Todavía no hay tareas en este proyecto.") | center,
                                         text("Presioná n para crear la primera.") | center | dim}));
                }
            } else {
                rows.push_back(hbox({text(" Título") | bold | size(WIDTH, EQUAL, 24),
                                     text("Estado") | bold | size(WIDTH, EQUAL, 13),
                                     text("Prioridad") | bold | size(WIDTH, EQUAL, 11),
                                     text("Responsable") | bold | size(WIDTH, EQUAL, 18),
                                     text("Revisar") | bold}) |
                               color(Color::Cyan));
                rows.push_back(separator());
                for (std::size_t index = 0; index < state->visible_tasks.size(); ++index) {
                    const auto& task = state->visible_tasks[index];
                    auto row = hbox({text(" " + shortened(task.title, 22)) | size(WIDTH, EQUAL, 24),
                                     text(std::string(task_status_label(task.status))) | size(WIDTH, EQUAL, 13),
                                     text(std::string(task_priority_label(task.priority))) | size(WIDTH, EQUAL, 11),
                                     text(shortened(task.assignee_name.value_or("—"), 16)) |
                                         size(WIDTH, EQUAL, 18),
                                     text(task.due_date.value_or("—"))});
                    if (static_cast<int>(index) == state->selected) {
                        row = row | inverted;
                    }
                    rows.push_back(row);
                }
            }
            Elements content{hbox({text(state->showing_archived ? " TAREAS ARCHIVADAS " : " TAREAS ACTIVAS ") |
                                        bold | color(Color::Cyan),
                                    filler(),
                                    text(std::to_string(state->visible_tasks.size()) + " tarea(s) ") | dim}),
                             separator()};
            if (state->searching || !state->search_query.empty()) {
                content.push_back(hbox({text(" Buscar: ") | bold, search_input->Render() | flex}));
                content.push_back(separator());
            }
            content.push_back(vbox(std::move(rows)) | frame | flex);
            if (state->project_is_archived()) {
                content.push_back(text(" Proyecto archivado: las tareas están en modo consulta.") | color(Color::Yellow));
            }
            if (!state->message.empty()) content.push_back(text(" " + state->message) | color(Color::Green));
            if (!state->error.empty()) content.push_back(text(" " + state->error) | color(Color::Red));
            body = vbox(std::move(content)) | border | flex;
            footer = state->project_is_archived()
                         ? "↑/↓ navegar  Enter detalle  v vista  / buscar  r recargar  Esc/q volver  ? ayuda"
                         : state->showing_archived
                               ? "↑/↓ navegar  Enter detalle  u desarchivar  v vista  / buscar  r recargar  ? ayuda"
                               : "↑/↓ navegar  Enter detalle  n nueva  e editar  a archivar  v vista  / buscar  ? ayuda";
        } else if (state->view == TaskView::detail && state->current_task) {
            const auto& task = *state->current_task;
            body = vbox({
                       hbox({text(" TAREA ") | bold | color(Color::Cyan), filler(),
                             text(std::string(task_status_label(task.status)) + " ") | bold}),
                       separator(),
                       hbox({text("Título:       ") | bold, text(task.title)}),
                       hbox({text("Descripción:  ") | bold, paragraph(task.description.value_or("—")) | flex}),
                       hbox({text("Tipo:         ") | bold, text(std::string(task_type_label(task.type)))}),
                       hbox({text("Prioridad:    ") | bold, text(std::string(task_priority_label(task.priority)))}),
                       hbox({text("Responsable:  ") | bold, text(task.assignee_name.value_or("—"))}),
                       hbox({text("Seguimiento:  ") | bold, text(task.due_date.value_or("—"))}),
                       hbox({text("Bloqueo:      ") | bold, paragraph(task.blocked_reason.value_or("—")) | flex}),
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
                                                          shortened(note.title, 48) + " · " +
                                                          note.updated_at.substr(0, 10)));
                                 }
                                 return items;
                             }()),
                       separator(),
                       text("Creada: " + task.created_at) | dim,
                       text("Modificada: " + task.updated_at) | dim,
                       task.completed_at ? text("Finalizada: " + *task.completed_at) | dim : text(""),
                       task.archived_at ? text("Archivada: " + *task.archived_at) | dim : text(""),
                       filler(),
                       !state->message.empty() ? text(state->message) | color(Color::Green) : text(""),
                       !state->error.empty() ? text(state->error) | color(Color::Red) : text(""),
                   }) |
                   border | flex;
            footer = state->project_is_archived()
                         ? "n nueva nota  1-5 abrir nota  Esc/q volver  ? ayuda"
                         : task.archived_at
                               ? "u desarchivar  n nueva nota  1-5 abrir nota  Esc/q volver"
                               : "n nueva nota  1-5 abrir nota  e editar  a archivar  Esc/q volver";
        } else if (state->view == TaskView::form) {
            body = vbox({
                       text(state->editing ? " EDITAR TAREA " : " NUEVA TAREA ") | bold | color(Color::Cyan),
                       separator(),
                       hbox({text("Título:       ") | size(WIDTH, EQUAL, 17), title_input->Render() | flex}),
                       hbox({text("Descripción:  ") | size(WIDTH, EQUAL, 17),
                             description_input->Render() | size(HEIGHT, LESS_THAN, 4) | flex}),
                       hbox({text("Tipo:         ") | size(WIDTH, EQUAL, 17), type_dropdown->Render() | flex}),
                       hbox({text("Estado:       ") | size(WIDTH, EQUAL, 17), status_dropdown->Render() | flex}),
                       hbox({text("Prioridad:    ") | size(WIDTH, EQUAL, 17), priority_dropdown->Render() | flex}),
                       hbox({text("Responsable:  ") | size(WIDTH, EQUAL, 17), assignee_input->Render() | flex}),
                       hbox({text("Seguimiento:   ") | size(WIDTH, EQUAL, 17), due_date_input->Render() | flex}),
                       hbox({text("Motivo bloqueo:") | size(WIDTH, EQUAL, 17),
                             blocked_reason_input->Render() | size(HEIGHT, LESS_THAN, 3) | flex}),
                       separator(),
                       !state->error.empty() ? text("Error: " + state->error) | color(Color::Red) : text(""),
                       filler(),
                       text("El título es obligatorio. Una tarea nueva comienza Pendiente.") | dim,
                   }) |
                   border | flex;
            footer = "Tab cambiar campo  Ctrl+S guardar  Esc cancelar  ? ayuda";
        } else if (state->view == TaskView::archive_confirmation && state->current_task) {
            body = vbox({filler(),
                         window(text(" CONFIRMAR ARCHIVADO ") | bold | color(Color::Yellow),
                                vbox({text("Se archivará la tarea:") | center,
                                      text(state->current_task->title) | bold | center,
                                      separator(),
                                      text("Presioná Enter o s para confirmar; Esc, q o n para cancelar.") | dim |
                                          center}) |
                                    size(WIDTH, EQUAL, 68)),
                         filler()}) |
                   flex;
            footer = "Enter/s confirmar  Esc/q/n cancelar";
        } else {
            body = text("No se pudo mostrar la tarea.") | center | border | flex;
            footer = "Esc/q volver";
        }

        auto base = vbox({hbox({text(" MODRA · " + state->project->name + " · TAREAS ") | bold |
                                    color(Color::Cyan),
                                filler(),
                                text(state->showing_archived ? "vista archivadas " : "vista activas ") | dim}) |
                                  border,
                              body,
                              text(" " + footer + " ") | border});
        return state->help_visible ? dbox({base, render_help() | clear_under | center}) : base;
    });

    return CatchEvent(renderer, [=](Event event) {
        state->sync_project();
        if (event == Event::Custom) {
            state->reload();
            if (state->current_task) state->load_related_notes(state->current_task->id);
            return true;
        }
        if (!state->project) {
            if (event == Event::Escape || shortcut(event, 'q')) state->on_back();
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
        if (state->view == TaskView::form) {
            if (event == Event::Escape) {
                state->error.clear();
                state->view = state->editing ? TaskView::detail : TaskView::list;
                return true;
            }
            if (event == Event::CtrlS) {
                state->save();
                return true;
            }
            return false;
        }
        if (state->view == TaskView::archive_confirmation) {
            if (event == Event::Return || shortcut(event, 's')) state->confirm_archive();
            else if (event == Event::Escape || shortcut(event, 'q') || shortcut(event, 'n'))
                state->view = state->archive_return_view;
            return true;
        }
        if (state->view == TaskView::detail) {
            if (event == Event::Escape || shortcut(event, 'q')) {
                state->view = TaskView::list;
                state->current_task.reset();
                return true;
            }
            if (!state->project_is_archived() && state->current_task && !state->current_task->archived_at) {
                if (shortcut(event, 'e')) {
                    state->begin_edit(*state->current_task);
                    state->control_tab = 1;
                    title_input->TakeFocus();
                } else if (shortcut(event, 'a')) {
                    state->request_archive(*state->current_task);
                }
            }
            if (shortcut(event, 'n') && state->current_task) {
                state->on_knowledge(KnowledgeRequest{std::nullopt, state->current_task->project_id,
                                                     state->current_task->id, true});
                return true;
            }
            if (shortcut(event, 'u') && state->current_task && state->current_task->archived_at &&
                !state->project_is_archived()) {
                state->restore_task(*state->current_task);
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
            return true;
        }
        if (state->searching) {
            if (event == Event::Escape || event == Event::Return) state->searching = false;
            else return false;
            return true;
        }
        if (event == Event::Escape || shortcut(event, 'q')) {
            state->on_back();
            return true;
        }
        if (event == Event::ArrowDown) {
            if (!state->visible_tasks.empty())
                state->selected = std::min(state->selected + 1, static_cast<int>(state->visible_tasks.size()) - 1);
            return true;
        }
        if (event == Event::ArrowUp) {
            state->selected = std::max(state->selected - 1, 0);
            return true;
        }
        if (event == Event::Return) {
            if (const auto* task = state->selected_task()) {
                state->current_task = *task;
                state->load_related_notes(task->id);
                state->message.clear();
                state->view = TaskView::detail;
            }
            return true;
        }
        if (!state->project_is_archived() && !state->showing_archived && shortcut(event, 'n')) {
            state->begin_create();
            state->control_tab = 1;
            title_input->TakeFocus();
            return true;
        }
        if (!state->project_is_archived() && shortcut(event, 'e')) {
            if (const auto* task = state->selected_task(); task && !task->archived_at) {
                state->current_task = *task;
                state->begin_edit(*task);
                state->control_tab = 1;
                title_input->TakeFocus();
            }
            return true;
        }
        if (!state->project_is_archived() && !state->showing_archived && shortcut(event, 'a')) {
            if (const auto* task = state->selected_task()) state->request_archive(*task);
            return true;
        }
        if (!state->project_is_archived() && state->showing_archived && shortcut(event, 'u')) {
            if (const auto* task = state->selected_task()) state->restore_task(*task);
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
