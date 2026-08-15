#pragma once

#include "libera/plugin/PluginRegistry.hpp"

#include <optional>
#include <string>
#include <vector>

namespace libera::plugin {

enum class ManagedPluginState {
    Loaded,
    NotAPlugin,
    FailedLoad,
    FailedValidation,
    FailedBackend,
    PendingRestart,
    RemovedPendingRestart,
};

enum class ManagedPluginSource {
    UserPluginDirectory,
    OtherConfiguredDirectory,
};

struct ManagedPluginInfo {
    std::string path;
    std::string filename;
    ManagedPluginState state = ManagedPluginState::PendingRestart;
    ManagedPluginSource source = ManagedPluginSource::OtherConfiguredDirectory;
    std::string typeName;
    std::string displayName;
    std::optional<std::string> loadError;
    std::vector<PluginRuntimeError> runtimeErrors;
    bool fileExists = false;
    bool canRemove = false;
    bool restartRequired = false;
};

struct PluginRemoveResult {
    bool success = false;
    std::string removedPath;
    std::string message;
    bool restartRequired = false;
};

/*
 * Directory used by the plugin installer/remover.
 *
 * This is the first configured System plugin directory. With the default
 * System configuration it is the shared per-user Libera plugin folder.
 */
const std::string& userPluginDirectory();

/*
 * Merge the runtime registry with files in the user plugin directory.
 *
 * Runtime registry entries report plugins loaded, rejected, or errored during
 * this process. Files that are present on disk but not yet in the registry are
 * reported as PendingRestart because System only loads plugins during startup.
 */
std::vector<ManagedPluginInfo> listManagedPlugins();

/*
 * Install a plugin into userPluginDirectory() after validating the same API
 * rules the runtime loader enforces.
 */
PluginInstallResult installPlugin(const std::string& sourcePath);

/*
 * Remove a plugin file from userPluginDirectory().
 *
 * Native plugin libraries remain loaded by the operating system until process
 * restart, so removing a loaded plugin returns restartRequired=true.
 */
PluginRemoveResult removePlugin(const std::string& pluginPath);

/*
 * Platform extension without a leading dot: "dll", "dylib", or "so".
 */
const char* platformPluginExtension();

bool isPluginLibraryFile(const std::string& path);

} // namespace libera::plugin
