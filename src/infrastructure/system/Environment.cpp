#include "infrastructure/system/Environment.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace modra {

std::string operating_system_name() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#elif defined(__linux__)
    return "Linux";
#else
    return "Unknown";
#endif
}

bool command_is_available(const std::string& command) {
    const char* path_value = std::getenv("PATH");
    if (path_value == nullptr) {
        return false;
    }

#if defined(_WIN32)
    constexpr char separator = ';';
    const std::vector<std::string> extensions{"", ".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    const std::vector<std::string> extensions{""};
#endif

    std::stringstream paths(path_value);
    std::string directory;
    while (std::getline(paths, directory, separator)) {
        if (directory.size() >= 2 && directory.front() == '"' && directory.back() == '"') {
            directory = directory.substr(1, directory.size() - 2);
        }
        for (const auto& extension : extensions) {
            std::error_code error;
            const auto candidate = std::filesystem::path(directory) / (command + extension);
            if (std::filesystem::is_regular_file(candidate, error)) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace modra
