#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/airspace_model.hpp"
#include "openefb/core/briefing_model.hpp"
#include "openefb/core/flight_plan_editor.hpp"
#include "openefb/core/airport_info.hpp"
#include "openefb/core/fuel_model.hpp"
#include "openefb/core/flight_log_model.hpp"
#include "openefb/core/moving_map_model.hpp"
#include "openefb/core/navigation_database_model.hpp"
#include "openefb/core/planning_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/traffic_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "openefb/core/weather_model.hpp"
#include "xplane_preferences.hpp"
#include "xplane_map_tiles.hpp"
#include "xplane_map_pois.hpp"
#include "xplane_pdf_viewer.hpp"
#include "xplane_flight_plan.hpp"
#include "xplane_airport_data.hpp"
#include "xplane_briefing_library.hpp"

#include <XPLMDisplay.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace openefb::xplane {

struct MapHitTarget {
    int x{};
    int y{};
    FlightPlanLeg leg;
};

struct PoiHitTarget {
    int x{};
    int y{};
    MapPoi poi;
};

struct TrafficHitTarget {
    int x{};
    int y{};
    std::string key;
    std::string callsign;
};

struct PendingMapNavigation {
    FlightPlanLeg leg;
    std::string name;
    std::string detail;
};

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
                                                 BriefingModel& briefing_model,
                                                 XPlaneBriefingLibrary& briefing_library,
                                                 MovingMapModel& moving_map_model,
                                                 NavigationDatabaseModel& navigation_database_model,
                                                 RouteProgressModel& route_progress_model,
                                                 WeatherModel& weather_model,
                                                 FlightLogModel& flight_log_model,
                                                 TrafficModel& traffic_model,
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
                 BriefingModel& briefing_model,
                 XPlaneBriefingLibrary& briefing_library,
                 MovingMapModel& moving_map_model,
                 NavigationDatabaseModel& navigation_database_model,
                 RouteProgressModel& route_progress_model,
                 WeatherModel& weather_model,
                 FlightLogModel& flight_log_model,
                 TrafficModel& traffic_model,
                 XPlanePreferences& preferences);

    void render(XPLMWindowID window_id) const;
    void save_geometry() const;
    void resolve_editor_waypoint(EditorPlacement placement);
    void apply_editor_route();
    void import_latest_route();
    void export_current_route();
    void toggle_high_contrast();
    void toggle_comfort_size();
    void toggle_traffic_injection();
    void resize_interface(double factor);
    void handle_editor_key(char key, char virtual_key);
    void handle_airport_key(char key, char virtual_key);
    void handle_briefing_key(char key, char virtual_key);
    void open_briefing_entry();
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
    BriefingModel& briefing_model_;
    XPlaneBriefingLibrary& briefing_library_;
    MovingMapModel& moving_map_model_;
    NavigationDatabaseModel& navigation_database_model_;
    RouteProgressModel& route_progress_model_;
    WeatherModel& weather_model_;
    FlightLogModel& flight_log_model_;
    TrafficModel& traffic_model_;
    XPlanePreferences& preferences_;
    mutable XPlaneMapTiles map_tiles_;
    mutable XPlaneMapPois map_pois_;
    mutable XPlanePdfViewer pdf_viewer_;
    mutable std::vector<MapHitTarget> map_hit_targets_;
    mutable std::vector<PoiHitTarget> poi_hit_targets_;
    mutable std::vector<TrafficHitTarget> traffic_hit_targets_;
    mutable std::optional<MapPoi> hovered_map_poi_;
    mutable std::optional<MapHitTarget> hovered_map_target_;
    std::optional<PendingMapNavigation> pending_map_navigation_;
    std::optional<std::string> selected_traffic_key_;
    std::string map_action_message_;
    bool map_dragging_{false};
    bool map_drag_moved_{false};
    bool map_filters_open_{false};
    int map_filter_scroll_{0};
    bool navigation_open_{false};
    bool show_map_aircraft_{true};
    bool show_map_aircraft_info_{true};
    bool show_map_route_{true};
    bool show_map_labels_{true};
    int map_marker_scale_{100};
    int map_drag_start_x_{};
    int map_drag_start_y_{};
    double map_drag_start_latitude_{};
    double map_drag_start_longitude_{};
    XPLMWindowID window_id_{nullptr};
    std::string airport_query_;
    DisplayPreferences display_preferences_;
    std::string route_file_message_;
};

} // namespace openefb::xplane
