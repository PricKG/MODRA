#include "infrastructure/system/Process.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace modra {
namespace {

#ifdef _WIN32
std::wstring utf8_to_wide(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) throw std::runtime_error("No se pudo preparar un argumento para el proceso.");
    std::wstring converted(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                        converted.data(), size);
    return converted;
}

std::wstring quote_windows_argument(const std::wstring& argument) {
    if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) return argument;

    std::wstring quoted(1, L'"');
    std::size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'"') {
            quoted.append(backslashes * 2 + 1, L'\\');
            quoted.push_back(character);
            backslashes = 0;
            continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}
#endif

}  // namespace

ProcessResult run_process_capture(const std::vector<std::string>& arguments) {
    if (arguments.empty()) throw std::invalid_argument("El proceso requiere un comando.");

#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;
    if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
        throw std::runtime_error("No se pudo preparar la salida del proceso.");
    }
    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        throw std::runtime_error("No se pudo preparar la salida del proceso.");
    }

    std::wstring command_line;
    for (const auto& argument : arguments) {
        if (!command_line.empty()) command_line.push_back(L' ');
        command_line += quote_windows_argument(utf8_to_wide(argument));
    }
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_pipe;
    startup.hStdError = write_pipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(write_pipe);
    if (!started) {
        CloseHandle(read_pipe);
        return {-1, "No se pudo iniciar el proceso."};
    }

    ProcessResult result;
    std::array<char, 4096> buffer{};
    DWORD read = 0;
    while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) && read > 0) {
        result.output.append(buffer.data(), read);
    }
    CloseHandle(read_pipe);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(process.hProcess, &exit_code);
    result.exit_code = static_cast<int>(exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return result;
#else
    int output_pipe[2]{-1, -1};
    if (pipe(output_pipe) != 0) throw std::runtime_error("No se pudo preparar la salida del proceso.");

    const pid_t child = fork();
    if (child < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        throw std::runtime_error("No se pudo iniciar el proceso.");
    }
    if (child == 0) {
        close(output_pipe[0]);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(output_pipe[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        execvp(argv.front(), argv.data());
        _exit(127);
    }

    close(output_pipe[1]);
    ProcessResult result;
    std::array<char, 4096> buffer{};
    ssize_t count = 0;
    while ((count = read(output_pipe[0], buffer.data(), buffer.size())) > 0) {
        result.output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    close(output_pipe[0]);
    int status = 0;
    if (waitpid(child, &status, 0) < 0) throw std::runtime_error("No se pudo esperar al proceso.");
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    return result;
#endif
}

}  // namespace modra
