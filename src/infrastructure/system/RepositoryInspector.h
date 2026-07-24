#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace modra {

enum class RepositoryKind {
    none,
    git,
    svn,
};

struct ToolInformation {
    bool available = false;
    std::string version;
};

struct RepositoryStatus {
    bool path_exists = false;
    RepositoryKind kind = RepositoryKind::none;
    bool tool_available = false;
    std::string branch;
    std::size_t staged = 0;
    std::size_t modified = 0;
    std::size_t untracked = 0;
    std::size_t ahead = 0;
    std::size_t behind = 0;
    std::string error;

    bool has_changes() const;
    bool needs_attention() const;
};

ToolInformation inspect_tool(const std::string& command);
RepositoryStatus inspect_repository(const std::filesystem::path& path,
                                    bool git_available,
                                    bool svn_available);

}  // namespace modra
