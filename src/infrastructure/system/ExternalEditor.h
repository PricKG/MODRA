#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace modra {

struct EditorEnvironment {
    std::optional<std::string> visual;
    std::optional<std::string> editor;
    bool windows = false;
};

struct ExternalEditResult {
    std::string content;
    std::filesystem::path temporary_file;
};

class ExternalEditor {
public:
    ExternalEditor(std::filesystem::path config_path, std::filesystem::path temporary_directory);

    static std::string resolve_command(const std::filesystem::path& config_path,
                                       const EditorEnvironment& environment);
    static std::vector<std::string> split_command(const std::string& command);
    ExternalEditResult edit(const std::string& title, const std::string& initial_content) const;
    void remove_temporary(const std::filesystem::path& path) const;

private:
    std::filesystem::path config_path_;
    std::filesystem::path temporary_directory_;
};

}  // namespace modra
