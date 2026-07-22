#include "ui/WorkScreen.h"

#include <algorithm>
#include <chrono>
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
#include <spdlog/spdlog.h>

#include "application/ProjectService.h"
#include "application/NoteService.h"
#include "application/TaskService.h"
#include "domain/Project.h"
#include "domain/Task.h"
#include "ui/KnowledgeScreen.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

enum class WorkMode { list, detail, form, filters, archive_confirmation };

std::string shortened(const std::string& value, std::size_t width) {
    if (value.size() <= width) return value;
    return width <= 3 ? value.substr(0, width) : value.substr(0, width - 3) + "...";
}

std::optional<std::string> optional_value(const std::string& value) {
    return value.empty() ? std::nullopt : std::optional<std::string>{value};
}

bool upcoming_date(const std::optional<std::string>& due_date, const std::string& today) {
    if (!due_date || *due_date <= today) return false;
    const auto parse = [](const std::string& value) {
        return std::chrono::sys_days{std::chrono::year{std::stoi(value.substr(0, 4))} /
                                     std::chrono::month{static_cast<unsigned>(std::stoul(value.substr(5, 2)))} /
                                     std::chrono::day{static_cast<unsigned>(std::stoul(value.substr(8, 2)))}};
    };
    return parse(*due_date) <= parse(today) + std::chrono::days{7};
}

const char* view_label(TaskQuickView view) {
    switch (view) {
        case TaskQuickView::all: return "Todas";
        case TaskQuickView::today: return "Hoy";
        case TaskQuickView::overdue: return "Atrasadas";
        case TaskQuickView::upcoming: return "Próximas";
        case TaskQuickView::blocked: return "Bloqueadas";
        case TaskQuickView::recently_completed: return "Finalizadas recientemente";
        case TaskQuickView::archived: return "Archivadas";
    }
    return "Todas";
}

const char* sort_label(TaskSort sort) {
    switch (sort) {
        case TaskSort::recommended: return "Recomendado";
        case TaskSort::due_date: return "Fecha seguimiento";
        case TaskSort::priority: return "Prioridad";
        case TaskSort::status: return "Estado";
        case TaskSort::project: return "Proyecto";
        case TaskSort::responsible: return "Responsable";
        case TaskSort::updated_at: return "Última actualización";
    }
    return "Recomendado";
}

struct WorkState {
    WorkState(TaskService& task_service,
              ProjectService& project_service,
              NoteService& note_service,
              std::function<void(std::int64_t)> open_project,
              std::function<void(KnowledgeRequest)> open_knowledge,
              std::function<std::optional<WorkOpenRequest>()> requested_open_value,
              std::function<void()> back)
        : tasks(task_service),
          projects(project_service),
          notes(note_service),
          on_open_project(std::move(open_project)),
          on_knowledge(std::move(open_knowledge)),
          requested_open(std::move(requested_open_value)),
          on_back(std::move(back)) {}

    void rebuild_filter_options() {
        project_filter_entries = {"Todos los proyectos"};
        project_form_entries.clear();
        for (const auto& project : filter_projects) {
            const std::string label = project.name + " (" + project.alias + ")";
            project_filter_entries.push_back(label);
        }
        for (const auto& project : active_projects)
            project_form_entries.push_back(project.name + " (" + project.alias + ")");
        responsible_filter_entries = {"Todos", "Sin responsable"};
        try {
            const auto names = tasks.list_distinct_responsibles();
            responsible_filter_entries.insert(responsible_filter_entries.end(), names.begin(), names.end());
        } catch (const std::exception& exception) {
            spdlog::error("Global task responsible filter error: {}", exception.what());
            error = exception.what();
        }
        project_filter_selected = std::clamp(project_filter_selected, 0,
                                             static_cast<int>(project_filter_entries.size()) - 1);
        responsible_filter_selected = std::clamp(responsible_filter_selected, 0,
                                                  static_cast<int>(responsible_filter_entries.size()) - 1);
    }

    TaskQuery current_query() const {
        TaskQuery query;
        query.view = quick_view;
        query.search = search;
        query.sort = sort;
        if (project_filter_selected > 0)
            query.project_id = filter_projects[static_cast<std::size_t>(project_filter_selected - 1)].id;
        if (responsible_filter_selected == 1) query.without_responsible = true;
        else if (responsible_filter_selected > 1)
            query.responsible = responsible_filter_entries[static_cast<std::size_t>(responsible_filter_selected)];
        constexpr TaskStatus statuses[]{TaskStatus::pending, TaskStatus::in_progress, TaskStatus::blocked,
                                        TaskStatus::in_review, TaskStatus::completed, TaskStatus::cancelled};
        constexpr TaskType types[]{TaskType::technical, TaskType::administrative, TaskType::management,
                                   TaskType::research, TaskType::documentation, TaskType::follow_up};
        constexpr TaskPriority priorities[]{TaskPriority::low, TaskPriority::normal, TaskPriority::high,
                                            TaskPriority::critical};
        if (status_filter_selected > 0) query.status = statuses[static_cast<std::size_t>(status_filter_selected - 1)];
        if (type_filter_selected > 0) query.type = types[static_cast<std::size_t>(type_filter_selected - 1)];
        if (priority_filter_selected > 0)
            query.priority = priorities[static_cast<std::size_t>(priority_filter_selected - 1)];
        if (date_filter_selected == 1) query.date_filter = TaskDateFilter::with_date;
        if (date_filter_selected == 2) query.date_filter = TaskDateFilter::without_date;
        return query;
    }

    void reload(bool refresh_options = true) {
        try {
            today = TaskService::current_local_date();
            if (refresh_options) {
                active_projects = projects.list_active();
                filter_projects = active_projects;
                const auto archived_projects = projects.list_archived();
                filter_projects.insert(filter_projects.end(), archived_projects.begin(), archived_projects.end());
                rebuild_filter_options();
            }
            visible = tasks.list_global(current_query(), today);
            selected = visible.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(visible.size()) - 1);
            error.clear();
        } catch (const std::exception& exception) {
            spdlog::error("Global task load error: {}", exception.what());
            error = exception.what();
        }
    }

    void apply_open_request(const WorkOpenRequest& request) {
        quick_view = request.task_id ? TaskQuickView::all : request.view;
        selected = 0;
        mode = WorkMode::list;
        current.reset();
        reload();
        if (!request.task_id) return;
        auto match = std::find_if(visible.begin(), visible.end(), [&](const TaskSummary& summary) {
            return summary.task.id == *request.task_id;
        });
        if (match == visible.end()) {
            quick_view = TaskQuickView::archived;
            reload();
            match = std::find_if(visible.begin(), visible.end(), [&](const TaskSummary& summary) {
                return summary.task.id == *request.task_id;
            });
        }
        if (match != visible.end()) {
            selected = static_cast<int>(std::distance(visible.begin(), match));
            current = *match;
            load_related_notes(current->task.id);
            mode = WorkMode::detail;
        } else {
            error = "La tarea seleccionada ya no está disponible en las vistas activas.";
        }
    }

    const TaskSummary* selected_summary() const {
        if (visible.empty() || selected < 0 || selected >= static_cast<int>(visible.size())) return nullptr;
        return &visible[static_cast<std::size_t>(selected)];
    }

    void clear_filters() {
        project_filter_selected = 0;
        responsible_filter_selected = 0;
        status_filter_selected = 0;
        type_filter_selected = 0;
        priority_filter_selected = 0;
        date_filter_selected = 0;
        search.clear();
        reload(false);
    }

    std::string active_filters() const {
        std::string result;
        auto add = [&](const std::string& value) {
            if (!result.empty()) result += " · ";
            result += value;
        };
        if (project_filter_selected > 0) add("Proyecto: " + project_filter_entries[project_filter_selected]);
        if (responsible_filter_selected > 0) add("Responsable: " + responsible_filter_entries[responsible_filter_selected]);
        if (status_filter_selected > 0) add("Estado: " + status_filter_entries[status_filter_selected]);
        if (type_filter_selected > 0) add("Tipo: " + type_filter_entries[type_filter_selected]);
        if (priority_filter_selected > 0) add("Prioridad: " + priority_filter_entries[priority_filter_selected]);
        if (date_filter_selected > 0) add(date_filter_entries[date_filter_selected]);
        if (!search.empty()) add("Búsqueda: " + search);
        return result.empty() ? "Sin filtros adicionales" : result;
    }

    void begin_create() {
        if (active_projects.empty()) {
            error = "No hay proyectos activos disponibles para crear una tarea.";
            return;
        }
        editing = false;
        editing_id = 0;
        project_form_selected = 0;
        form_title.clear(); form_description.clear(); form_assignee.clear(); form_due.clear(); form_blocked.clear();
        type_selected = 0; status_entries = {"Pendiente"}; status_selected = 0; priority_selected = 1;
        error.clear();
        mode = WorkMode::form;
    }

    void begin_edit(const TaskSummary& summary) {
        if (summary.project_archived || summary.task.archived_at) return;
        editing = true;
        editing_id = summary.task.id;
        project_form_selected = 0;
        for (std::size_t i = 0; i < active_projects.size(); ++i)
            if (active_projects[i].id == summary.task.project_id) project_form_selected = static_cast<int>(i);
        form_title = summary.task.title;
        form_description = summary.task.description.value_or("");
        form_assignee = summary.task.assignee_name.value_or("");
        form_due = summary.task.due_date.value_or("");
        form_blocked = summary.task.blocked_reason.value_or("");
        type_selected = static_cast<int>(summary.task.type);
        status_entries = {"Pendiente", "En curso", "Bloqueada", "En revisión", "Finalizada", "Cancelada"};
        status_selected = static_cast<int>(summary.task.status);
        priority_selected = static_cast<int>(summary.task.priority);
        error.clear();
        mode = WorkMode::form;
    }

    void save() {
        constexpr TaskType types[]{TaskType::technical, TaskType::administrative, TaskType::management,
                                   TaskType::research, TaskType::documentation, TaskType::follow_up};
        constexpr TaskStatus statuses[]{TaskStatus::pending, TaskStatus::in_progress, TaskStatus::blocked,
                                        TaskStatus::in_review, TaskStatus::completed, TaskStatus::cancelled};
        constexpr TaskPriority priorities[]{TaskPriority::low, TaskPriority::normal, TaskPriority::high,
                                            TaskPriority::critical};
        TaskInput input{active_projects[static_cast<std::size_t>(project_form_selected)].id,
                        form_title,
                        optional_value(form_description),
                        types[static_cast<std::size_t>(type_selected)],
                        editing ? statuses[static_cast<std::size_t>(status_selected)] : TaskStatus::pending,
                        priorities[static_cast<std::size_t>(priority_selected)],
                        optional_value(form_assignee), optional_value(form_due), optional_value(form_blocked)};
        try {
            const Task saved = editing ? tasks.update(editing_id, input) : tasks.create(input);
            spdlog::info("Task {} from global view: id={}, project_id={}", editing ? "updated" : "created",
                         saved.id, saved.project_id);
            message = editing ? "Tarea actualizada." : "Tarea creada.";
            mode = WorkMode::list;
            reload();
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void confirm_archive() {
        if (!current) return;
        try {
            const Task archived = tasks.archive(current->task.id);
            spdlog::info("Task archived from global view: id={}, project_id={}", archived.id, archived.project_id);
            message = "Tarea archivada.";
            current.reset();
            mode = WorkMode::list;
            reload();
        } catch (const std::exception& exception) { error = exception.what(); }
    }

    void restore_task(const TaskSummary& summary) {
        if (summary.project_archived || !summary.task.archived_at) return;
        try {
            const Task restored = tasks.restore(summary.task.id);
            spdlog::info("Task restored from global view: id={}, project_id={}", restored.id, restored.project_id);
            message = "Tarea desarchivada.";
            current.reset();
            mode = WorkMode::list;
            reload();
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

    TaskService& tasks;
    ProjectService& projects;
    NoteService& notes;
    std::function<void(std::int64_t)> on_open_project;
    std::function<void(KnowledgeRequest)> on_knowledge;
    std::function<std::optional<WorkOpenRequest>()> requested_open;
    std::function<void()> on_back;
    WorkMode mode = WorkMode::list;
    TaskQuickView quick_view = TaskQuickView::all;
    TaskSort sort = TaskSort::recommended;
    std::string today;
    std::vector<Project> active_projects;
    std::vector<Project> filter_projects;
    std::vector<TaskSummary> visible;
    std::optional<TaskSummary> current;
    std::vector<NoteSummary> related_notes;
    int selected = 0;
    bool editing = false;
    bool help = false;
    bool searching = false;
    std::int64_t editing_id = 0;
    int control_tab = 0;
    std::string search, error, message;
    std::vector<std::string> project_filter_entries, responsible_filter_entries, project_form_entries;
    std::vector<std::string> status_filter_entries{"Todos", "Pendiente", "En curso", "Bloqueada", "En revisión", "Finalizada", "Cancelada"};
    std::vector<std::string> type_filter_entries{"Todos", "Técnica", "Administrativa", "Gestión", "Investigación", "Documentación", "Seguimiento"};
    std::vector<std::string> priority_filter_entries{"Todas", "Baja", "Normal", "Alta", "Crítica"};
    std::vector<std::string> date_filter_entries{"Cualquier fecha", "Con seguimiento", "Sin seguimiento"};
    int project_filter_selected = 0, responsible_filter_selected = 0, status_filter_selected = 0;
    int type_filter_selected = 0, priority_filter_selected = 0, date_filter_selected = 0;
    std::string form_title, form_description, form_assignee, form_due, form_blocked;
    std::vector<std::string> type_entries{"Técnica", "Administrativa", "Gestión", "Investigación", "Documentación", "Seguimiento"};
    std::vector<std::string> status_entries{"Pendiente"};
    std::vector<std::string> priority_entries{"Baja", "Normal", "Alta", "Crítica"};
    int project_form_selected = 0, type_selected = 0, status_selected = 0, priority_selected = 1;
};

ftxui::Element help_dialog() {
    using namespace ftxui;
    return window(text(" AYUDA · MI TRABAJO ") | bold | color(Color::Cyan),
                  vbox({text("1 Todas  2 Hoy  3 Atrasadas  4 Próximas  5 Bloqueadas  6 Finalizadas  7 Archivadas"),
                        separator(), text("↑/↓ navegar · Enter detalle · n nueva · e editar · a archivar · u desarchivar"),
                        text("/ buscar · f filtros · s orden · r recargar · v activas/archivadas"),
                        text("p abrir proyecto desde detalle · Ctrl+S guardar/aplicar"), separator(),
                        text("?, Esc o q cierra esta ayuda") | dim | center})) |
           size(WIDTH, EQUAL, 90);
}

}  // namespace

ftxui::Component create_work_screen(TaskService& tasks,
                                    ProjectService& projects,
                                    NoteService& notes,
                                    std::function<void(std::int64_t)> on_open_project,
                                    std::function<void(KnowledgeRequest)> on_knowledge,
                                    std::function<std::optional<WorkOpenRequest>()> requested_open,
                                    std::function<void()> on_back) {
    using namespace ftxui;
    auto state = std::make_shared<WorkState>(tasks, projects, notes, std::move(on_open_project),
                                             std::move(on_knowledge), std::move(requested_open), std::move(on_back));
    InputOption search_options; search_options.multiline = false; search_options.on_change = [state] { state->reload(false); };
    auto search_input = Input(&state->search, "título, descripción, responsable o proyecto", search_options);
    auto project_filter = Dropdown(&state->project_filter_entries, &state->project_filter_selected);
    auto responsible_filter = Dropdown(&state->responsible_filter_entries, &state->responsible_filter_selected);
    auto status_filter = Dropdown(&state->status_filter_entries, &state->status_filter_selected);
    auto type_filter = Dropdown(&state->type_filter_entries, &state->type_filter_selected);
    auto priority_filter = Dropdown(&state->priority_filter_entries, &state->priority_filter_selected);
    auto date_filter = Dropdown(&state->date_filter_entries, &state->date_filter_selected);
    auto filter_controls = Container::Vertical({project_filter, responsible_filter, status_filter, type_filter,
                                                priority_filter, date_filter});

    InputOption single; single.multiline = false;
    InputOption multi; multi.multiline = true;
    auto title = Input(&state->form_title, "Título obligatorio", single);
    auto description = Input(&state->form_description, "Descripción opcional", multi);
    auto assignee = Input(&state->form_assignee, "Nombre opcional", single);
    auto due = Input(&state->form_due, "YYYY-MM-DD", single);
    auto blocked = Input(&state->form_blocked, "Obligatorio si está bloqueada", multi);
    auto project_form = Dropdown(&state->project_form_entries, &state->project_form_selected);
    auto type_form = Dropdown(&state->type_entries, &state->type_selected);
    auto status_form = Dropdown(&state->status_entries, &state->status_selected);
    auto priority_form = Dropdown(&state->priority_entries, &state->priority_selected);
    auto form_controls = Container::Vertical({project_form, title, description, assignee, type_form, status_form,
                                              priority_form, due, blocked});
    auto controls = Container::Tab({search_input, filter_controls, form_controls}, &state->control_tab);
    state->reload();

    auto renderer = Renderer(controls, [=] {
        if (const auto request = state->requested_open()) state->apply_open_request(*request);
        Element body;
        std::string footer;
        if (state->mode == WorkMode::list) {
            Elements rows;
            if (state->visible.empty()) {
                std::string empty = "No hay tareas para esta vista.";
                if (state->quick_view == TaskQuickView::all) empty = "Todavía no hay tareas activas.\nPresioná n para crear la primera.";
                else if (state->quick_view == TaskQuickView::today) empty = "No hay tareas con vencimiento para hoy.";
                else if (state->quick_view == TaskQuickView::overdue) empty = "No hay seguimientos atrasados.";
                else if (state->quick_view == TaskQuickView::blocked) empty = "No hay tareas bloqueadas.";
                else if (state->quick_view == TaskQuickView::archived) empty = "No hay tareas archivadas.";
                rows.push_back(paragraph(empty) | center | dim);
            } else {
                rows.push_back(hbox({text(" Proyecto") | bold | size(WIDTH, EQUAL, 16),
                                     text("Título") | bold | size(WIDTH, EQUAL, 25),
                                     text("Responsable") | bold | size(WIDTH, EQUAL, 18),
                                     text("Tipo") | bold | size(WIDTH, EQUAL, 13),
                                     text("Estado") | bold | size(WIDTH, EQUAL, 14),
                                     text("Prioridad") | bold | size(WIDTH, EQUAL, 11),
                                     text("Revisar") | bold | size(WIDTH, EQUAL, 12), text("Actualizada") | bold}) |
                               color(Color::Cyan));
                rows.push_back(separator());
                for (std::size_t i = 0; i < state->visible.size(); ++i) {
                    const auto& summary = state->visible[i];
                    const auto& task = summary.task;
                    const bool overdue = task.due_date && *task.due_date < state->today &&
                                         task.status != TaskStatus::completed;
                    std::string markers;
                    if (overdue) markers += "!ATRASADA ";
                    if (task.status == TaskStatus::blocked) markers += "!BLOQ ";
                    if (task.priority == TaskPriority::critical) markers += "!CRIT ";
                    auto row = hbox({text(" " + shortened(summary.project_alias, 14)) | size(WIDTH, EQUAL, 16),
                                     text(shortened(markers + task.title, 23)) | size(WIDTH, EQUAL, 25),
                                     text(shortened(task.assignee_name.value_or("Sin responsable"), 16)) |
                                         size(WIDTH, EQUAL, 18),
                                     text(shortened(std::string(task_type_label(task.type)), 11)) | size(WIDTH, EQUAL, 13),
                                     text(std::string(task_status_label(task.status))) | size(WIDTH, EQUAL, 14),
                                     text(std::string(task_priority_label(task.priority))) | size(WIDTH, EQUAL, 11),
                                     text(task.due_date.value_or("Sin fecha")) | size(WIDTH, EQUAL, 12),
                                     text(task.updated_at.substr(0, 10))});
                    if (overdue) row = row | color(Color::Red);
                    else if (task.priority == TaskPriority::critical || task.status == TaskStatus::blocked)
                        row = row | color(Color::Yellow);
                    if (static_cast<int>(i) == state->selected) row = row | inverted;
                    rows.push_back(row);
                }
            }
            Elements content{hbox({text(" VISTA: " + std::string(view_label(state->quick_view)) + " ") | bold |
                                        color(Color::Cyan), filler(),
                                    text("Orden: " + std::string(sort_label(state->sort)) + " · Hoy: " + state->today + " ") | dim}),
                             separator(), text(" " + state->active_filters()) | dim};
            if (state->searching || !state->search.empty())
                content.push_back(hbox({text(" Buscar: ") | bold, search_input->Render() | flex}));
            content.push_back(separator()); content.push_back(vbox(std::move(rows)) | frame | flex);
            if (!state->message.empty()) content.push_back(text(" " + state->message) | color(Color::Green));
            if (!state->error.empty()) content.push_back(text(" " + state->error) | color(Color::Red));
            body = vbox(std::move(content)) | border | flex;
            footer = "1-7 vistas  ↑/↓ navegar  Enter detalle  n nueva  e editar  a archivar  u desarchivar  / buscar";
        } else if (state->mode == WorkMode::filters) {
            body = vbox({text(" FILTROS ") | bold | color(Color::Cyan), separator(),
                         hbox({text("Proyecto:      ") | size(WIDTH, EQUAL, 18), project_filter->Render() | flex}),
                         hbox({text("Responsable:   ") | size(WIDTH, EQUAL, 18), responsible_filter->Render() | flex}),
                         hbox({text("Estado:        ") | size(WIDTH, EQUAL, 18), status_filter->Render() | flex}),
                         hbox({text("Tipo:          ") | size(WIDTH, EQUAL, 18), type_filter->Render() | flex}),
                         hbox({text("Prioridad:     ") | size(WIDTH, EQUAL, 18), priority_filter->Render() | flex}),
                         hbox({text("Seguimiento:   ") | size(WIDTH, EQUAL, 18), date_filter->Render() | flex}),
                         separator(), text("Ctrl+S aplicar · c limpiar · Esc cancelar") | dim, filler()}) | border | flex;
            footer = "Tab cambiar filtro  Ctrl+S aplicar  c limpiar  Esc cancelar";
        } else if (state->mode == WorkMode::form) {
            body = vbox({text(state->editing ? " EDITAR TAREA GLOBAL " : " NUEVA TAREA GLOBAL ") | bold | color(Color::Cyan),
                         separator(), hbox({text("Proyecto:      ") | size(WIDTH, EQUAL, 18), project_form->Render() | flex}),
                         hbox({text("Título:        ") | size(WIDTH, EQUAL, 18), title->Render() | flex}),
                         hbox({text("Descripción:   ") | size(WIDTH, EQUAL, 18), description->Render() | size(HEIGHT, LESS_THAN, 3) | flex}),
                         hbox({text("Responsable:   ") | size(WIDTH, EQUAL, 18), assignee->Render() | flex}),
                         hbox({text("Tipo:          ") | size(WIDTH, EQUAL, 18), type_form->Render() | flex}),
                         hbox({text("Estado:        ") | size(WIDTH, EQUAL, 18), status_form->Render() | flex}),
                         hbox({text("Prioridad:     ") | size(WIDTH, EQUAL, 18), priority_form->Render() | flex}),
                         hbox({text("Seguimiento:   ") | size(WIDTH, EQUAL, 18), due->Render() | flex}),
                         hbox({text("Motivo bloqueo:") | size(WIDTH, EQUAL, 18), blocked->Render() | size(HEIGHT, LESS_THAN, 3) | flex}),
                         separator(), !state->error.empty() ? text("Error: " + state->error) | color(Color::Red) : text(""), filler()}) | border | flex;
            footer = "Tab cambiar campo  Ctrl+S guardar  Esc cancelar";
        } else if (state->mode == WorkMode::detail && state->current) {
            const auto& summary = *state->current; const auto& task = summary.task;
            const bool overdue = task.due_date && *task.due_date < state->today && task.status != TaskStatus::completed;
            const bool upcoming = upcoming_date(task.due_date, state->today) && task.status != TaskStatus::completed;
            body = vbox({hbox({text(" TAREA GLOBAL ") | bold | color(Color::Cyan), filler(),
                               text(std::string(task_status_label(task.status)) + " ") | bold}), separator(),
                         text("Proyecto: " + summary.project_name + " (" + summary.project_alias + ")") | bold,
                         summary.project_archived ? text("PROYECTO ARCHIVADO · SOLO CONSULTA") | color(Color::Yellow) : text(""),
                         overdue ? text("! SEGUIMIENTO ATRASADO") | bold | color(Color::Red) : text(""),
                         upcoming ? text("Próxima dentro de siete días") | color(Color::Yellow) : text(""),
                         hbox({text("Título:       ") | bold, text(task.title)}),
                         hbox({text("Descripción:  ") | bold, paragraph(task.description.value_or("—")) | flex}),
                         hbox({text("Responsable:  ") | bold, text(task.assignee_name.value_or("Sin responsable"))}),
                         hbox({text("Tipo:         ") | bold, text(std::string(task_type_label(task.type)))}),
                         hbox({text("Prioridad:    ") | bold, text(std::string(task_priority_label(task.priority)))}),
                         hbox({text("Seguimiento:  ") | bold, text(task.due_date.value_or("Sin fecha"))}),
                         hbox({text("Bloqueo:      ") | bold, paragraph(task.blocked_reason.value_or("—")) | flex}),
                         separator(), text("Notas relacionadas") | bold | color(Color::Cyan),
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
                         separator(), text("Creada: " + task.created_at) | dim, text("Actualizada: " + task.updated_at) | dim,
                         task.completed_at ? text("Finalizada: " + *task.completed_at) | dim : text(""), filler()}) | border | flex;
            footer = summary.project_archived
                         ? "n nueva nota  1-5 abrir nota  p proyecto  Esc/q volver"
                         : task.archived_at
                               ? "u desarchivar  n nueva nota  1-5 abrir nota  p proyecto  Esc/q volver"
                               : "n nueva nota  1-5 abrir nota  e editar  a archivar  p proyecto  Esc/q volver";
        } else {
            body = window(text(" CONFIRMAR ARCHIVADO ") | bold | color(Color::Yellow),
                          vbox({text("Se archivará: " + state->current->task.title) | bold | center,
                                text("Enter/s confirma · Esc/q/n cancela") | dim | center})) | center;
            footer = "Enter/s confirmar  Esc/q/n cancelar";
        }
        auto base = vbox({hbox({text(" MODRA · MI TRABAJO ") | bold | color(Color::Cyan), filler(),
                                text(std::to_string(state->visible.size()) + " tarea(s) ") | dim}) | border,
                          body, text(" " + footer + " ") | border});
        return state->help ? dbox({base, help_dialog() | clear_under | center}) : base;
    });

    return CatchEvent(renderer, [=](Event event) {
        if (event == Event::Custom) {
            state->reload();
            if (state->current) state->load_related_notes(state->current->task.id);
            return true;
        }
        if (state->help) {
            if (event == Event::Character('?') || event == Event::Escape || shortcut(event, 'q')) state->help = false;
            return true;
        }
        if (event == Event::Character('?')) { state->help = true; return true; }
        if (state->mode == WorkMode::form) {
            if (event == Event::Escape) { state->mode = WorkMode::list; state->error.clear(); return true; }
            if (event == Event::CtrlS) { state->save(); return true; }
            return false;
        }
        if (state->mode == WorkMode::filters) {
            if (event == Event::Escape) { state->mode = WorkMode::list; return true; }
            if (shortcut(event, 'c')) { state->clear_filters(); state->mode = WorkMode::list; return true; }
            if (event == Event::CtrlS || event == Event::Return) { state->reload(false); state->mode = WorkMode::list; return true; }
            return false;
        }
        if (state->mode == WorkMode::archive_confirmation) {
            if (event == Event::Return || shortcut(event, 's')) state->confirm_archive();
            else if (event == Event::Escape || shortcut(event, 'q') || shortcut(event, 'n')) state->mode = WorkMode::detail;
            return true;
        }
        if (state->mode == WorkMode::detail) {
            if (event == Event::Escape || shortcut(event, 'q')) { state->mode = WorkMode::list; state->current.reset(); return true; }
            if (shortcut(event, 'p') && state->current) { state->on_open_project(state->current->task.project_id); return true; }
            if (shortcut(event, 'n') && state->current) {
                state->on_knowledge(KnowledgeRequest{std::nullopt, state->current->task.project_id,
                                                     state->current->task.id, true});
                return true;
            }
            const std::string pressed = event.character();
            if (pressed.size() == 1 && pressed[0] >= '1' && pressed[0] <= '5') {
                const std::size_t index = static_cast<std::size_t>(pressed[0] - '1');
                if (index < state->related_notes.size())
                    state->on_knowledge(KnowledgeRequest{state->related_notes[index].note.id, std::nullopt,
                                                         std::nullopt, false});
                return true;
            }
            if (state->current && !state->current->project_archived && !state->current->task.archived_at) {
                if (shortcut(event, 'e')) { state->begin_edit(*state->current); state->control_tab = 2; title->TakeFocus(); }
                else if (shortcut(event, 'a')) state->mode = WorkMode::archive_confirmation;
            }
            if (shortcut(event, 'u') && state->current && !state->current->project_archived &&
                state->current->task.archived_at) {
                state->restore_task(*state->current);
            }
            return true;
        }
        if (state->searching) {
            if (event == Event::Escape || event == Event::Return) state->searching = false;
            else return false;
            return true;
        }
        if (event == Event::Escape || shortcut(event, 'q')) { state->on_back(); return true; }
        if (event == Event::ArrowDown) {
            if (!state->visible.empty()) state->selected = std::min(state->selected + 1, static_cast<int>(state->visible.size()) - 1);
            return true;
        }
        if (event == Event::ArrowUp) { state->selected = std::max(0, state->selected - 1); return true; }
        if (event == Event::Return) { if (const auto* item = state->selected_summary()) { state->current = *item; state->load_related_notes(item->task.id); state->mode = WorkMode::detail; } return true; }
        if (shortcut(event, 'n') && state->quick_view != TaskQuickView::archived) { state->begin_create(); state->control_tab = 2; title->TakeFocus(); return true; }
        if (shortcut(event, 'e')) { if (const auto* item = state->selected_summary()) { state->current = *item; state->begin_edit(*item); state->control_tab = 2; title->TakeFocus(); } return true; }
        if (shortcut(event, 'a')) { if (const auto* item = state->selected_summary(); item && !item->project_archived && !item->task.archived_at) { state->current = *item; state->mode = WorkMode::archive_confirmation; } return true; }
        if (shortcut(event, 'u')) { if (const auto* item = state->selected_summary(); item && !item->project_archived && item->task.archived_at) state->restore_task(*item); return true; }
        if (event == Event::Character('/')) { state->searching = true; state->control_tab = 0; search_input->TakeFocus(); return true; }
        if (shortcut(event, 'f')) { state->mode = WorkMode::filters; state->control_tab = 1; project_filter->TakeFocus(); return true; }
        if (shortcut(event, 's')) { state->sort = static_cast<TaskSort>((static_cast<int>(state->sort) + 1) % 7); state->reload(false); return true; }
        if (shortcut(event, 'r')) { state->reload(); state->message = "Vista actualizada."; return true; }
        if (shortcut(event, 'v')) { state->quick_view = state->quick_view == TaskQuickView::archived ? TaskQuickView::all : TaskQuickView::archived; state->reload(false); return true; }
        constexpr TaskQuickView views[]{TaskQuickView::all, TaskQuickView::today, TaskQuickView::overdue,
                                        TaskQuickView::upcoming, TaskQuickView::blocked,
                                        TaskQuickView::recently_completed, TaskQuickView::archived};
        for (int index = 0; index < 7; ++index) {
            if (event == Event::Character(static_cast<char>('1' + index))) {
                state->quick_view = views[static_cast<std::size_t>(index)];
                state->selected = 0;
                state->reload(false);
                return true;
            }
        }
        return true;
    });
}

}  // namespace modra
