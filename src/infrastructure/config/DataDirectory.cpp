#include "infrastructure/config/DataDirectory.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace modra {
namespace {

std::optional<std::string> environment_value(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

}  // namespace

std::filesystem::path resolve_data_directory(const DataDirectoryEnvironment& environment) {
    switch (environment.operating_system) {
        case OperatingSystem::windows:
            if (!environment.local_app_data) {
                throw std::runtime_error("LOCALAPPDATA is not configured");
            }
            return std::filesystem::path(*environment.local_app_data) / "MODRA";
        case OperatingSystem::macos:
            if (!environment.home) {
                throw std::runtime_error("HOME is not configured");
            }
            return std::filesystem::path(*environment.home) / "Library" / "Application Support" / "MODRA";
        case OperatingSystem::linux:
            if (environment.xdg_data_home) {
                return std::filesystem::path(*environment.xdg_data_home) / "modra";
            }
            if (!environment.home) {
                throw std::runtime_error("Neither XDG_DATA_HOME nor HOME is configured");
            }
            return std::filesystem::path(*environment.home) / ".local" / "share" / "modra";
    }
    throw std::runtime_error("Unsupported operating system");
}

DataPaths current_data_paths() {
#if defined(_WIN32)
    const OperatingSystem operating_system = OperatingSystem::windows;
#elif defined(__APPLE__)
    const OperatingSystem operating_system = OperatingSystem::macos;
#else
    const OperatingSystem operating_system = OperatingSystem::linux;
#endif

    const auto root = resolve_data_directory({
        operating_system,
        environment_value("LOCALAPPDATA"),
        environment_value("XDG_DATA_HOME"),
        environment_value("HOME"),
    });
    return {
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };
}

DataPaths initialize_data_directory(const std::filesystem::path& root) {
    DataPaths paths{
        root,
        root / "modra.db",
        root / "config.json",
        root / "backups",
        root / "exports",
        root / "logs",
    };

    std::filesystem::create_directories(paths.backups);
    std::filesystem::create_directories(paths.exports);
    std::filesystem::create_directories(paths.logs);

    if (!std::filesystem::exists(paths.config)) {
        std::ofstream config(paths.config);
        if (!config) {
            throw std::runtime_error("Could not create config.json at " + paths.config.string());
        }
        config << nlohmann::json{{"version", 1}, {"editor", {{"command", ""}}}}.dump(2) << '\n';
        if (!config) {
            throw std::runtime_error("Could not write config.json at " + paths.config.string());
        }
    } else {
        std::ifstream input(paths.config);
        if (!input) {
            throw std::runtime_error("Could not read config.json at " + paths.config.string());
        }
        try {
            auto config = nlohmann::json::parse(input);
            if (!config.contains("editor") || !config["editor"].is_object() ||
                !config["editor"].contains("command")) {
                config["editor"]["command"] = "";
                std::ofstream output(paths.config, std::ios::trunc);
                if (!output) {
                    throw std::runtime_error("Could not update config.json at " + paths.config.string());
                }
                output << config.dump(2) << '\n';
                if (!output) {
                    throw std::runtime_error("Could not update config.json at " + paths.config.string());
                }
            }
        } catch (const nlohmann::json::exception&) {
            // Preserve an existing invalid file. The editor resolver will report a clear error when it is used.
        }
    }

    return paths;
}

}  // namespace modra
