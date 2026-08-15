#pragma once

#include <functional>
#include <optional>
#include <string>

namespace libera::gui::imgui {

struct PluginPanelState {
    std::string lastMessage;
    bool lastMessageIsError = false;
    bool restartHintVisible = false;
};

struct PluginPanelCallbacks {
    std::function<std::optional<std::string>()> choosePluginFile;
    std::function<void()> requestRestart;
    std::function<void(const std::string& path)> revealInFileBrowser;
};

struct PluginPanelOptions {
    bool allowInstall = true;
    bool allowRemove = true;
    bool showRestartButton = true;
    bool showRevealButtons = true;
    bool showRuntimeErrors = true;
};

/*
 * Draws reusable plugin-management content into the current ImGui window.
 *
 * The caller owns window placement, native file picking, restart behavior, and
 * any app-specific styling. This panel owns only the shared plugin UI logic.
 */
void DrawPluginManagementPanel(PluginPanelState& state,
                               const PluginPanelCallbacks& callbacks,
                               const PluginPanelOptions& options = {});

} // namespace libera::gui::imgui
