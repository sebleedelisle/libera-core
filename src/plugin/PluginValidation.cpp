#include "PluginValidation.hpp"

namespace libera::plugin {

bool isSharedLibraryPath(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    return ext == ".dylib" || ext == ".so" || ext == ".dll";
}

std::string validatePluginApi(const libera_plugin_api_t* api) {
    if (!api) {
        return "returned a null API table";
    }

    if (api->abi_version != LIBERA_PLUGIN_API_VERSION) {
        return "ABI version mismatch (plugin=" +
               std::to_string(api->abi_version) +
               ", host=" + std::to_string(LIBERA_PLUGIN_API_VERSION) + ")";
    }

    if (!api->type_name || !*api->type_name) {
        return "missing type_name";
    }

    if (!api->display_name || !*api->display_name) {
        return "missing display_name";
    }

    if (!api->discover) {
        return "missing discover()";
    }

    if (!api->connect_controller) {
        return "missing connect_controller()";
    }

    if (!api->destroy_controller) {
        return "missing destroy_controller()";
    }

    const bool hasPointTransport = api->send_points != nullptr;
    const bool hasFrameRequirements = api->get_frame_requirements != nullptr;
    const bool hasFrameSender = api->send_frame != nullptr;
    if (hasFrameRequirements != hasFrameSender) {
        return "must provide both get_frame_requirements() and send_frame()";
    }

    const bool hasFrameTransport = hasFrameRequirements && hasFrameSender;
    if (!hasPointTransport && !hasFrameTransport) {
        return "missing send_points() or get_frame_requirements()+send_frame()";
    }

    if (api->property_count > 0 && !api->properties) {
        return "declared properties without a property table";
    }

    if (api->property_count > 0 && !api->read_property) {
        return "declared properties without read_property()";
    }

    return {};
}

} // namespace libera::plugin
