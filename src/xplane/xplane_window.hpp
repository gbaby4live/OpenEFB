#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "openefb/core/weather_model.hpp"
#include "xplane_preferences.hpp"

#include <XPLMDisplay.h>

#include <memory>

namespace openefb::xplane {

class XPlaneWindow final : public WindowSurface {
public:
    static std::unique_ptr<WindowSurface> create(UiModel& ui_model, TelemetryModel& telemetry_model,
                                                 FlightPlanModel& flight_plan_model,
                                                 WeatherModel& weather_model,
                                                 XPlanePreferences& preferences);

    ~XPlaneWindow() override;

    XPlaneWindow(const XPlaneWindow&) = delete;
    XPlaneWindow& operator=(const XPlaneWindow&) = delete;

    void show() override;
    void hide() override;
    [[nodiscard]] bool visible() const override;

private:
    XPlaneWindow(UiModel& ui_model, TelemetryModel& telemetry_model,
                 FlightPlanModel& flight_plan_model, WeatherModel& weather_model,
                 XPlanePreferences& preferences);

    void render(XPLMWindowID window_id) const;
    void save_geometry() const;

    static void draw(XPLMWindowID window_id, void* refcon);
    static int handle_mouse(XPLMWindowID window_id, int x, int y, XPLMMouseStatus status, void* refcon);
    static void handle_key(XPLMWindowID window_id, char key, XPLMKeyFlags flags, char virtual_key,
                           void* refcon, int losing_focus);
    static XPLMCursorStatus handle_cursor(XPLMWindowID window_id, int x, int y, void* refcon);
    static int handle_wheel(XPLMWindowID window_id, int x, int y, int wheel, int clicks, void* refcon);

    UiModel& ui_model_;
    TelemetryModel& telemetry_model_;
    FlightPlanModel& flight_plan_model_;
    WeatherModel& weather_model_;
    XPlanePreferences& preferences_;
    XPLMWindowID window_id_{nullptr};
};

} // namespace openefb::xplane
