#pragma once

#include <string>

namespace modra {

std::string operating_system_name();
bool command_is_available(const std::string& command);

}  // namespace modra
