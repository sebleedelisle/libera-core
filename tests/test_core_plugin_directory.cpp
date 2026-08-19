#include "libera/System.hpp"
#include "libera/log/Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>

using namespace libera;

static int g_failures = 0;

#define ASSERT_TRUE(cond, msg) \
    do { if (!(cond)) { logError("ASSERT TRUE FAILED", (msg), "@", __FILE__, __LINE__); ++g_failures; } } while(0)

#define ASSERT_STRING_EQ(a,b,msg) \
    do { auto _va=(a); auto _vb=(b); if (!((_va)==(_vb))) { \
        logError("ASSERT STRING EQ FAILED", (msg), "lhs", _va, "rhs", _vb, "@", __FILE__, __LINE__); \
        ++g_failures; \
    } } while(0)

namespace {

std::string envValue(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string{};
}

std::filesystem::path expectedUserPluginDirectory() {
    namespace fs = std::filesystem;
    fs::path baseDir;

#ifdef _WIN32
    const auto localAppData = envValue("LOCALAPPDATA");
    if (!localAppData.empty()) {
        baseDir = localAppData;
    } else {
        const auto userProfile = envValue("USERPROFILE");
        if (!userProfile.empty()) {
            baseDir = fs::path(userProfile) / "AppData" / "Local";
        }
    }
#elif defined(__APPLE__)
    const auto home = envValue("HOME");
    if (!home.empty()) {
        baseDir = fs::path(home) / "Library" / "Application Support";
    }
#else
    const auto xdgDataHome = envValue("XDG_DATA_HOME");
    if (!xdgDataHome.empty()) {
        baseDir = xdgDataHome;
    } else {
        const auto home = envValue("HOME");
        if (!home.empty()) {
            baseDir = fs::path(home) / ".local" / "share";
        }
    }
#endif

    if (baseDir.empty()) {
        baseDir = fs::current_path();
    }

#if defined(_WIN32) || defined(__APPLE__)
    return baseDir / "Libera" / "Plugins";
#else
    return baseDir / "libera" / "plugins";
#endif
}

void testDefaultPluginDirectoryUsesSharedUserLocation() {
    const auto expected = expectedUserPluginDirectory().string();
    const auto& dirs = System::pluginDirectories();

    ASSERT_TRUE(dirs.size() == 1, "default plugin search should use one shared directory");
    ASSERT_STRING_EQ(System::pluginDirectory(),
                     expected,
                     "first plugin directory should be the shared user plugin folder");
    ASSERT_STRING_EQ(dirs.front(),
                     expected,
                     "plugin directory list should contain the shared user plugin folder");
}

} // namespace

int main() {
    testDefaultPluginDirectoryUsesSharedUserLocation();
    return g_failures == 0 ? 0 : 1;
}
