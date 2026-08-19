#include "libera/gui/imgui/PluginManagementPanel.hpp"

#include "imgui.h"
#include "libera/plugin/PluginManagement.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

namespace libera::gui::imgui {

namespace {

constexpr ImU32 loadedColor = IM_COL32(60, 200, 90, 255);
constexpr ImU32 pendingColor = IM_COL32(230, 180, 70, 255);
constexpr ImU32 failureColor = IM_COL32(230, 110, 110, 255);
constexpr ImU32 neutralColor = IM_COL32(150, 150, 150, 255);

const char* stateLabel(plugin::ManagedPluginState state) {
    using State = plugin::ManagedPluginState;
    switch (state) {
        case State::Loaded:
            return "Loaded";
        case State::NotAPlugin:
            return "Not a Libera plugin";
        case State::FailedLoad:
            return "Failed to load";
        case State::FailedValidation:
            return "Validation failed";
        case State::FailedBackend:
            return "Backend init failed";
        case State::PendingRestart:
            return "Pending restart";
        case State::RemovedPendingRestart:
            return "Removed, restart required";
    }
    return "?";
}

ImU32 stateColor(plugin::ManagedPluginState state) {
    using State = plugin::ManagedPluginState;
    switch (state) {
        case State::Loaded:
            return loadedColor;
        case State::PendingRestart:
        case State::RemovedPendingRestart:
            return pendingColor;
        case State::FailedLoad:
        case State::FailedValidation:
        case State::FailedBackend:
            return failureColor;
        case State::NotAPlugin:
            return neutralColor;
    }
    return neutralColor;
}

std::string formatRuntimeTime(std::chrono::system_clock::time_point timePoint) {
    const auto raw = std::chrono::system_clock::to_time_t(timePoint);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &raw);
#else
    localtime_r(&raw, &localTime);
#endif

    char buffer[32];
    std::snprintf(buffer,
                  sizeof(buffer),
                  "%02d:%02d:%02d",
                  localTime.tm_hour,
                  localTime.tm_min,
                  localTime.tm_sec);
    return buffer;
}

std::string pluginTitle(const plugin::ManagedPluginInfo& pluginInfo) {
    if (!pluginInfo.displayName.empty()) {
        return pluginInfo.displayName;
    }
    if (!pluginInfo.filename.empty()) {
        return pluginInfo.filename;
    }
    return pluginInfo.path;
}

bool hasRestartRequirement(const std::vector<plugin::ManagedPluginInfo>& plugins) {
    return std::any_of(plugins.begin(),
                       plugins.end(),
                       [](const plugin::ManagedPluginInfo& pluginInfo) {
                           return pluginInfo.restartRequired;
                       });
}

void drawStatusDot(ImU32 color) {
    const float radius = ImGui::GetTextLineHeight() * 0.38f;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    pos.x += radius;
    pos.y += ImGui::GetTextLineHeight() * 0.5f;
    ImGui::GetWindowDrawList()->AddCircleFilled(pos, radius, color, 24);
    ImGui::Dummy(ImVec2(radius * 2.0f + 4.0f, ImGui::GetTextLineHeight()));
}

void drawLabelValue(const char* label, const std::string& value) {
    if (value.empty()) {
        return;
    }
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", value.c_str());
}

void drawPlugin(plugin::ManagedPluginInfo pluginInfo,
                PluginPanelState& state,
                const PluginPanelCallbacks& callbacks,
                const PluginPanelOptions& options) {
    ImGui::PushID(pluginInfo.path.c_str());

    drawStatusDot(stateColor(pluginInfo.state));
    ImGui::SameLine();

    const bool expanded = ImGui::TreeNodeEx(
        "plugin",
        ImGuiTreeNodeFlags_SpanAvailWidth,
        "%s",
        pluginTitle(pluginInfo).c_str());

    const bool canReveal =
        options.showRevealButtons && callbacks.revealInFileBrowser != nullptr;
    const bool canRemove =
        options.allowRemove && pluginInfo.canRemove && pluginInfo.fileExists;
    if (canReveal || canRemove) {
        ImGui::SameLine();
        const float removeWidth = canRemove ? 86.0f : 0.0f;
        const float revealWidth = canReveal ? 74.0f : 0.0f;
        const float spacing = (canReveal && canRemove)
            ? ImGui::GetStyle().ItemSpacing.x
            : 0.0f;
        const float x = ImGui::GetContentRegionMax().x -
                        removeWidth -
                        revealWidth -
                        spacing;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x));

        if (canReveal) {
            if (ImGui::Button("Reveal", ImVec2(revealWidth, 0.0f))) {
                callbacks.revealInFileBrowser(pluginInfo.path);
            }
            if (canRemove) {
                ImGui::SameLine();
            }
        }

        if (canRemove) {
            if (ImGui::Button("Remove", ImVec2(removeWidth, 0.0f))) {
                const auto result = plugin::removePlugin(pluginInfo.path);
                state.lastMessageIsError = !result.success;
                state.lastMessage = result.success
                    ? result.message
                    : "Remove failed: " + result.message;
                if (result.restartRequired) {
                    state.restartHintVisible = true;
                }
            }
        }
    }

    if (expanded) {
        ImGui::Indent();

        ImGui::TextDisabled("State:");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, stateColor(pluginInfo.state));
        ImGui::TextUnformatted(stateLabel(pluginInfo.state));
        ImGui::PopStyleColor();

        if (!pluginInfo.typeName.empty()) {
            drawLabelValue("Type:", pluginInfo.typeName);
        }
        drawLabelValue("File:", pluginInfo.filename);
        drawLabelValue("Path:", pluginInfo.path);

        if (pluginInfo.loadError) {
            ImGui::PushStyleColor(ImGuiCol_Text, failureColor);
            ImGui::TextWrapped("Error: %s", pluginInfo.loadError->c_str());
            ImGui::PopStyleColor();
        }

        if (pluginInfo.restartRequired) {
            ImGui::PushStyleColor(ImGuiCol_Text, pendingColor);
            ImGui::TextWrapped("Restart required for this change to take effect.");
            ImGui::PopStyleColor();
        }

        if (options.showRuntimeErrors && !pluginInfo.runtimeErrors.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("Runtime errors");
            for (const auto& runtimeError : pluginInfo.runtimeErrors) {
                const auto timeLabel = formatRuntimeTime(runtimeError.time);
                ImGui::BulletText("%s  %s: %s",
                                  timeLabel.c_str(),
                                  runtimeError.code.c_str(),
                                  runtimeError.message.c_str());
            }
        }

        ImGui::Unindent();
        ImGui::TreePop();
    }

    ImGui::PopID();
}

void drawPluginSection(const char* label,
                       const char* emptyLabel,
                       const std::vector<plugin::ManagedPluginInfo>& plugins,
                       PluginPanelState& state,
                       const PluginPanelCallbacks& callbacks,
                       const PluginPanelOptions& options) {
    ImGui::TextDisabled("%s", label);
    if (plugins.empty()) {
        ImGui::TextDisabled("%s", emptyLabel);
        return;
    }

    for (const auto& pluginInfo : plugins) {
        drawPlugin(pluginInfo, state, callbacks, options);
    }
}

} // namespace

void DrawPluginManagementPanel(PluginPanelState& state,
                               const PluginPanelCallbacks& callbacks,
                               const PluginPanelOptions& options) {
    auto plugins = plugin::listManagedPlugins();
    if (hasRestartRequirement(plugins)) {
        state.restartHintVisible = true;
    }

    ImGui::TextDisabled("User plugin directory:");
    ImGui::SameLine();
    ImGui::TextWrapped("%s", plugin::userPluginDirectory().c_str());
    ImGui::TextDisabled(
        "Plugins in this folder are loaded at startup. Install/remove changes require a restart.");

    if (state.restartHintVisible) {
        if (options.showRestartButton && callbacks.requestRestart) {
            if (ImGui::Button("Restart now", ImVec2(130.0f, 0.0f))) {
                callbacks.requestRestart();
            }
            ImGui::SameLine();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, pendingColor);
        ImGui::TextWrapped("Restart the app for plugin changes to take effect.");
        ImGui::PopStyleColor();
    }

    ImGui::Separator();

    if (options.allowInstall && callbacks.choosePluginFile) {
        if (ImGui::Button("Install new plugin...", ImVec2(190.0f, 0.0f))) {
            const auto picked = callbacks.choosePluginFile();
            if (picked && !picked->empty()) {
                const auto result = plugin::installPlugin(*picked);
                state.lastMessageIsError = !result.success;
                state.lastMessage = result.message;
                if (result.success) {
                    state.restartHintVisible = true;
                    if (!result.installedPath.empty()) {
                        state.lastMessage += ". Restart to load it.";
                    }
                }
            }
        }
    }

    if (!state.lastMessage.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              state.lastMessageIsError ? failureColor : pendingColor);
        ImGui::TextWrapped("%s", state.lastMessage.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<plugin::ManagedPluginInfo> userPlugins;
    std::vector<plugin::ManagedPluginInfo> otherPlugins;
    for (auto& pluginInfo : plugins) {
        if (pluginInfo.source == plugin::ManagedPluginSource::UserPluginDirectory) {
            userPlugins.push_back(std::move(pluginInfo));
        } else {
            otherPlugins.push_back(std::move(pluginInfo));
        }
    }

    drawPluginSection("User plugins",
                      "No user-installed plugins found.",
                      userPlugins,
                      state,
                      callbacks,
                      options);

    if (!otherPlugins.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        drawPluginSection("Other plugins found at startup",
                          "",
                          otherPlugins,
                          state,
                          callbacks,
                          options);
    }
}

} // namespace libera::gui::imgui
