#include "openefb/core/application.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "xplane_menu.hpp"
#include "xplane_preferences.hpp"
#include "xplane_telemetry.hpp"
#include "xplane_window.hpp"

#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

namespace {

struct PluginRuntime {
    PluginRuntime()
        : application(xplane_log),
          telemetry(telemetry_model),
          window_controller([this] {
              return openefb::xplane::XPlaneWindow::create(ui_model, telemetry_model, preferences);
          }),
          menu([this] { toggle_window(); }) {}

    static void xplane_log(std::string_view message) {
        const std::string line = "[OpenEFB] " + std::string(message) + "\n";
        XPLMDebugString(line.c_str());
    }

    void toggle_window() {
        if (application.state() != openefb::LifecycleState::enabled) {
            return;
        }
        if (!window_controller.toggle()) {
            xplane_log("failed to create window");
        }
    }

    openefb::Application application;
    openefb::UiModel ui_model;
    openefb::TelemetryModel telemetry_model;
    openefb::xplane::XPlanePreferences preferences;
    openefb::xplane::XPlaneTelemetry telemetry;
    openefb::WindowController window_controller;
    openefb::xplane::XPlaneMenu menu;
};

std::unique_ptr<PluginRuntime> runtime;

void copy_plugin_string(char* destination, std::string_view value) {
    constexpr std::size_t xplm_buffer_size = 256;
    const auto length = std::min(value.size(), xplm_buffer_size - 1);
    std::memcpy(destination, value.data(), length);
    destination[length] = '\0';
}

} // namespace

PLUGIN_API int XPluginStart(char* out_name, char* out_signature, char* out_description) {
    copy_plugin_string(out_name, "OpenEFB");
    copy_plugin_string(out_signature, "org.openefb.plugin");
    copy_plugin_string(out_description, "Open-source electronic flight bag for X-Plane 12");

    try {
        runtime = std::make_unique<PluginRuntime>();
        if (!runtime->menu.valid() || !runtime->application.start()) {
            runtime.reset();
            return 0;
        }
        return 1;
    } catch (...) {
        runtime.reset();
        return 0;
    }
}

PLUGIN_API void XPluginStop() {
    if (runtime) {
        runtime->telemetry.stop();
        runtime->window_controller.reset();
        runtime->application.stop();
        runtime.reset();
    }
}

PLUGIN_API int XPluginEnable() {
    if (!runtime || !runtime->application.enable()) {
        return 0;
    }
    if (!runtime->telemetry.start()) {
        PluginRuntime::xplane_log("telemetry datarefs unavailable");
    }
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (runtime) {
        runtime->telemetry.stop();
        runtime->window_controller.reset();
        runtime->application.disable();
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void* parameter) {
    if (runtime && message == XPLM_MSG_PLANE_LOADED && reinterpret_cast<std::intptr_t>(parameter) == 0) {
        runtime->telemetry.refresh_aircraft_identity();
        runtime->application.on_flight_loaded();
    }
}
