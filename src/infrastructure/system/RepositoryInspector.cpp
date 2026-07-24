#include "infrastructure/system/RepositoryInspector.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

#include "infrastructure/system/Environment.h"
#include "infrastructure/system/Process.h"

namespace modra {
namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character);
    });
    value.erase(value.begin(), first);
    return value;
}

std::size_t parse_counter(const std::string& value, const std::string& marker) {
    const auto position = value.find(marker);
    if (position == std::string::npos) return 0;
    std::size_t index = position + marker.size();
    std::size_t result = 0;
    while (index < value.size() && std::isdigit(static_cast<unsigned char>(value[index]))) {
        result = result * 10 + static_cast<std::size_t>(value[index] - '0');
        ++index;
    }
    return result;
}

std::string path_argument(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
#else
    return path.string();
#endif
}

void parse_git_status(RepositoryStatus& status, const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    bool header = true;
    while (std::getline(lines, line)) {
        if (header && line.rfind("## ", 0) == 0) {
            std::string branch = line.substr(3);
            constexpr const char* no_commits = "No commits yet on ";
            constexpr const char* initial_commit = "Initial commit on ";
            if (branch.rfind(no_commits, 0) == 0) branch.erase(0, std::char_traits<char>::length(no_commits));
            if (branch.rfind(initial_commit, 0) == 0)
                branch.erase(0, std::char_traits<char>::length(initial_commit));
            const auto remote = branch.find("...");
            const auto metadata = branch.find(" [");
            const auto end = std::min(remote == std::string::npos ? branch.size() : remote,
                                      metadata == std::string::npos ? branch.size() : metadata);
            status.branch = branch.substr(0, end);
            status.ahead = parse_counter(line, "ahead ");
            status.behind = parse_counter(line, "behind ");
            header = false;
            continue;
        }
        header = false;
        if (line.size() < 2) continue;
        const char index_status = line[0];
        const char worktree_status = line[1];
        if (index_status == '?' && worktree_status == '?') {
            ++status.untracked;
            continue;
        }
        if (index_status != ' ' && index_status != '!' && index_status != '?') ++status.staged;
        if (worktree_status != ' ' && worktree_status != '!' && worktree_status != '?') ++status.modified;
    }
}

void parse_svn_status(RepositoryStatus& status, const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        if (line[0] == '?') {
            ++status.untracked;
        } else if (line[0] != ' ' && line[0] != 'X' && line[0] != 'I') {
            ++status.modified;
        } else if (line.size() > 1 && line[1] != ' ') {
            ++status.modified;
        }
    }
}

}  // namespace

bool RepositoryStatus::has_changes() const {
    return staged > 0 || modified > 0 || untracked > 0;
}

bool RepositoryStatus::needs_attention() const {
    return !path_exists || !error.empty() || has_changes() || behind > 0;
}

ToolInformation inspect_tool(const std::string& command) {
    ToolInformation information;
    information.available = command_is_available(command);
    if (!information.available) return information;

    const auto result = command == "svn"
                            ? run_process_capture({command, "--version", "--quiet"})
                            : run_process_capture({command, "--version"});
    if (result.exit_code == 0) information.version = trim(result.output);
    return information;
}

RepositoryStatus inspect_repository(const std::filesystem::path& path,
                                    bool git_available,
                                    bool svn_available) {
    RepositoryStatus status;
    std::error_code filesystem_error;
    status.path_exists = std::filesystem::is_directory(path, filesystem_error);
    if (!status.path_exists) return status;

    const std::string command_path = path_argument(path);
    filesystem_error.clear();
    const bool git_metadata = std::filesystem::exists(path / ".git", filesystem_error);
    const auto git_probe = git_available
                               ? run_process_capture({"git", "-C", command_path, "rev-parse",
                                                      "--is-inside-work-tree"})
                               : ProcessResult{};
    if (git_metadata || (git_available && git_probe.exit_code == 0 &&
                         trim(git_probe.output) == "true")) {
        status.kind = RepositoryKind::git;
        status.tool_available = git_available;
        if (!git_available) {
            status.error = "Git no está disponible para consultar el repositorio.";
            return status;
        }
        const auto result = run_process_capture(
            {"git", "-C", command_path, "status", "--porcelain=v1", "--branch", "--untracked-files=normal"});
        if (result.exit_code != 0) {
            status.error = trim(result.output);
            if (status.error.empty()) status.error = "Git no pudo consultar el repositorio.";
            return status;
        }
        parse_git_status(status, result.output);
        return status;
    }

    filesystem_error.clear();
    const bool svn_metadata = std::filesystem::is_directory(path / ".svn", filesystem_error);
    const auto svn_probe = svn_available
                               ? run_process_capture({"svn", "info", "--show-item", "wc-root", command_path})
                               : ProcessResult{};
    if (svn_metadata || (svn_available && svn_probe.exit_code == 0 &&
                         !trim(svn_probe.output).empty())) {
        status.kind = RepositoryKind::svn;
        status.tool_available = svn_available;
        if (!svn_available) {
            status.error = "SVN no está disponible para consultar el repositorio.";
            return status;
        }
        const auto result = run_process_capture({"svn", "status", "--ignore-externals", command_path});
        if (result.exit_code != 0) {
            status.error = trim(result.output);
            if (status.error.empty()) status.error = "SVN no pudo consultar el repositorio.";
            return status;
        }
        parse_svn_status(status, result.output);
    }
    return status;
}

}  // namespace modra
