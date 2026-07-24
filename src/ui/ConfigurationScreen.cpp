#include "ui/ConfigurationScreen.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <spdlog/spdlog.h>

#include "application/AppInfo.h"
#include "infrastructure/system/Environment.h"
#include "infrastructure/system/ExternalEditor.h"
#include "ui/KeyEvent.h"

namespace modra {
namespace {

using namespace ftxui;

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') return std::nullopt;
    return std::string(value);
}

Element info_row(const std::string& label, const std::string& value) {
    return hbox({text(label) | bold | size(WIDTH, EQUAL, 22), text(value) | flex});
}

struct EditorChoice {
    std::string label;
    std::string command;
    bool available = false;
    std::string reason;
};

struct ConfigurationState {
    ConfigurationState(DataPaths paths_value, std::string sqlite_version_value)
        : paths(std::move(paths_value)), sqlite_version(std::move(sqlite_version_value)) {
        reload();
    }

    void reload() {
        visual = environment_value("VISUAL");
        editor = environment_value("EDITOR");
        configured_editor = read_configured_editor();
        rebuild_editor_choices();
        try {
#ifdef _WIN32
            constexpr bool is_windows = true;
#else
            constexpr bool is_windows = false;
#endif
            resolved_editor = ExternalEditor::resolve_command(paths.config, {visual, editor, is_windows});
            editor_error.clear();
        } catch (const std::exception& exception) {
            resolved_editor.clear();
            editor_error = exception.what();
            spdlog::warn("Configuration editor resolution error: {}", exception.what());
        }
    }

    bool command_can_start(const std::string& command) const {
        try {
            const auto arguments = ExternalEditor::split_command(command);
            if (arguments.empty()) return false;
            const std::filesystem::path executable(arguments.front());
            if (executable.is_absolute() || arguments.front().find('/') != std::string::npos ||
                arguments.front().find('\\') != std::string::npos) {
                std::error_code filesystem_error;
                return std::filesystem::is_regular_file(executable, filesystem_error);
            }
#ifdef _WIN32
            if (arguments.front() == "notepad.exe") return true;
#endif
            return command_is_available(arguments.front());
        } catch (const std::exception&) {
            return false;
        }
    }

    bool automatic_editor_available() const {
        if (visual && command_can_start(*visual)) return true;
        if (editor && command_can_start(*editor)) return true;
#ifdef _WIN32
        return true;
#else
        return command_is_available("nano") || command_is_available("vi");
#endif
    }

    void add_editor_choice(std::string label, std::string command, bool available, std::string reason = {}) {
        for (const auto& choice : editor_choices) {
            if (choice.command == command) return;
        }
        editor_choices.push_back({std::move(label), std::move(command), available, std::move(reason)});
    }

    void rebuild_editor_choices() {
        editor_choices.clear();
        add_editor_choice("Automático (VISUAL/EDITOR o fallback)", "", automatic_editor_available(),
                          "No se encontró VISUAL, EDITOR, nano, vi ni fallback del sistema.");
        if (visual && visual->find_first_not_of(" \t\r\n") != std::string::npos)
            add_editor_choice("VISUAL: " + *visual, *visual, command_can_start(*visual),
                              "El comando definido en VISUAL no está disponible.");
        if (editor && editor->find_first_not_of(" \t\r\n") != std::string::npos)
            add_editor_choice("EDITOR: " + *editor, *editor, command_can_start(*editor),
                              "El comando definido en EDITOR no está disponible.");
        add_editor_choice("Visual Studio Code", "code --wait", command_is_available("code"),
                          "El comando code no está en PATH.");
        add_editor_choice("Cursor", "cursor --wait", command_is_available("cursor"),
                          "El comando cursor no está en PATH.");
        add_editor_choice("Nano", "nano", command_is_available("nano"), "El comando nano no está en PATH.");
        add_editor_choice("Vim", "vim", command_is_available("vim"), "El comando vim no está en PATH.");
        add_editor_choice("Vi", "vi", command_is_available("vi"), "El comando vi no está en PATH.");
#ifdef _WIN32
        add_editor_choice("Bloc de notas", "notepad.exe", true);
#endif
        if (!configured_editor.empty())
            add_editor_choice("Actual: " + configured_editor, configured_editor, command_can_start(configured_editor),
                              "El editor configurado actualmente no está disponible.");

        editor_selected = 0;
        for (std::size_t index = 0; index < editor_choices.size(); ++index) {
            if (editor_choices[index].command == configured_editor) editor_selected = static_cast<int>(index);
        }
        if (!editor_choices[static_cast<std::size_t>(editor_selected)].available) select_next_available(1);
    }

    std::string read_configured_editor() {
        if (!std::filesystem::exists(paths.config)) return "";
        std::ifstream input(paths.config);
        if (!input) {
            editor_error = "No se pudo leer config.json.";
            return "";
        }
        try {
            const auto config = nlohmann::json::parse(input);
            if (config.contains("editor") && config["editor"].is_object() &&
                config["editor"].contains("command") && config["editor"]["command"].is_string()) {
                return config["editor"]["command"].get<std::string>();
            }
        } catch (const nlohmann::json::exception& exception) {
            editor_error = "config.json no contiene JSON válido: " + std::string(exception.what());
        }
        return "";
    }

    void save_editor() {
        try {
            const auto& selected = editor_choices[static_cast<std::size_t>(editor_selected)];
            if (!selected.available) throw std::runtime_error("La opción seleccionada no está disponible en este sistema.");
            const std::string command = selected.command;
            if (command.find_first_not_of(" \t\r\n") != std::string::npos) {
                (void)ExternalEditor::split_command(command);
            }

            nlohmann::json config = nlohmann::json::object();
            if (std::filesystem::exists(paths.config)) {
                std::ifstream input(paths.config);
                if (!input) throw std::runtime_error("No se pudo leer config.json.");
                config = nlohmann::json::parse(input);
                if (!config.is_object()) config = nlohmann::json::object();
            }
            if (!config.contains("version")) config["version"] = 1;
            if (!config.contains("editor") || !config["editor"].is_object()) config["editor"] = nlohmann::json::object();
            config["editor"]["command"] = command;

            std::ofstream output(paths.config, std::ios::trunc);
            if (!output) throw std::runtime_error("No se pudo escribir config.json.");
            output << config.dump(2) << '\n';
            if (!output) throw std::runtime_error("No se pudo escribir config.json.");
            output.close();
            if (!output) throw std::runtime_error("No se pudo cerrar config.json después de escribirlo.");

            message = command.empty() ? "Editor automático configurado." : "Editor guardado.";
            error.clear();
            reload();
            editor_picker_open = false;
        } catch (const std::exception& exception) {
            error = exception.what();
            message.clear();
        }
    }

    void select_next_available(int direction) {
        if (editor_choices.empty()) return;
        int candidate = editor_selected;
        for (std::size_t step = 0; step < editor_choices.size(); ++step) {
            candidate += direction;
            if (candidate < 0) candidate = static_cast<int>(editor_choices.size()) - 1;
            if (candidate >= static_cast<int>(editor_choices.size())) candidate = 0;
            if (editor_choices[static_cast<std::size_t>(candidate)].available) {
                editor_selected = candidate;
                message.clear();
                error.clear();
                return;
            }
        }
    }

    DataPaths paths;
    std::string sqlite_version;
    std::optional<std::string> visual;
    std::optional<std::string> editor;
    std::string configured_editor;
    std::string resolved_editor;
    std::string editor_error;
    std::string error;
    std::string message;
    std::vector<EditorChoice> editor_choices;
    int editor_selected = 0;
    bool editor_picker_open = false;
};

}  // namespace

ftxui::Component create_configuration_screen(DataPaths paths, std::string sqlite_version) {
    auto state = std::make_shared<ConfigurationState>(std::move(paths), std::move(sqlite_version));
    auto component = Renderer([state] {
        Elements editor_rows{
            info_row("Editor actual", state->resolved_editor.empty() ? "No disponible" : state->resolved_editor),
            info_row("Configuración", state->configured_editor.empty() ? "Selección automática" : "Personalizada"),
            separator(),
            hbox({
                text("  Cambiar editor  ") | bold | inverted,
                text("  Enter") | dim,
            }) | center,
        };
        if (!state->editor_error.empty()) {
            editor_rows.push_back(text("Error: " + state->editor_error) | color(Color::Yellow));
        }
        if (!state->message.empty()) editor_rows.push_back(text(state->message) | color(Color::Green));
        if (!state->error.empty()) editor_rows.push_back(text("Error: " + state->error) | color(Color::Red));

        auto base = vbox({
                        text(" MODRA · Configuración ") | bold | color(Color::Cyan),
                        separator(),
                        window(text(" Editor externo ") | bold, vbox(std::move(editor_rows))),
                        window(text(" Aplicación ") | bold,
                               vbox({
                                   info_row("Versión", std::string(application_version())),
                                   info_row("Sistema operativo", operating_system_name()),
                                   info_row("SQLite", state->sqlite_version),
                               })),
                        window(text(" Datos locales ") | bold,
                               vbox({
                                   info_row("Directorio", state->paths.root.string()),
                                   info_row("Base SQLite", state->paths.database.string()),
                                   info_row("Config", state->paths.config.string()),
                                   info_row("Backups", state->paths.backups.string()),
                                   info_row("Exportaciones", state->paths.exports.string()),
                                   info_row("Logs", state->paths.logs.string()),
                               })),
                        filler(),
                        text("Enter cambia el editor · Ctrl+R refresca · Esc vuelve al menú · ? ayuda") | dim | center,
                    }) |
                    vscroll_indicator | frame | flex;
        if (!state->editor_picker_open) return base;

        Elements choice_rows;
        for (std::size_t index = 0; index < state->editor_choices.size(); ++index) {
            const auto& choice = state->editor_choices[index];
            if (!choice.available) continue;
            const bool selected = static_cast<int>(index) == state->editor_selected;
            const bool configured = choice.command == state->configured_editor;
            auto row = hbox({
                text(selected ? "> " : "  ") | size(WIDTH, EQUAL, 2),
                text(configured ? "● " : "  ") | color(Color::Cyan),
                text(choice.label) | flex,
            });
            if (selected) row = row | inverted;
            choice_rows.push_back(row);
        }
        choice_rows.push_back(separator());
        choice_rows.push_back(text("↑/↓ seleccionar · Enter/Ctrl+S confirmar · Esc cancelar") | dim | center);
        auto picker = window(text(" Elegir editor ") | bold | color(Color::Cyan),
                             vbox(std::move(choice_rows))) |
                      size(WIDTH, EQUAL, 64);
        return dbox({base, picker | clear_under | center});
    });

    return CatchEvent(component, [state](Event event) {
        if (event == Event::Custom) {
            state->reload();
            return true;
        }
        if (state->editor_picker_open) {
            if (event == Event::ArrowDown) {
                state->select_next_available(1);
            } else if (event == Event::ArrowUp) {
                state->select_next_available(-1);
            } else if (event == Event::Return || event == Event::CtrlS) {
                state->save_editor();
            } else if (event == Event::Escape) {
                state->editor_picker_open = false;
                state->error.clear();
            }
            return true;
        }
        if (event == Event::Return) {
            state->editor_picker_open = true;
            state->message.clear();
            state->error.clear();
            return true;
        }
        if (event == Event::CtrlR) {
            state->reload();
            return true;
        }
        return false;
    });
}

}  // namespace modra
