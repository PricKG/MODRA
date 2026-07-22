#include "ui/KnowledgeScreen.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include "application/NoteService.h"
#include "application/ProjectService.h"
#include "application/TaskService.h"
#include "domain/Note.h"
#include "domain/Project.h"
#include "domain/Task.h"
#include "ui/KeyEvent.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

enum class KnowledgeView { list, detail, form, archive_confirmation };

std::string shortened(const std::string& value, std::size_t width) {
    if (value.size() <= width) return value;
    if (width <= 3) return value.substr(0, width);
    return value.substr(0, width - 3) + "...";
}

struct KnowledgeState {
    KnowledgeState(NoteService& note_service,
                   ProjectService& project_service,
                   TaskService& task_service,
                   std::function<std::optional<KnowledgeRequest>()> request_provider,
                   std::function<void(std::int64_t)> open_project,
                   std::function<void(std::int64_t)> open_task,
                   std::function<void()> back,
                   std::function<void(const std::function<void()>&)> restored_io)
        : notes(note_service), projects(project_service), tasks(task_service), requested(std::move(request_provider)),
          on_project(std::move(open_project)), on_task(std::move(open_task)), on_back(std::move(back)),
          with_restored_io(std::move(restored_io)) {}

    void load_relations() {
        relation_projects = projects.list_active();
        const auto archived_projects = projects.list_archived();
        relation_projects.insert(relation_projects.end(), archived_projects.begin(), archived_projects.end());
        project_entries = {"Sin proyecto"};
        for (const auto& project : relation_projects) {
            project_entries.push_back(project.name + " (" + project.alias + ")" +
                                      (project.status == ProjectStatus::archived ? " [archivado]" : ""));
        }

        TaskQuery active_query;
        const std::string today = TaskService::current_local_date();
        relation_tasks = tasks.list_global(active_query, today);
        active_query.view = TaskQuickView::archived;
        auto archived_tasks = tasks.list_global(active_query, today);
        std::set<std::int64_t> known;
        for (const auto& summary : relation_tasks) known.insert(summary.task.id);
        for (const auto& summary : archived_tasks) {
            if (known.insert(summary.task.id).second) relation_tasks.push_back(summary);
        }
        task_entries = {"Sin tarea"};
        for (const auto& summary : relation_tasks) {
            task_entries.push_back(summary.project_alias + " · " + summary.task.title +
                                   (summary.task.archived_at || summary.project_archived ? " [archivada]" : ""));
        }

        filter_project_entries = {"Todos", "Globales"};
        for (const auto& project : relation_projects)
            filter_project_entries.push_back(project.name + " (" + project.alias + ")");
    }

    NoteQuery query() const {
        NoteQuery result;
        result.archived = showing_archived;
        result.search = search_query;
        if (type_filter > 0) result.type = note_types[static_cast<std::size_t>(type_filter - 1)];
        if (project_filter == 1) result.only_global = true;
        if (project_filter > 1)
            result.project_id = relation_projects[static_cast<std::size_t>(project_filter - 2)].id;
        result.task_id = task_filter;
        result.only_favorites = scope_filter == 1;
        result.with_task = scope_filter == 2;
        result.without_task = scope_filter == 3;
        return result;
    }

    void reload() {
        try {
            load_relations();
            visible_notes = notes.list(query());
            selected = visible_notes.empty() ? 0 : std::clamp(selected, 0, static_cast<int>(visible_notes.size()) - 1);
            error.clear();
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    const NoteSummary* selected_note() const {
        if (visible_notes.empty() || selected < 0 || selected >= static_cast<int>(visible_notes.size())) return nullptr;
        return &visible_notes[static_cast<std::size_t>(selected)];
    }

    void select_relations(const Note& note) {
        project_selected = 0;
        task_selected = 0;
        if (note.project_id) {
            for (std::size_t index = 0; index < relation_projects.size(); ++index)
                if (relation_projects[index].id == *note.project_id) project_selected = static_cast<int>(index + 1);
        }
        if (note.task_id) {
            for (std::size_t index = 0; index < relation_tasks.size(); ++index)
                if (relation_tasks[index].task.id == *note.task_id) task_selected = static_cast<int>(index + 1);
        }
    }

    void begin_create(std::optional<std::int64_t> project_id = {}, std::optional<std::int64_t> task_id = {}) {
        editing = false;
        editing_id = 0;
        form_title.clear();
        form_type = 0;
        form_favorite = false;
        form_content.clear();
        Note relation_note;
        relation_note.project_id = project_id;
        relation_note.task_id = task_id;
        if (task_id) {
            for (const auto& summary : relation_tasks)
                if (summary.task.id == *task_id) relation_note.project_id = summary.task.project_id;
        }
        select_relations(relation_note);
        error.clear();
        view = KnowledgeView::form;
    }

    void begin_edit(const Note& note) {
        editing = true;
        editing_id = note.id;
        form_title = note.title;
        form_type = static_cast<int>(note.type);
        form_favorite = note.is_favorite;
        form_content = note.content;
        select_relations(note);
        error.clear();
        view = KnowledgeView::form;
    }

    NoteInput form_input(const std::string& title, const std::string& content) const {
        NoteInput input;
        input.title = title;
        input.type = note_types[static_cast<std::size_t>(form_type)];
        input.content = content;
        if (project_selected > 0)
            input.project_id = relation_projects[static_cast<std::size_t>(project_selected - 1)].id;
        if (task_selected > 0)
            input.task_id = relation_tasks[static_cast<std::size_t>(task_selected - 1)].task.id;
        input.is_favorite = form_favorite;
        return input;
    }

    void save_with_editor() {
        if (form_title.find_first_not_of(" \t\r\n") == std::string::npos) {
            error = "El título es obligatorio.";
            return;
        }
        std::optional<EditedNoteDocument> edited;
        try {
            with_restored_io([&] { edited = notes.edit_external(form_title, editing ? form_content : ""); });
            const Note saved = editing
                                   ? notes.update(editing_id, form_input(edited->document.title, edited->document.body))
                                   : notes.create(form_input(edited->document.title, edited->document.body));
            notes.complete_external_edit(edited->temporary_file);
            message = editing ? "Nota actualizada." : "Nota creada.";
            form_title = saved.title;
            form_content = saved.content;
            reload();
            current = notes.find_summary_by_id(saved.id);
            view = KnowledgeView::detail;
            content_offset = 0;
        } catch (const std::exception& exception) {
            error = exception.what();
            if (edited && error.find(edited->temporary_file.string()) == std::string::npos) {
                error += " Archivo temporal conservado en: " + edited->temporary_file.string();
            }
        }
    }

    void edit_content() {
        if (!current) return;
        std::optional<EditedNoteDocument> edited;
        try {
            with_restored_io([&] { edited = notes.edit_external(current->note.title, current->note.content); });
            NoteInput input{edited->document.title, current->note.type, edited->document.body, current->note.project_id,
                            current->note.task_id, current->note.is_favorite};
            notes.update(current->note.id, std::move(input));
            notes.complete_external_edit(edited->temporary_file);
            current = notes.find_summary_by_id(current->note.id);
            reload();
            message = "Nota actualizada.";
            error.clear();
        } catch (const std::exception& exception) {
            error = exception.what();
            if (edited && error.find(edited->temporary_file.string()) == std::string::npos) {
                error += " Archivo temporal conservado en: " + edited->temporary_file.string();
            }
        }
    }

    void toggle_favorite(const NoteSummary& summary) {
        try {
            notes.set_favorite(summary.note.id, !summary.note.is_favorite);
            current = notes.find_summary_by_id(summary.note.id);
            reload();
            message = summary.note.is_favorite ? "Nota quitada de favoritas." : "Nota marcada como favorita.";
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void confirm_archive() {
        if (!current) return;
        try {
            notes.archive(current->note.id);
            current.reset();
            reload();
            message = "Nota archivada.";
            view = KnowledgeView::list;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void restore_note(const NoteSummary& summary) {
        try {
            notes.restore(summary.note.id);
            current.reset();
            reload();
            message = "Nota desarchivada.";
            error.clear();
            view = KnowledgeView::list;
        } catch (const std::exception& exception) {
            error = exception.what();
        }
    }

    void sync_request() {
        const auto request = requested();
        if (!request) return;
        reload();
        if (request->create) {
            begin_create(request->project_id, request->task_id);
        } else if (request->note_id) {
            current = notes.find_summary_by_id(*request->note_id);
            if (current) {
                view = KnowledgeView::detail;
                showing_archived = current->note.archived_at.has_value();
            }
        } else {
            type_filter = 0;
            project_filter = 0;
            scope_filter = 0;
            task_filter.reset();
            search_query.clear();
            if (request->project_id) {
                for (std::size_t index = 0; index < relation_projects.size(); ++index)
                    if (relation_projects[index].id == *request->project_id) project_filter = static_cast<int>(index + 2);
            }
            if (request->task_id) {
                task_filter = request->task_id;
            }
            reload();
            view = KnowledgeView::list;
        }
    }

    std::string filters_label() const {
        std::string value = "Tipo: " + type_filter_entries[static_cast<std::size_t>(type_filter)] +
                            " · Proyecto: " + filter_project_entries[static_cast<std::size_t>(project_filter)] +
                            " · Alcance: " + scope_entries[static_cast<std::size_t>(scope_filter)];
        if (task_filter) value += " · Tarea vinculada";
        return value;
    }

    NoteService& notes;
    ProjectService& projects;
    TaskService& tasks;
    std::function<std::optional<KnowledgeRequest>()> requested;
    std::function<void(std::int64_t)> on_project;
    std::function<void(std::int64_t)> on_task;
    std::function<void()> on_back;
    std::function<void(const std::function<void()>&)> with_restored_io;
    KnowledgeView view = KnowledgeView::list;
    bool showing_archived = false;
    bool searching = false;
    bool help_visible = false;
    bool editing = false;
    int control_tab = 0;
    int selected = 0;
    int type_filter = 0;
    int project_filter = 0;
    int scope_filter = 0;
    int form_type = 0;
    int project_selected = 0;
    int task_selected = 0;
    int content_offset = 0;
    std::int64_t editing_id = 0;
    std::optional<std::int64_t> task_filter;
    std::string search_query;
    std::string form_title;
    std::string form_content;
    bool form_favorite = false;
    std::string error;
    std::string message;
    std::vector<NoteSummary> visible_notes;
    std::optional<NoteSummary> current;
    std::vector<Project> relation_projects;
    std::vector<TaskSummary> relation_tasks;
    std::vector<std::string> project_entries{"Sin proyecto"};
    std::vector<std::string> task_entries{"Sin tarea"};
    std::vector<std::string> filter_project_entries{"Todos", "Globales"};
    const std::vector<NoteType> note_types{NoteType::general, NoteType::technical, NoteType::solution,
                                           NoteType::meeting, NoteType::sql, NoteType::procedure,
                                           NoteType::configuration, NoteType::reference};
    const std::vector<std::string> type_entries{"General", "Técnica", "Solución", "Reunión", "SQL",
                                                 "Procedimiento", "Configuración", "Referencia"};
    const std::vector<std::string> type_filter_entries{"Todos", "General", "Técnica", "Solución", "Reunión",
                                                        "SQL", "Procedimiento", "Configuración", "Referencia"};
    const std::vector<std::string> scope_entries{"Todas", "Favoritas", "Con tarea", "Sin tarea"};
};

ftxui::Element help_panel() {
    using namespace ftxui;
    return window(text(" AYUDA · CONOCIMIENTO ") | bold | color(Color::Cyan),
                  vbox({text("↑/↓ navegar · Enter detalle · n nueva · e editar · f favorita"),
                        text("a archivar · u desarchivar · v activas/archivadas · / buscar · r recargar"),
                        text("t tipo · p proyecto/global · g alcance · c limpiar filtros"),
                        text("Detalle: o editor · p proyecto · t tarea · Esc/q volver"), separator(),
                        text("Ctrl+S abre título y cuerpo como un único documento Markdown.") | dim,
                        text("Presioná ?, Esc o q para cerrar") | dim | center})) |
           size(WIDTH, EQUAL, 78);
}

}  // namespace

ftxui::Component create_knowledge_screen(
    NoteService& notes,
    ProjectService& projects,
    TaskService& tasks,
    std::function<std::optional<KnowledgeRequest>()> requested,
    std::function<void(std::int64_t)> on_project,
    std::function<void(std::int64_t)> on_task,
    std::function<void()> on_back,
    std::function<void(const std::function<void()>&)> with_restored_io) {
    using namespace ftxui;
    auto state = std::make_shared<KnowledgeState>(notes, projects, tasks, std::move(requested), std::move(on_project),
                                                  std::move(on_task), std::move(on_back), std::move(with_restored_io));
    state->reload();

    InputOption search_options;
    search_options.multiline = false;
    search_options.on_change = [state] { state->reload(); };
    auto search_input = Input(&state->search_query, "título, contenido o relación", search_options);
    InputOption title_options;
    title_options.multiline = false;
    auto title_input = Input(&state->form_title, "Título obligatorio", title_options);
    auto type_dropdown = Dropdown(&state->type_entries, &state->form_type);
    auto project_dropdown = Dropdown(&state->project_entries, &state->project_selected);
    auto task_dropdown = Dropdown(&state->task_entries, &state->task_selected);
    auto favorite_checkbox = Checkbox("Favorita", &state->form_favorite);
    auto form = Container::Vertical({title_input, type_dropdown, project_dropdown, task_dropdown, favorite_checkbox});
    auto controls = Container::Tab({search_input, form}, &state->control_tab);

    auto renderer = Renderer(controls, [=] {
        state->sync_request();
        Element body;
        std::string footer;
        if (state->view == KnowledgeView::list) {
            Elements rows;
            if (state->visible_notes.empty()) {
                const bool filtered = !state->search_query.empty() || state->type_filter != 0 ||
                                      state->project_filter != 0 || state->scope_filter != 0;
                if (filtered) rows.push_back(text("No se encontraron notas con los filtros actuales.") | center | dim);
                else if (state->showing_archived) rows.push_back(text("No hay notas archivadas.") | center | dim);
                else rows.push_back(vbox({text("Todavía no hay notas guardadas.") | center,
                                          text("Presioná n para crear la primera.") | center | dim}));
            } else {
                rows.push_back(hbox({text(" F") | bold | size(WIDTH, EQUAL, 3),
                                     text("Título") | bold | size(WIDTH, EQUAL, 25),
                                     text("Tipo") | bold | size(WIDTH, EQUAL, 15),
                                     text("Proyecto") | bold | size(WIDTH, EQUAL, 18),
                                     text("Tarea") | bold | size(WIDTH, EQUAL, 20), text("Modificada") | bold}) |
                               color(Color::Cyan));
                rows.push_back(separator());
                for (std::size_t index = 0; index < state->visible_notes.size(); ++index) {
                    const auto& summary = state->visible_notes[index];
                    auto row = hbox({text(summary.note.is_favorite ? " * " : "   ") | size(WIDTH, EQUAL, 3),
                                     text(shortened(summary.note.title, 23)) | size(WIDTH, EQUAL, 25),
                                     text(std::string(note_type_label(summary.note.type))) | size(WIDTH, EQUAL, 15),
                                     text(shortened(summary.project_alias.value_or("Global"), 16)) |
                                         size(WIDTH, EQUAL, 18),
                                     text(shortened(summary.task_title.value_or("—"), 18)) | size(WIDTH, EQUAL, 20),
                                     text(summary.note.updated_at.substr(0, 10))});
                    if (static_cast<int>(index) == state->selected) row = row | inverted;
                    rows.push_back(row);
                }
            }
            Elements content{hbox({text(state->showing_archived ? " NOTAS ARCHIVADAS " : " BASE DE CONOCIMIENTO ") |
                                        bold | color(Color::Cyan),
                                    filler(), text(std::to_string(state->visible_notes.size()) + " nota(s) ") | dim}),
                             separator(), text(" " + state->filters_label()) | dim};
            if (state->searching || !state->search_query.empty())
                content.push_back(hbox({text(" Buscar: ") | bold, search_input->Render() | flex}));
            content.push_back(separator());
            content.push_back(vbox(std::move(rows)) | frame | flex);
            if (!state->message.empty()) content.push_back(text(" " + state->message) | color(Color::Green));
            if (!state->error.empty()) content.push_back(text(" " + state->error) | color(Color::Red));
            body = vbox(std::move(content)) | border | flex;
            footer = state->showing_archived
                         ? "↑/↓ navegar  Enter detalle  u desarchivar  v vista  / buscar  ? ayuda"
                         : "↑/↓ navegar  Enter detalle  n nueva  e editar  f favorita  a archivar  v vista  / buscar  ? ayuda";
        } else if (state->view == KnowledgeView::detail && state->current) {
            const auto& summary = *state->current;
            std::istringstream stream(summary.note.content);
            std::vector<std::string> lines;
            std::string line;
            while (std::getline(stream, line)) lines.push_back(line);
            Elements rendered;
            const int first = std::clamp(state->content_offset, 0, std::max(0, static_cast<int>(lines.size()) - 1));
            for (std::size_t index = static_cast<std::size_t>(first); index < lines.size(); ++index)
                rendered.push_back(text(lines[index]));
            if (rendered.empty()) rendered.push_back(text("—") | dim);
            body = vbox({hbox({text(" NOTA ") | bold | color(Color::Cyan), filler(),
                               text(summary.note.is_favorite ? "* Favorita " : "No favorita ") | bold}),
                         separator(), hbox({text("Título:   ") | bold, text(summary.note.title)}),
                         hbox({text("Tipo:     ") | bold, text(std::string(note_type_label(summary.note.type)))}),
                         hbox({text("Proyecto: ") | bold,
                               text(summary.project_name.value_or("Global") +
                                    (summary.project_alias ? " (" + *summary.project_alias + ")" : ""))}),
                         hbox({text("Tarea:    ") | bold, text(summary.task_title.value_or("—"))}), separator(),
                         vbox(std::move(rendered)) | frame | flex | border,
                         text("Creada: " + summary.note.created_at) | dim,
                         text("Modificada: " + summary.note.updated_at) | dim,
                         summary.note.archived_at ? text("Archivada: " + *summary.note.archived_at) | dim : text(""),
                         !state->message.empty() ? text(state->message) | color(Color::Green) : text(""),
                         !state->error.empty() ? text(state->error) | color(Color::Red) : text("")}) |
                   border | flex;
            footer = summary.note.archived_at ? "u desarchivar  o editor  e metadatos  f favorita  p proyecto  t tarea  Esc/q volver"
                                              : "o editor  e editar  f favorita  a archivar  p proyecto  t tarea  Esc/q volver";
        } else if (state->view == KnowledgeView::form) {
            body = vbox({text(state->editing ? " EDITAR NOTA " : " NUEVA NOTA ") | bold | color(Color::Cyan),
                         separator(), hbox({text("Título:   ") | size(WIDTH, EQUAL, 13), title_input->Render() | flex}),
                         hbox({text("Tipo:     ") | size(WIDTH, EQUAL, 13), type_dropdown->Render() | flex}),
                         hbox({text("Proyecto: ") | size(WIDTH, EQUAL, 13), project_dropdown->Render() | flex}),
                         hbox({text("Tarea:    ") | size(WIDTH, EQUAL, 13), task_dropdown->Render() | flex}),
                         favorite_checkbox->Render(), separator(),
                         text("El título inicial prepara la plantilla; podés cambiarlo dentro del editor.") | dim,
                         text("Ctrl+S abrirá título y cuerpo como un único documento Markdown.") | dim,
                         !state->error.empty() ? text("Error: " + state->error) | color(Color::Red) : text(""), filler()}) |
                   border | flex;
            footer = "Tab cambiar campo  Ctrl+S editar documento y guardar  Esc cancelar  ? ayuda";
        } else if (state->view == KnowledgeView::archive_confirmation && state->current) {
            body = vbox({filler(), window(text(" CONFIRMAR ARCHIVADO ") | bold | color(Color::Yellow),
                                          vbox({text("Se archivará la nota:") | center,
                                                text(state->current->note.title) | bold | center, separator(),
                                                text("Enter o s confirma; Esc, q o n cancela.") | dim | center})) |
                                       size(WIDTH, EQUAL, 64),
                         filler()}) |
                   flex;
            footer = "Enter/s confirmar  Esc/q/n cancelar";
        } else {
            body = text("No se pudo mostrar la nota.") | center | border | flex;
            footer = "Esc/q volver";
        }
        auto base = vbox({hbox({text(" MODRA · CONOCIMIENTO ") | bold | color(Color::Cyan), filler(),
                                text(state->showing_archived ? "archivadas " : "activas ") | dim}) |
                              border,
                          body, text(" " + footer + " ") | border});
        return state->help_visible ? dbox({base, help_panel() | clear_under | center}) : base;
    });

    return CatchEvent(renderer, [=](Event event) {
        state->sync_request();
        if (event == Event::Custom) {
            state->reload();
            if (state->current) state->current = state->notes.find_summary_by_id(state->current->note.id);
            return true;
        }
        if (state->help_visible) {
            if (event == Event::Character('?') || event == Event::Escape || shortcut(event, 'q'))
                state->help_visible = false;
            return true;
        }
        if (event == Event::Character('?')) { state->help_visible = true; return true; }
        if (state->view == KnowledgeView::form) {
            if (event == Event::Escape) {
                state->view = state->editing ? KnowledgeView::detail : KnowledgeView::list;
                state->error.clear();
                return true;
            }
            if (event == Event::CtrlS) { state->save_with_editor(); return true; }
            return false;
        }
        if (state->view == KnowledgeView::archive_confirmation) {
            if (event == Event::Return || shortcut(event, 's')) state->confirm_archive();
            else if (event == Event::Escape || shortcut(event, 'q') || shortcut(event, 'n'))
                state->view = KnowledgeView::detail;
            return true;
        }
        if (state->view == KnowledgeView::detail) {
            if (event == Event::Escape || shortcut(event, 'q')) {
                state->view = KnowledgeView::list;
                state->current.reset();
            } else if (shortcut(event, 'e') && state->current) {
                state->begin_edit(state->current->note); state->control_tab = 1; title_input->TakeFocus();
            } else if (shortcut(event, 'o')) state->edit_content();
            else if (shortcut(event, 'f') && state->current) state->toggle_favorite(*state->current);
            else if (shortcut(event, 'a') && state->current && !state->current->note.archived_at)
                state->view = KnowledgeView::archive_confirmation;
            else if (shortcut(event, 'u') && state->current && state->current->note.archived_at)
                state->restore_note(*state->current);
            else if (shortcut(event, 'p') && state->current && state->current->note.project_id)
                state->on_project(*state->current->note.project_id);
            else if (shortcut(event, 't') && state->current && state->current->note.task_id)
                state->on_task(*state->current->note.task_id);
            else if (shortcut(event, 'r') && state->current)
                state->current = state->notes.find_summary_by_id(state->current->note.id);
            else if (event == Event::ArrowDown) ++state->content_offset;
            else if (event == Event::ArrowUp)
                state->content_offset = std::max(0, state->content_offset - 1);
            return true;
        }
        if (state->searching) {
            if (event == Event::Escape || event == Event::Return) state->searching = false;
            else return false;
            return true;
        }
        if (event == Event::Escape || shortcut(event, 'q')) { state->on_back(); return true; }
        if (event == Event::ArrowDown) {
            if (!state->visible_notes.empty())
                state->selected = std::min(state->selected + 1, static_cast<int>(state->visible_notes.size()) - 1);
        } else if (event == Event::ArrowUp) state->selected = std::max(0, state->selected - 1);
        else if (event == Event::Return) {
            if (const auto* note = state->selected_note()) { state->current = *note; state->view = KnowledgeView::detail; }
        } else if (shortcut(event, 'n') && !state->showing_archived) {
            state->begin_create(); state->control_tab = 1; title_input->TakeFocus();
        } else if (shortcut(event, 'e')) {
            if (const auto* note = state->selected_note()) { state->current = *note; state->begin_edit(note->note); state->control_tab = 1; title_input->TakeFocus(); }
        } else if (shortcut(event, 'f')) {
            if (const auto* note = state->selected_note()) state->toggle_favorite(*note);
        } else if (shortcut(event, 'a') && !state->showing_archived) {
            if (const auto* note = state->selected_note()) { state->current = *note; state->view = KnowledgeView::archive_confirmation; }
        } else if (shortcut(event, 'u') && state->showing_archived) {
            if (const auto* note = state->selected_note()) state->restore_note(*note);
        } else if (shortcut(event, 'v')) {
            state->showing_archived = !state->showing_archived; state->selected = 0; state->reload();
        } else if (event == Event::Character('/')) {
            state->searching = true; state->control_tab = 0; search_input->TakeFocus();
        } else if (shortcut(event, 't')) {
            state->type_filter = (state->type_filter + 1) % static_cast<int>(state->type_filter_entries.size()); state->reload();
        } else if (shortcut(event, 'p')) {
            state->project_filter = (state->project_filter + 1) % static_cast<int>(state->filter_project_entries.size()); state->reload();
        } else if (shortcut(event, 'g')) {
            state->scope_filter = (state->scope_filter + 1) % static_cast<int>(state->scope_entries.size()); state->reload();
        } else if (shortcut(event, 'c')) {
            state->type_filter = 0; state->project_filter = 0; state->scope_filter = 0; state->task_filter.reset(); state->search_query.clear(); state->reload();
        } else if (shortcut(event, 'r')) state->reload();
        return true;
    });
}

}  // namespace modra
