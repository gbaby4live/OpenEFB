#include "openefb/core/application.hpp"

#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace {

std::unique_ptr<openefb::Application> application;

void copy_plugin_string(char* destination, std::string_view value) {
    constexpr std::size_t xplm_buffer_size = 256;
    const auto length = std::min(value.size(), xplm_buffer_size - 1);
    std::memcpy(destination, value.data(), length);
    destination[length] = '\0';
}

void xplane_log(std::string_view message) {
    const std::string line = "[OpenEFB] " + std::string(message) + "\n";
    XPLMDebugString(line.c_str());
}

} // namespace

PLUGIN_API int XPluginStart(char* out_name, char* out_signature, char* out_description) {
    copy_plugin_string(out_name, "OpenEFB");
    copy_plugin_string(out_signature, "org.openefb.plugin");
    copy_plugin_string(out_description, "Open-source electronic flight bag for X-Plane 12");

    application = std::make_unique<openefb::Application>(xplane_log);
    return application->start() ? 1 : 0;
}

PLUGIN_API void XPluginStop() {
    if (application) {
        application->stop();
        application.reset();
    }
}

PLUGIN_API int XPluginEnable() {
    return application && application->enable() ? 1 : 0;
}

PLUGIN_API void XPluginDisable() {
    if (application) {
        application->disable();
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void*) {
    if (application && message == XPLM_MSG_PLANE_LOADED) {
        application->on_flight_loaded();
    }
}
