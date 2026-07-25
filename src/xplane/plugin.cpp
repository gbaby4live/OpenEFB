#include "openefb/core/application.hpp"
#include "openefb/core/airport_info.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/flight_plan_editor.hpp"
#include "openefb/core/fuel_model.hpp"
#include "openefb/core/moving_map_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/weather_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "xplane_flight_plan.hpp"
#include "xplane_airport_data.hpp"
#include "xplane_fuel.hpp"
#include "xplane_menu.hpp"
#include "xplane_preferences.hpp"
#include "xplane_route_progress.hpp"
#include "xplane_telemetry.hpp"
#include "xplane_weather.hpp"
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
          ui_model(),
          telemetry_model(),
          flight_plan_model(),
          flight_plan_editor(),
          airport_info_model(),
          fuel_model(),
          moving_map_model(),
          route_progress_model(),
          weather_model(),
          preferences(),
          telemetry(telemetry_model),
          flight_plan(flight_plan_model),
          airport_data(airport_info_model),
          fuel(fuel_model, telemetry_model),
          route_progress(route_progress_model, telemetry_model, flight_plan_model),
          weather(weather_model, flight_plan_model),
          window_controller([this] {
              return openefb::xplane::XPlaneWindow::create(
                  ui_model, telemetry_model, flight_plan_model, flight_plan_editor, flight_plan,
                  airport_info_model, airport_data,
                  fuel_model, moving_map_model,
                  route_progress_model,
                  weather_model, preferences);
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
    openefb::FlightPlanModel flight_plan_model;
    openefb::FlightPlanEditor flight_plan_editor;
    openefb::AirportInfoModel airport_info_model;
    openefb::FuelModel fuel_model;
    openefb::MovingMapModel moving_map_model;
    openefb::RouteProgressModel route_progress_model;
    openefb::WeatherModel weather_model;
    openefb::xplane::XPlanePreferences preferences;
    openefb::xplane::XPlaneTelemetry telemetry;
    openefb::xplane::XPlaneFlightPlan flight_plan;
    openefb::xplane::XPlaneAirportData airport_data;
    openefb::xplane::XPlaneFuel fuel;
    openefb::xplane::XPlaneRouteProgress route_progress;
    openefb::xplane::XPlaneWeather weather;
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
        runtime->weather.stop();
        runtime->airport_data.stop();
        runtime->route_progress.stop();
        runtime->fuel.stop();
        runtime->flight_plan.stop();
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
    if (!runtime->flight_plan.start()) {
        PluginRuntime::xplane_log("flight plan unavailable");
    }
    if (!runtime->airport_data.start()) {
        PluginRuntime::xplane_log("airport data unavailable");
    }
    if (!runtime->fuel.start()) {
        PluginRuntime::xplane_log("fuel data unavailable");
    }
    if (!runtime->route_progress.start()) {
        PluginRuntime::xplane_log("route progress unavailable");
    }
    if (!runtime->weather.start()) {
        PluginRuntime::xplane_log("weather unavailable");
    }
    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (runtime) {
        runtime->weather.stop();
        runtime->airport_data.stop();
        runtime->route_progress.stop();
        runtime->fuel.stop();
        runtime->flight_plan.stop();
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
