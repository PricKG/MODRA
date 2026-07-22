#include "application/AppInfo.h"

#include "modra/Version.h"

namespace modra {

std::string_view application_name() {
    return "MODRA";
}

std::string_view application_version() {
    return MODRA_VERSION;
}

}  // namespace modra
