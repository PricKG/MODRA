#pragma once

#include <string>
#include <vector>

namespace modra {

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

ProcessResult run_process_capture(const std::vector<std::string>& arguments);

}  // namespace modra
