#pragma once

#include <string>

#include <ftxui/component/component_base.hpp>

#include "infrastructure/config/DataDirectory.h"

namespace modra {

ftxui::Component create_configuration_screen(DataPaths paths, std::string sqlite_version);

}  // namespace modra
