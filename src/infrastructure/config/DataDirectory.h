#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace modra {

enum class OperatingSystem {
    windows,
    linux,
    macos,
};

struct DataDirectoryEnvironment {
    OperatingSystem operating_system;
    std::optional<std::string> local_app_data;
    std::optional<std::string> xdg_data_home;
    std::optional<std::string> home;
};

struct DataPaths {
    std::filesystem::path root;
    std::filesystem::path database;
    std::filesystem::path config;
    std::filesystem::path backups;
    std::filesystem::path exports;
    std::filesystem::path logs;
};

std::filesystem::path resolve_data_directory(const DataDirectoryEnvironment& environment);
DataPaths current_data_paths();
DataPaths initialize_data_directory(const std::filesystem::path& root);

}  // namespace modra
