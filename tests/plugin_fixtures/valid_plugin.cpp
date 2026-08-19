#include "libera/plugin/libera_plugin.h"

namespace {

void discover(void*, libera_emit_controller_fn emit, void* ctx) {
    if (!emit) {
        return;
    }

    libera_controller_info_t info;
    libera_controller_info_init(&info,
                                "test-valid-001",
                                "Test Valid Controller",
                                30000);
    emit(ctx, &info);
}

void* connectController(void*,
                        const libera_controller_info_t*,
                        libera_host_ctx_t) {
    return nullptr;
}

void destroyController(void*) {}

libera_status_t sendPoints(void*,
                           const libera_point_t*,
                           uint32_t) {
    return LIBERA_OK;
}

const libera_plugin_api_t pluginApi = {
    /* abi_version        */ LIBERA_PLUGIN_API_VERSION,
    /* type_name          */ "TestValidPlugin",
    /* display_name       */ "Test Valid Plugin",
    /* create_backend     */ nullptr,
    /* destroy_backend    */ nullptr,
    /* rescan             */ nullptr,
    /* discover           */ &discover,
    /* connect_controller */ &connectController,
    /* destroy_controller */ &destroyController,
    /* send_points        */ &sendPoints,
    /* set_point_rate     */ nullptr,
    /* set_armed          */ nullptr,
    /* get_buffer_state   */ nullptr,
    /* properties         */ nullptr,
    /* property_count     */ 0,
    /* read_property      */ nullptr,
    /* get_frame_requirements */ nullptr,
    /* send_frame             */ nullptr,
};

} // namespace

LIBERA_PLUGIN_EXPORT(pluginApi)
