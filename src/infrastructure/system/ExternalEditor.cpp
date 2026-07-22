#include "infrastructure/system/ExternalEditor.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "infrastructure/system/Environment.h"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace modra {
namespace {

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') return std::nullopt;
    return std::string(value);
}

int run_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) throw std::runtime_error("No se configuró un comando de editor válido.");
#ifdef _WIN32
    std::vector<std::wstring> wide_arguments;
    wide_arguments.reserve(arguments.size());
    for (const auto& argument : arguments) wide_arguments.push_back(std::filesystem::path(argument).wstring());
    std::vector<const wchar_t*> argv;
    argv.reserve(wide_arguments.size() + 1);
    for (const auto& argument : wide_arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);
    const intptr_t result = _wspawnvp(_P_WAIT, argv.front(), argv.data());
    if (result == -1) throw std::runtime_error("No se pudo iniciar el editor externo.");
    return static_cast<int>(result);
#else
    const pid_t child = fork();
    if (child < 0) throw std::runtime_error("No se pudo iniciar el editor externo.");
    if (child == 0) {
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(127);
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0) throw std::runtime_error("No se pudo esperar al editor externo.");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

}  // namespace

ExternalEditor::ExternalEditor(std::filesystem::path config_path, std::filesystem::path temporary_directory)
    : config_path_(std::move(config_path)), temporary_directory_(std::move(temporary_directory)) {}

std::string ExternalEditor::resolve_command(const std::filesystem::path& config_path,
                                            const EditorEnvironment& environment) {
    if (std::filesystem::exists(config_path)) {
        std::ifstream input(config_path);
        if (!input) throw std::runtime_error("No se pudo leer la configuración del editor.");
        try {
            const auto config = nlohmann::json::parse(input);
            if (config.contains("editor") && config["editor"].is_object() &&
                config["editor"].contains("command") && config["editor"]["command"].is_string()) {
                const std::string configured = config["editor"]["command"].get<std::string>();
                if (configured.find_first_not_of(" \t\r\n") != std::string::npos) return configured;
            }
        } catch (const nlohmann::json::exception& exception) {
            throw std::runtime_error("config.json no contiene JSON válido: " + std::string(exception.what()));
        }
    }
    if (environment.visual && environment.visual->find_first_not_of(" \t\r\n") != std::string::npos)
        return *environment.visual;
    if (environment.editor && environment.editor->find_first_not_of(" \t\r\n") != std::string::npos)
        return *environment.editor;
    if (environment.windows) return "notepad.exe";
    if (command_is_available("nano")) return "nano";
    if (command_is_available("vi")) return "vi";
    throw std::runtime_error("No se encontró un editor. Configurá editor.command, VISUAL o EDITOR.");
}

std::vector<std::string> ExternalEditor::split_command(const std::string& command) {
    std::vector<std::string> arguments;
    std::string current;
    char quote = '\0';
    for (std::size_t index = 0; index < command.size(); ++index) {
        const char character = command[index];
        if (quote != '\0') {
            if (character == quote) {
                quote = '\0';
            } else if (character == '\\' && index + 1 < command.size() && command[index + 1] == quote) {
                current.push_back(command[++index]);
            } else {
                current.push_back(character);
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == ' ' || character == '\t') {
            if (!current.empty()) {
                arguments.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(character);
        }
    }
    if (quote != '\0') throw std::runtime_error("El comando del editor contiene comillas sin cerrar.");
    if (!current.empty()) arguments.push_back(current);
    if (arguments.empty()) throw std::runtime_error("No se configuró un comando de editor válido.");
    return arguments;
}

ExternalEditResult ExternalEditor::edit(const std::string& title, const std::string& initial_content) const {
    std::filesystem::create_directories(temporary_directory_);
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary_file = temporary_directory_ / (".modra-note-" + std::to_string(suffix) + ".md");

    try {
        {
            std::ofstream output(temporary_file, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("No se pudo crear el archivo temporal de la nota.");
            output.write(initial_content.data(), static_cast<std::streamsize>(initial_content.size()));
            if (!output) throw std::runtime_error("No se pudo escribir el archivo temporal de la nota.");
        }

#ifdef _WIN32
        constexpr bool is_windows = true;
#else
        constexpr bool is_windows = false;
#endif
        const std::string command = resolve_command(
            config_path_, {environment_value("VISUAL"), environment_value("EDITOR"), is_windows});
        auto arguments = split_command(command);
        arguments.push_back(temporary_file.string());
        spdlog::info("Opening external editor for note title length={}", title.size());
        const int result = run_process(arguments);
        if (result != 0) {
            spdlog::error("External editor failed with exit code {}", result);
            throw std::runtime_error("El editor externo terminó con error. La nota original no fue modificada.");
        }

        std::ifstream input(temporary_file, std::ios::binary);
        if (!input) throw std::runtime_error("No se pudo leer el contenido devuelto por el editor.");
        return {std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()), temporary_file};
    } catch (const std::exception& exception) {
        if (std::filesystem::exists(temporary_file)) {
            throw std::runtime_error(std::string(exception.what()) +
                                     " Archivo temporal conservado en: " + temporary_file.string());
        }
        throw;
    }
}

void ExternalEditor::remove_temporary(const std::filesystem::path& path) const {
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error || (!removed && std::filesystem::exists(path))) {
        spdlog::warn("Could not remove note temporary file: {}", path.string());
    }
}

}  // namespace modra
