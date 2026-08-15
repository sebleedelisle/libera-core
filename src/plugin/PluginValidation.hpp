#pragma once

#include "libera/plugin/libera_plugin.h"

#include <filesystem>
#include <string>

namespace libera::plugin {

bool isSharedLibraryPath(const std::filesystem::path& path);

std::string validatePluginApi(const libera_plugin_api_t* api);

} // namespace libera::plugin
