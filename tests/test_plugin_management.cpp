#include "libera/System.hpp"
#include "libera/log/Log.hpp"
#include "libera/plugin/PluginManagement.hpp"
#include "libera/plugin/PluginRegistry.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifndef TEST_VALID_PLUGIN_PATH
#error "TEST_VALID_PLUGIN_PATH must point at the valid plugin fixture"
#endif

#ifndef TEST_MISSING_TRANSPORT_PLUGIN_PATH
#error "TEST_MISSING_TRANSPORT_PLUGIN_PATH must point at the invalid plugin fixture"
#endif

using namespace libera;
using namespace libera::plugin;

static int g_failures = 0;

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { logError("ASSERT TRUE FAILED", (msg), "@", __FILE__, __LINE__); ++g_failures; } } while(0)

#define ASSERT_STRING_EQ(a,b,msg) \
    do { auto _va=(a); auto _vb=(b); if (!((_va)==(_vb))) { \
        logError("ASSERT STRING EQ FAILED", (msg), "lhs", _va, "rhs", _vb, "@", __FILE__, __LINE__); \
        ++g_failures; \
    } } while(0)

namespace {

std::string normalizedPathString(const std::filesystem::path& path) {
    namespace fs = std::filesystem;
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

std::filesystem::path uniqueTempDirectory() {
    const auto suffix = std::chrono::steady_clock::now()
        .time_since_epoch()
        .count();
    return std::filesystem::temp_directory_path() /
           ("libera-plugin-management-test-" + std::to_string(suffix));
}

bool containsText(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

const ManagedPluginInfo* findManagedPlugin(
    const std::vector<ManagedPluginInfo>& plugins,
    const std::string& path) {
    const std::string normalized = normalizedPathString(path);
    auto it = std::find_if(plugins.begin(), plugins.end(),
                           [&](const ManagedPluginInfo& info) {
                               return info.path == normalized;
                           });
    return it == plugins.end() ? nullptr : &*it;
}

void testValidationUsesRuntimeRules() {
    const auto valid = validatePluginFile(TEST_VALID_PLUGIN_PATH);
    ASSERT_TRUE(valid.success, "valid plugin should pass validation");
    ASSERT_TRUE(containsText(valid.message, "Test Valid Plugin"),
                "valid plugin message should include display name");

    const auto invalid =
        validatePluginFile(TEST_MISSING_TRANSPORT_PLUGIN_PATH);
    ASSERT_TRUE(!invalid.success,
                "plugin without a transport should fail validation");
    ASSERT_TRUE(containsText(invalid.message,
                             "missing send_points() or get_frame_requirements()+send_frame()"),
                "validation should report the missing transport callbacks");
}

void testManagedPluginInstallListAndRemove() {
    namespace fs = std::filesystem;

    const fs::path pluginDir = uniqueTempDirectory();
    std::error_code ec;
    fs::remove_all(pluginDir, ec);
    fs::create_directories(pluginDir, ec);
    ASSERT_TRUE(!ec, "test plugin directory should be created");

    System::setPluginDirectory(pluginDir.string());
    ASSERT_STRING_EQ(userPluginDirectory(),
                     pluginDir.string(),
                     "management should use the configured user plugin directory");

    auto externalRemoval = removePlugin(TEST_VALID_PLUGIN_PATH);
    ASSERT_TRUE(!externalRemoval.success,
                "removePlugin should reject files outside the user plugin directory");

    auto install = installPlugin(TEST_VALID_PLUGIN_PATH);
    ASSERT_TRUE(install.success, "valid plugin should install");
    ASSERT_STRING_EQ(fs::path(install.installedPath).parent_path().string(),
                     pluginDir.string(),
                     "plugin should install into the configured user directory");

    const auto reinstall = installPlugin(install.installedPath);
    ASSERT_TRUE(reinstall.success,
                "installing a plugin already in the user directory should validate without copy failure");
    ASSERT_STRING_EQ(reinstall.installedPath,
                     normalizedPathString(install.installedPath),
                     "same-path install should report the installed path");

    auto plugins = listManagedPlugins();
    const ManagedPluginInfo* pending =
        findManagedPlugin(plugins, install.installedPath);
    ASSERT_TRUE(pending != nullptr, "installed plugin should be listed");
    if (pending) {
        ASSERT_TRUE(pending->state == ManagedPluginState::PendingRestart,
                    "newly-installed plugin should require restart before load");
        ASSERT_TRUE(pending->source == ManagedPluginSource::UserPluginDirectory,
                    "newly-installed plugin should be from the user plugin directory");
        ASSERT_TRUE(pending->fileExists, "newly-installed plugin should exist");
        ASSERT_TRUE(pending->canRemove, "newly-installed plugin should be removable");
        ASSERT_TRUE(pending->restartRequired,
                    "newly-installed plugin should require restart");
    }

    const auto pendingRemoval = removePlugin(install.installedPath);
    ASSERT_TRUE(pendingRemoval.success,
                "pending plugin file should be removable before restart");
    ASSERT_TRUE(!pendingRemoval.restartRequired,
                "removing a never-loaded pending plugin should not require restart");

    install = installPlugin(TEST_VALID_PLUGIN_PATH);
    ASSERT_TRUE(install.success, "valid plugin should reinstall after pending removal");

    PluginRegistry::instance().recordLoaded(normalizedPathString(install.installedPath),
                                            "TestValidPlugin",
                                            "Test Valid Plugin");

    plugins = listManagedPlugins();
    const ManagedPluginInfo* loaded =
        findManagedPlugin(plugins, install.installedPath);
    ASSERT_TRUE(loaded != nullptr, "loaded plugin should be listed");
    if (loaded) {
        ASSERT_TRUE(loaded->state == ManagedPluginState::Loaded,
                    "registry-loaded plugin should be reported as loaded");
        ASSERT_TRUE(!loaded->restartRequired,
                    "loaded plugin should not require restart while its file exists");
        ASSERT_TRUE(loaded->canRemove,
                    "loaded user plugin file should still be removable");
    }

    const auto removal = removePlugin(install.installedPath);
    ASSERT_TRUE(removal.success, "installed plugin should be removed");
    ASSERT_TRUE(removal.restartRequired,
                "removing a plugin should require restart to unload native code");

    plugins = listManagedPlugins();
    const ManagedPluginInfo* removed =
        findManagedPlugin(plugins, install.installedPath);
    ASSERT_TRUE(removed != nullptr,
                "removed loaded plugin should remain visible until restart");
    if (removed) {
        ASSERT_TRUE(removed->state == ManagedPluginState::RemovedPendingRestart,
                    "removed loaded plugin should be marked pending restart");
        ASSERT_TRUE(!removed->fileExists, "removed plugin file should be gone");
        ASSERT_TRUE(!removed->canRemove,
                    "removed plugin should not offer another remove action");
        ASSERT_TRUE(removed->restartRequired,
                    "removed loaded plugin should require restart");
    }

    fs::remove_all(pluginDir, ec);
}

} // namespace

int main() {
    testValidationUsesRuntimeRules();
    testManagedPluginInstallListAndRemove();
    return g_failures == 0 ? 0 : 1;
}
