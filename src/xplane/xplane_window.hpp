#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/airspace_model.hpp"
#include "openefb/core/flight_plan_editor.hpp"
#include "openefb/core/airport_info.hpp"
#include "openefb/core/fuel_model.hpp"
#include "openefb/core/moving_map_model.hpp"
#include "openefb/core/planning_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "openefb/core/weather_model.hpp"
#include "xplane_preferences.hpp"
#include "xplane_map_tiles.hpp"
#include "xplane_flight_plan.hpp"
#include "xplane_airport_data.hpp"

#include <XPLMDisplay.h>

#include <memory>
#include <string>

namespace openefb::xplane {

class XPlaneWindow final : public WindowSurface {
public:
    static std::unique_ptr<WindowSurface> create(UiModel& ui_model, TelemetryModel& telemetry_model,
                                                 FlightPlanModel& flight_plan_model,
                                                 FlightPlanEditor& flight_plan_editor,
                                                 XPlaneFlightPlan& xplane_flight_plan,
                                                 AirportInfoModel& airport_info_model,
                                                 XPlaneAirportData& xplane_airport_data,
                                                 AirspaceModel& airspace_model,
                                                 FuelModel& fuel_model,
                                                 PlanningModel& planning_model,
                                                 MovingMapModel& moving_map_model,
                                                 RouteProgressModel& route_progress_model,
                                                 WeatherModel& weather_model,
                                                 XPlanePreferences& preferences);

    ~XPlaneWindow() override;

    XPlaneWindow(const XPlaneWindow&) = delete;
    XPlaneWindow& operator=(const XPlaneWindow&) = delete;

    void show() override;
    void hide() override;
    [[nodiscard]] bool visible() const override;

private:
    enum class EditorPlacement {
        departure,
        destination,
        enroute,
    };

    XPlaneWindow(UiModel& ui_model, TelemetryModel& telemetry_model,
                 FlightPlanModel& flight_plan_model, FlightPlanEditor& flight_plan_editor,
                 XPlaneFlightPlan& xplane_flight_plan,
                 AirportInfoModel& airport_info_model,
                 XPlaneAirportData& xplane_airport_data,
                 AirspaceModel& airspace_model,
                 FuelModel& fuel_model,
                 PlanningModel& planning_model,
                 MovingMapModel& moving_map_model,
                 RouteProgressModel& route_progress_model,
                 WeatherModel& weather_model,
                 XPlanePreferences& preferences);

    void render(XPLMWindowID window_id) const;
    void save_geometry() const;
    void resolve_editor_waypoint(EditorPlacement placement);
    void apply_editor_route();
    void handle_editor_key(char key, char virtual_key);
    void handle_airport_key(char key, char virtual_key);
    void search_airport();

    static void draw(XPLMWindowID window_id, void* refcon);
    static int handle_mouse(XPLMWindowID window_id, int x, int y, XPLMMouseStatus status, void* refcon);
    static void handle_key(XPLMWindowID window_id, char key, XPLMKeyFlags flags, char virtual_key,
                           void* refcon, int losing_focus);
    static XPLMCursorStatus handle_cursor(XPLMWindowID window_id, int x, int y, void* refcon);
    static int handle_wheel(XPLMWindowID window_id, int x, int y, int wheel, int clicks, void* refcon);

    UiModel& ui_model_;
    TelemetryModel& telemetry_model_;
    FlightPlanModel& flight_plan_model_;
    FlightPlanEditor& flight_plan_editor_;
    XPlaneFlightPlan& xplane_flight_plan_;
    AirportInfoModel& airport_info_model_;
    XPlaneAirportData& xplane_airport_data_;
    AirspaceModel& airspace_model_;
    FuelModel& fuel_model_;
    PlanningModel& planning_model_;
    MovingMapModel& moving_map_model_;
    RouteProgressModel& route_progress_model_;
    WeatherModel& weather_model_;
    XPlanePreferences& preferences_;
    mutable XPlaneMapTiles map_tiles_;
    XPLMWindowID window_id_{nullptr};
    std::string airport_query_;
};

} // namespace openefb::xplane
