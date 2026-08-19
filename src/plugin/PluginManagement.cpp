#include "libera/plugin/PluginManagement.hpp"

#include "libera/System.hpp"
#include "PluginValidation.hpp"

#include <algorithm>
#include <filesystem>
#include <system_error>
#include <unordered_map>

namespace libera::plugin {

namespace fs = std::filesystem;

namespace {

std::string normalizedPathString(const fs::path& path) {
    std::error_code ec;
    auto absolute = path.is_absolute() ? path : fs::absolute(path, ec);
    if (ec) {
        absolute = path;
    }

    auto canonical = fs::weakly_canonical(absolute, ec);
    if (!ec) {
        return canonical.string();
    }

    return absolute.lexically_normal().string();
}

ManagedPluginState toManagedState(PluginState state) {
    switch (state) {
        case PluginState::Loaded:
            return ManagedPluginState::Loaded;
        case PluginState::NotAPlugin:
            return ManagedPluginState::NotAPlugin;
        case PluginState::FailedLoad:
            return ManagedPluginState::FailedLoad;
        case PluginState::FailedValidation:
            return ManagedPluginState::FailedValidation;
        case PluginState::FailedBackend:
            return ManagedPluginState::FailedBackend;
    }
    return ManagedPluginState::FailedLoad;
}

std::string displaySortName(const ManagedPluginInfo& info) {
    if (!info.displayName.empty()) {
        return info.displayName;
    }
    if (!info.filename.empty()) {
        return info.filename;
    }
    return info.path;
}

bool isUserPluginPath(const std::string& path,
                      const std::string& normalizedUserDir) {
    if (normalizedUserDir.empty()) {
        return false;
    }
    return normalizedPathString(fs::path(path).parent_path()) == normalizedUserDir;
}

bool runtimeKnowsPluginPath(const std::string& normalizedPath) {
    for (const auto& info : PluginRegistry::instance().snapshot()) {
        if (normalizedPathString(info.path) == normalizedPath) {
            return true;
        }
    }
    return false;
}

ManagedPluginInfo managedInfoFromRegistry(const PluginInfo& info,
                                          const std::string& normalizedUserDir) {
    ManagedPluginInfo managed;
    managed.path = normalizedPathString(info.path);
    managed.filename = fs::path(managed.path).filename().string();
    managed.typeName = info.typeName;
    managed.displayName = info.displayName;
    managed.loadError = info.loadError;
    managed.runtimeErrors = info.runtimeErrors;

    std::error_code ec;
    managed.fileExists = fs::is_regular_file(managed.path, ec);
    managed.source = isUserPluginPath(managed.path, normalizedUserDir)
        ? ManagedPluginSource::UserPluginDirectory
        : ManagedPluginSource::OtherConfiguredDirectory;

    if (managed.fileExists) {
        managed.state = toManagedState(info.state);
        managed.canRemove =
            managed.source == ManagedPluginSource::UserPluginDirectory;
    } else {
        managed.state = ManagedPluginState::RemovedPendingRestart;
        managed.restartRequired = true;
    }

    return managed;
}

ManagedPluginInfo pendingInfoFromFile(const fs::path& path,
                                      const std::string& normalizedUserDir) {
    const std::string normalizedPath = normalizedPathString(path);

    ManagedPluginInfo managed;
    managed.path = normalizedPath;
    managed.filename = fs::path(normalizedPath).filename().string();
    managed.state = ManagedPluginState::PendingRestart;
    managed.source = isUserPluginPath(normalizedPath, normalizedUserDir)
        ? ManagedPluginSource::UserPluginDirectory
        : ManagedPluginSource::OtherConfiguredDirectory;
    managed.fileExists = true;
    managed.canRemove =
        managed.source == ManagedPluginSource::UserPluginDirectory;
    managed.restartRequired = true;
    return managed;
}

std::vector<fs::path> pluginFilesInUserDirectory(const std::string& directory) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (directory.empty() || !fs::is_directory(directory, ec)) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && isSharedLibraryPath(entry.path())) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

} // namespace

const std::string& userPluginDirectory() {
    return System::pluginDirectory();
}

std::vector<ManagedPluginInfo> listManagedPlugins() {
    const std::string normalizedUserDir = userPluginDirectory().empty()
        ? std::string{}
        : normalizedPathString(userPluginDirectory());

    std::unordered_map<std::string, ManagedPluginInfo> byPath;
    for (const auto& info : PluginRegistry::instance().snapshot()) {
        auto managed = managedInfoFromRegistry(info, normalizedUserDir);
        byPath[managed.path] = std::move(managed);
    }

    // The user plugin directory is the only install target. Files here that
    // were copied after startup are visible to management but not loaded yet.
    for (const auto& file : pluginFilesInUserDirectory(userPluginDirectory())) {
        auto managed = pendingInfoFromFile(file, normalizedUserDir);
        byPath.emplace(managed.path, std::move(managed));
    }

    std::vector<ManagedPluginInfo> plugins;
    plugins.reserve(byPath.size());
    for (auto& item : byPath) {
        plugins.push_back(std::move(item.second));
    }

    std::sort(plugins.begin(), plugins.end(),
              [](const ManagedPluginInfo& a, const ManagedPluginInfo& b) {
                  const auto aName = displaySortName(a);
                  const auto bName = displaySortName(b);
                  if (aName == bName) {
                      return a.path < b.path;
                  }
                  return aName < bName;
              });
    return plugins;
}

PluginInstallResult installPlugin(const std::string& sourcePath) {
    const auto& directory = userPluginDirectory();
    if (directory.empty()) {
        return {false, {}, "Plugin directory is disabled"};
    }

    PluginInstallResult validated = validatePluginFile(sourcePath);
    if (!validated.success) {
        return validated;
    }

    const fs::path source = sourcePath;
    const fs::path destination = fs::path(directory) / source.filename();
    const std::string normalizedSource = normalizedPathString(source);
    const std::string normalizedDestination = normalizedPathString(destination);

    if (normalizedSource == normalizedDestination) {
        validated.installedPath = normalizedDestination;
        return validated;
    }

    return installPluginFile(sourcePath, directory);
}

PluginRemoveResult removePlugin(const std::string& pluginPath) {
    const auto& directory = userPluginDirectory();
    if (directory.empty()) {
        return {false, {}, "Plugin directory is disabled", false};
    }

    const std::string normalizedUserDir = normalizedPathString(directory);
    const std::string normalizedPluginPath = normalizedPathString(pluginPath);
    if (!isUserPluginPath(normalizedPluginPath, normalizedUserDir)) {
        return {false,
                {},
                "Can only remove plugins from the user plugin directory",
                false};
    }

    std::string error;
    const bool restartRequired = runtimeKnowsPluginPath(normalizedPluginPath);
    if (!removePluginFile(normalizedPluginPath, &error)) {
        return {false, normalizedPluginPath, error, false};
    }

    return {true,
            normalizedPluginPath,
            restartRequired
                ? "Plugin file removed. Restart required to unload native code."
                : "Plugin file removed.",
            restartRequired};
}

const char* platformPluginExtension() {
#ifdef _WIN32
    return "dll";
#elif defined(__APPLE__)
    return "dylib";
#else
    return "so";
#endif
}

bool isPluginLibraryFile(const std::string& path) {
    return isSharedLibraryPath(path);
}

} // namespace libera::plugin
