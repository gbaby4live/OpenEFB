#include "openefb/core/application.hpp"
#include "openefb/core/airspace_model.hpp"
#include "openefb/core/airport_info.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/flight_plan_editor.hpp"
#include "openefb/core/fuel_model.hpp"
#include "openefb/core/moving_map_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "openefb/core/window_geometry.hpp"
#include "openefb/core/weather_model.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct FakeWindowState {
    bool visible{false};
    int destroyed{0};
};

class FakeWindow final : public openefb::WindowSurface {
public:
    explicit FakeWindow(std::shared_ptr<FakeWindowState> state) : state_(std::move(state)) {}
    ~FakeWindow() override { ++state_->destroyed; }

    void show() override { state_->visible = true; }
    void hide() override { state_->visible = false; }
    [[nodiscard]] bool visible() const override { return state_->visible; }

private:
    std::shared_ptr<FakeWindowState> state_;
};

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    std::vector<std::string> log;
    openefb::Application app([&log](std::string_view message) {
        log.emplace_back(message);
    });

    require(app.state() == openefb::LifecycleState::stopped, "initial state is stopped");
    require(!app.enable(), "cannot enable before start");
    require(app.start(), "application starts");
    require(!app.start(), "application cannot start twice");
    require(app.enable(), "started application enables");
    app.on_flight_loaded();
    require(app.flight_loaded(), "flight-loaded event is recorded while enabled");
    app.disable();
    require(app.state() == openefb::LifecycleState::started, "disable returns to started");
    app.stop();
    require(app.state() == openefb::LifecycleState::stopped, "application stops");
    require(!app.flight_loaded(), "stop clears flight state");
    require(log.size() == 5, "each lifecycle transition is logged");

    auto window_state = std::make_shared<FakeWindowState>();
    int created = 0;
    openefb::WindowController window_controller([&] {
        ++created;
        return std::make_unique<FakeWindow>(window_state);
    });

    require(!window_controller.created(), "window creation is lazy");
    require(window_controller.toggle(), "first toggle creates the window");
    require(window_controller.visible(), "first toggle shows the window");
    require(created == 1, "window is created once");
    require(window_controller.toggle(), "second toggle succeeds");
    require(!window_controller.visible(), "second toggle hides the window");
    window_controller.toggle();
    window_controller.hide();
    require(!window_controller.visible(), "explicit hide hides the window");
    window_controller.reset();
    require(!window_controller.created(), "reset releases the window");
    require(window_state->destroyed == 1, "reset destroys the owned window");

    openefb::WindowController failing_controller([] { return std::unique_ptr<openefb::WindowSurface>{}; });
    require(!failing_controller.toggle(), "failed window creation is reported");

    openefb::UiModel ui_model;
    require(ui_model.active_page() == openefb::EfbPage::home, "UI starts on the home page");
    require(ui_model.active_page_title() == "Home", "home page title is available");
    constexpr int navigation_x = 30;
    const int aircraft_y = openefb::navigation_top + 6 * (openefb::navigation_item_height +
                           openefb::navigation_item_gap) + 10;
    require(ui_model.select_at(navigation_x, aircraft_y), "aircraft navigation click is handled");
    require(ui_model.active_page() == openefb::EfbPage::aircraft, "navigation changes the active page");
    require(!ui_model.select_at(openefb::sidebar_width + 20, aircraft_y), "content clicks do not navigate");
    require(ui_model.active_page() == openefb::EfbPage::aircraft, "ignored clicks preserve the active page");
    ui_model.select_page(openefb::EfbPage::settings);
    require(ui_model.active_page_title() == "Settings", "pages can be selected directly");

    const openefb::WindowGeometry valid_geometry{100, 700, 960, 120};
    const openefb::WindowGeometry invalid_geometry{100, 200, 300, 100};
    require(valid_geometry.valid(), "normal window geometry is valid");
    require(valid_geometry.width() == 860 && valid_geometry.height() == 580, "geometry reports its size");
    require(!invalid_geometry.valid(), "undersized window geometry is rejected");
    auto compact_geometry = openefb::WindowGeometry{100, 700, 700, 260};
    compact_geometry.enforce_minimum(760, 560);
    require(compact_geometry.width() == 760 && compact_geometry.height() == 560,
            "restored geometry expands to the current layout minimum");

    openefb::TelemetryModel telemetry_model;
    require(!telemetry_model.snapshot().available, "telemetry starts unavailable");
    openefb::TelemetrySnapshot telemetry;
    telemetry.aircraft_name = "Cessna 172 SP";
    telemetry.latitude_degrees = 47.449;
    telemetry.longitude_degrees = -122.309;
    telemetry.altitude_feet = 5400.0;
    telemetry.ground_speed_knots = 118.0;
    telemetry.heading_degrees = 725.0;
    telemetry.vertical_speed_fpm = 450.0;
    telemetry_model.update(telemetry);
    require(telemetry_model.snapshot().available, "telemetry update marks data available");
    require(telemetry_model.snapshot().heading_degrees == 5.0, "heading is normalized");
    require(telemetry_model.snapshot().revision == 1, "telemetry updates have revisions");
    require(telemetry_model.snapshot().aircraft_name == "Cessna 172 SP", "aircraft identity is retained");
    telemetry_model.mark_unavailable();
    require(!telemetry_model.snapshot().available, "telemetry can be marked unavailable");

    openefb::FlightPlanModel flight_plan_model;
    require(!flight_plan_model.snapshot().available, "flight plan starts unavailable");
    openefb::FlightPlanSnapshot flight_plan;
    flight_plan.active_leg_index = 1;
    flight_plan.legs.push_back({0, "KSEA", openefb::WaypointKind::airport, 0, 47.449, -122.309, false});
    flight_plan.legs.push_back({1, "", openefb::WaypointKind::fix, 5000, 47.6, -122.0, false});
    flight_plan.legs.push_back({2, "KPDX", openefb::WaypointKind::airport, 0, 45.589, -122.597, false});
    flight_plan_model.update(flight_plan);
    require(flight_plan_model.snapshot().available, "flight plan update marks the service available");
    require(flight_plan_model.snapshot().legs[1].active, "active FMS leg is identified");
    require(flight_plan_model.snapshot().legs[1].identifier == "WPT 2", "unnamed legs receive a label");
    require(flight_plan_model.snapshot().revision == 1, "flight plan updates have revisions");
    flight_plan.active_leg_index = 99;
    flight_plan_model.update(flight_plan);
    require(flight_plan_model.snapshot().active_leg_index == -1,
            "out-of-range active legs are safely cleared");
    flight_plan_model.mark_unavailable();
    require(!flight_plan_model.snapshot().available, "flight plan can be marked unavailable");

    openefb::FlightPlanEditor flight_plan_editor;
    require(!flight_plan_editor.begin(flight_plan_model.snapshot()),
            "editor rejects an unavailable route");
    flight_plan_model.update(flight_plan);
    require(flight_plan_editor.begin(flight_plan_model.snapshot()),
            "editor starts from the live route");
    require(flight_plan_editor.legs().size() == 3 && !flight_plan_editor.dirty(),
            "a new draft preserves route legs without becoming dirty");
    require(flight_plan_editor.append_input('p') && flight_plan_editor.append_input('d') &&
                flight_plan_editor.input() == "PD",
            "waypoint entry normalizes identifiers to uppercase");
    require(!flight_plan_editor.append_input('-'), "waypoint entry rejects punctuation");
    openefb::FlightPlanLeg new_leg{0, "KPDX", openefb::WaypointKind::airport,
                                   3000, 45.589, -122.597, false};
    require(flight_plan_editor.insert_after_selection(new_leg),
            "a resolved waypoint is inserted after the selection");
    require(flight_plan_editor.selected_index() == 1 && flight_plan_editor.dirty(),
            "inserted waypoint becomes selected and marks the draft dirty");
    require(flight_plan_editor.move_selected_down() && flight_plan_editor.selected_index() == 2,
            "selected waypoint can move down");
    require(flight_plan_editor.move_selected_up() && flight_plan_editor.selected_index() == 1,
            "selected waypoint can move up");
    require(flight_plan_editor.remove_selected() && flight_plan_editor.legs().size() == 3,
            "selected waypoint can be removed");
    require(flight_plan_editor.source_unchanged(flight_plan_model.snapshot()),
            "unchanged live routes remain safe to apply");
    auto externally_changed_route = flight_plan_model.snapshot();
    externally_changed_route.legs.pop_back();
    require(!flight_plan_editor.source_unchanged(externally_changed_route),
            "external route changes are detected before apply");
    flight_plan_editor.cancel();
    require(!flight_plan_editor.active(), "cancel closes and clears the route draft");

    openefb::FlightPlanSnapshot empty_route;
    empty_route.available = true;
    openefb::FlightPlanEditor endpoint_editor;
    require(endpoint_editor.begin(empty_route), "an empty live route can open the builder");
    const openefb::FlightPlanLeg departure_leg{0, "KSEA", openefb::WaypointKind::airport,
                                                0, 47.449, -122.309, false};
    const openefb::FlightPlanLeg destination_leg{0, "KPDX", openefb::WaypointKind::airport,
                                                  0, 45.589, -122.597, false};
    require(endpoint_editor.set_destination(destination_leg) &&
                endpoint_editor.destination()->identifier == "KPDX" &&
                endpoint_editor.departure() == nullptr,
            "destination can be assigned before departure");
    require(endpoint_editor.set_departure(departure_leg) &&
                endpoint_editor.departure()->identifier == "KSEA" &&
                endpoint_editor.destination()->identifier == "KPDX",
            "departure and destination remain explicit endpoints");
    const openefb::FlightPlanLeg enroute_leg{0, "BTG", openefb::WaypointKind::vor,
                                              0, 45.748, -122.592, false};
    require(endpoint_editor.insert_after_selection(enroute_leg) &&
                endpoint_editor.legs()[1].identifier == "BTG" &&
                endpoint_editor.legs().back().identifier == "KPDX",
            "enroute additions stay before the assigned destination");

    std::istringstream airport_data{
        "I\n1200 Version\n"
        "1 433 0 0 KSEA Seattle Tacoma International\n"
        "100 45.72 1 0 0.25 1 2 1 16L 47.464000 -122.311000 0 0 3 8 1 1 "
        "34R 47.431000 -122.309000 0 0 3 8 1 1\n"
        "54 11990 Tower Legacy\n"
        "1054 119900 Seattle Tower\n"
        "1 31 0 0 KBFI Boeing Field\n99\n"};
    auto parsed_airport = openefb::parse_airport_apt(airport_data, "ksea");
    require(parsed_airport && parsed_airport->identifier == "KSEA" &&
                parsed_airport->name == "Seattle Tacoma International" &&
                parsed_airport->elevation_feet == 433,
            "apt.dat parser finds airport headers case-insensitively");
    require(parsed_airport->runways.size() == 1 &&
                parsed_airport->runways[0].identifiers == "16L / 34R" &&
                parsed_airport->runways[0].length_feet > 11000.0,
            "apt.dat parser derives runway dimensions from endpoints");
    require(parsed_airport->frequencies.size() == 1 &&
                std::abs(parsed_airport->frequencies[0].megahertz - 119.9) < 0.001,
            "modern 8.33 kHz frequencies supersede legacy airport frequencies");
    std::istringstream procedure_data{
        "SID:010,1,SEA6,RW16L,FIX1;\n"
        "SID:020,1,SEA6,RW16L,FIX2;\n"
        "STAR:010,2,OLM4,ALL,FIX3;\n"
        "APPCH:010,A,I16L,,FIX4;\n"};
    openefb::parse_airport_procedures(procedure_data, *parsed_airport);
    require(parsed_airport->procedures.departures.size() == 1 &&
                parsed_airport->procedures.departure_count == 1 &&
                parsed_airport->procedures.departures[0] == "SEA6" &&
                parsed_airport->procedures.arrivals[0] == "OLM4" &&
                parsed_airport->procedures.approaches[0] == "I16L",
            "CIFP parser lists unique SIDs, STARs, and approaches");

    openefb::WeatherModel weather_model;
    require(!weather_model.snapshot().available, "weather starts unavailable");
    openefb::WeatherSnapshot weather;
    weather.departure = {"KSEA", "KSEA 251653Z 18008KT 10SM FEW020 15/09 A3004"};
    weather.destination = {"KPDX", "KPDX 251653Z 22005KT 10SM SCT025 17/10 A3001"};
    weather.route_revision = 4;
    weather_model.update(weather);
    require(weather_model.snapshot().available, "weather update marks the service available");
    require(weather_model.snapshot().departure.airport_id == "KSEA",
            "departure weather is retained");
    require(weather_model.snapshot().destination.metar.find("KPDX") == 0,
            "destination METAR is retained");
    require(weather_model.snapshot().route_revision == 4,
            "weather records their source route revision");
    require(weather_model.snapshot().revision == 1, "weather updates have revisions");
    weather_model.mark_unavailable();
    require(!weather_model.snapshot().available, "weather can be marked unavailable");

    openefb::RouteProgressModel route_progress_model;
    require(!route_progress_model.snapshot().available, "route progress starts unavailable");
    openefb::TelemetrySnapshot progress_telemetry;
    progress_telemetry.available = true;
    progress_telemetry.latitude_degrees = 47.449;
    progress_telemetry.longitude_degrees = -122.309;
    progress_telemetry.ground_speed_knots = 120.0;
    progress_telemetry.revision = 7;
    openefb::FlightPlanSnapshot progress_route;
    progress_route.available = true;
    progress_route.revision = 8;
    progress_route.active_leg_index = 0;
    progress_route.legs.push_back(
        {0, "KPDX", openefb::WaypointKind::airport, 0, 45.589, -122.597, true});
    progress_route.legs.push_back(
        {1, "APPCH", openefb::WaypointKind::fix, 3000, 45.500, -122.700, false});
    route_progress_model.update(progress_telemetry, progress_route);
    const auto& route_progress = route_progress_model.snapshot();
    require(route_progress.available, "route progress is available with telemetry and a route");
    require(route_progress.active_waypoint.identifier == "KPDX",
            "active waypoint progress is identified");
    require(route_progress.destination.identifier == "KPDX",
            "the final airport remains the destination after approach fixes");
    require(route_progress.destination.distance_nm > 100.0 &&
                route_progress.destination.distance_nm < 120.0,
            "great-circle distance is calculated in nautical miles");
    require(route_progress.destination.bearing_degrees > 175.0 &&
                route_progress.destination.bearing_degrees < 200.0,
            "initial true bearing is calculated");
    require(route_progress.destination.ete_available &&
                route_progress.destination.ete_minutes > 50.0 &&
                route_progress.destination.ete_minutes < 60.0,
            "ETE uses live groundspeed");
    progress_telemetry.ground_speed_knots = 0.5;
    route_progress_model.update(progress_telemetry, progress_route);
    require(!route_progress_model.snapshot().destination.ete_available,
            "ETE is suppressed near zero groundspeed");
    route_progress_model.mark_unavailable();
    require(!route_progress_model.snapshot().available, "route progress can be marked unavailable");

    openefb::FuelModel fuel_model;
    require(!fuel_model.snapshot().available, "fuel starts unavailable");
    fuel_model.update(180.0, 36.0, 120.0, 9);
    require(fuel_model.snapshot().available, "fuel update marks data available");
    require(fuel_model.snapshot().fuel_remaining_kg == 180.0,
            "fuel remaining is retained in kilograms");
    require(fuel_model.snapshot().endurance_available &&
                fuel_model.snapshot().endurance_hours == 5.0,
            "fuel endurance uses total engine burn");
    require(fuel_model.snapshot().fuel_flow_us_gallons_per_hour > 13.2 &&
                fuel_model.snapshot().fuel_flow_us_gallons_per_hour < 13.3,
            "fuel flow is converted to US GPH on a standard avgas basis");
    require(fuel_model.snapshot().range_available &&
                fuel_model.snapshot().estimated_range_nm == 600.0,
            "fuel range uses live groundspeed");
    fuel_model.update(180.0, 0.0, 0.0, 10);
    require(!fuel_model.snapshot().endurance_available,
            "endurance is withheld without measurable fuel flow");
    require(!fuel_model.snapshot().range_available,
            "range is withheld without endurance and groundspeed");
    fuel_model.mark_unavailable();
    require(!fuel_model.snapshot().available, "fuel can be marked unavailable");

    openefb::MovingMapModel moving_map;
    require(moving_map.style() == openefb::MapStyle::street,
            "moving map defaults to OpenStreetMap street tiles");
    moving_map.select_style(openefb::MapStyle::topographic);
    require(moving_map.style() == openefb::MapStyle::topographic,
            "moving map can select topographic tiles");
    require(moving_map.layer_enabled(openefb::MapLayer::weather) &&
                moving_map.layer_enabled(openefb::MapLayer::airports) &&
                moving_map.layer_enabled(openefb::MapLayer::navaids) &&
                !moving_map.layer_enabled(openefb::MapLayer::airspace),
            "operational overlays have readable defaults");
    moving_map.toggle_layer(openefb::MapLayer::airspace);
    require(moving_map.layer_enabled(openefb::MapLayer::airspace),
            "airspace overlay can be enabled independently");
    require(moving_map.range_nm() == 40.0, "moving map starts at a useful regional range");
    require(moving_map.zoom_in() && moving_map.range_nm() == 20.0,
            "moving map can zoom in");
    require(moving_map.apply_wheel(-2) && moving_map.range_nm() == 80.0,
            "mouse wheel can zoom the moving map out");
    const auto same_position = moving_map.project(47.449, -122.309, 47.449, -122.309);
    require(same_position.valid && std::abs(same_position.east_nm) < 0.001 &&
                std::abs(same_position.north_nm) < 0.001,
            "aircraft position projects to map center");
    const auto portland = moving_map.project(47.449, -122.309, 45.589, -122.597);
    require(portland.valid && portland.east_nm < 0.0 && portland.north_nm < -100.0,
            "route points project to local nautical-mile offsets");
    const auto dateline = moving_map.project(0.0, 179.5, 0.0, -179.5);
    require(dateline.valid && dateline.east_nm > 59.0 && dateline.east_nm < 61.0,
            "map projection follows the short path across the date line");

    std::istringstream openair_data{
        "* sample airspace\n"
        "AC D\nAN TEST POLYGON\nAL SFC\nAH 4500 FT\n"
        "DP 47:00:00 N 122:00:00 W\n"
        "DP 47:10:00 N 122:00:00 W\n"
        "DP 47:10:00 N 122:10:00 W\n"
        "AC R\nAN TEST CIRCLE\nAL 1000 FT\nAH FL180\n"
        "V X=46:30:00 N 121:30:00 W\nDC 5\n"
        "AC C\nAN TEST ARC\nV X=46:00:00 N 121:00:00 W\nV D=-\nDA 10,90,0\n"};
    const auto airspace_zones = openefb::parse_openair(openair_data);
    require(airspace_zones.size() == 3 && airspace_zones[0].class_code == "D" &&
                airspace_zones[0].boundary.size() == 3,
            "OpenAIR parser reads polygon classes and DMS coordinates");
    require(airspace_zones[1].boundary.size() >= 40 &&
                airspace_zones[2].boundary.size() >= 10,
            "OpenAIR parser expands circles and directional arcs");
    openefb::AirspaceModel airspace_model;
    airspace_model.begin_load();
    require(airspace_model.snapshot().state == openefb::AirspaceLoadState::loading,
            "airspace model exposes background loading state");
    airspace_model.update(openefb::AirspaceLoadState::ready,
        std::make_shared<const std::vector<openefb::AirspaceZone>>(airspace_zones), "3 zones loaded");
    require(airspace_model.snapshot().zones->size() == 3 &&
                airspace_model.snapshot().revision == 2,
            "airspace model publishes immutable parsed snapshots");

    std::cout << "OpenEFB core tests passed\n";
    return EXIT_SUCCESS;
}
