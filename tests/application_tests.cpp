#include "openefb/core/application.hpp"
#include "openefb/core/airspace_model.hpp"
#include "openefb/core/airport_info.hpp"
#include "openefb/core/briefing_model.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/flight_log_model.hpp"
#include "openefb/core/faa_chart_catalog.hpp"
#include "openefb/core/flight_plan_editor.hpp"
#include "openefb/core/flight_plan_file.hpp"
#include "openefb/core/fuel_model.hpp"
#include "openefb/core/geopdf.hpp"
#include "openefb/core/moving_map_model.hpp"
#include "openefb/core/map_poi.hpp"
#include "openefb/core/navigation_database_model.hpp"
#include "openefb/core/planning_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/traffic_model.hpp"
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
    const int briefing_y = openefb::navigation_top + 6 * (openefb::navigation_item_height +
                           openefb::navigation_item_gap) + 10;
    require(ui_model.select_at(navigation_x, briefing_y), "briefing navigation click is handled");
    require(ui_model.active_page() == openefb::EfbPage::briefing, "navigation changes the active page");
    require(!ui_model.select_at(openefb::sidebar_width + 20, briefing_y), "content clicks do not navigate");
    require(ui_model.active_page() == openefb::EfbPage::briefing, "ignored clicks preserve the active page");
    ui_model.select_page(openefb::EfbPage::settings);
    require(ui_model.active_page_title() == "Settings", "pages can be selected directly");
    ui_model.select_page(openefb::EfbPage::planning);
    require(ui_model.active_page_title() == "Planning", "aircraft planning replaces the fuel-only page");

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
    auto procedure_route = flight_plan_model.snapshot();
    procedure_route.approach_legs = {
        {0, "SARGS", openefb::WaypointKind::fix, 2000, 32.72, -117.25, false},
        {1, "APP 2", openefb::WaypointKind::other, 0, 0.0, 0.0, false},
        {2, "RW09", openefb::WaypointKind::coordinate, 71, 32.733, -117.189, true}};
    const auto complete_route = openefb::complete_flight_plan_legs(procedure_route);
    require(complete_route.size() == 5 && complete_route[2].identifier == "SARGS" &&
                complete_route[3].identifier == "RW09" &&
                complete_route.back().identifier == "KPDX",
            "flight plan overview merges navigable approach fixes before the destination");
    require(flight_plan_editor.begin(procedure_route) &&
                flight_plan_editor.legs().size() == complete_route.size() &&
                flight_plan_editor.select(2) && flight_plan_editor.remove_selected() &&
                flight_plan_editor.legs()[2].identifier == "RW09",
            "flight plan builder exposes approach fixes for individual removal");
    flight_plan_editor.cancel();

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
        "RWY:RW16L,     ,      ,00432, ,ISNQ,3,   ;N47274966,W122182790,0000;\n"
        "SID:010,1,SEA6,RW16L,FIX1;\n"
        "SID:020,1,SEA6,RW16L,FIX2;\n"
        "STAR:010,2,OLM4,ALL,FIX3;\n"
        "APPCH:010,A,I16L,KENMO,KENMO,K1,P,C,E  A, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,+,05000,     ,18000;\n"
        "APPCH:020,A,I16L,KENMO,HELZR,K1,P,C,E  B, ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,04000,     ,18000;\n"
        "APPCH:010,I,I16L, ,HELZR,K1,P,C,E  I, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,+,03200,     ,18000;\n"
        "APPCH:020,I,I16L, ,RW16L,K1,P,G,G  M, ,   ,CF, , , , , ,      ,    ,    ,    ,    , ,00432,     ,     ;\n"
        "APPCH:030,I,I16L, ,MISSD,K1,P,C,E  M, ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,03000,     ,     ;\n"};
    openefb::parse_airport_procedures(procedure_data, *parsed_airport);
    require(parsed_airport->procedures.departures.size() == 1 &&
                parsed_airport->procedures.departure_count == 1 &&
                parsed_airport->procedures.departures[0] == "SEA6" &&
                parsed_airport->procedures.arrivals[0] == "OLM4" &&
                parsed_airport->procedures.approaches[0] == "I16L",
            "CIFP parser lists unique SIDs, STARs, and approaches");
    require(parsed_airport->procedures.approach_details.size() == 1 &&
                parsed_airport->procedures.approach_details[0].display_name == "ILS RWY 16L" &&
                parsed_airport->procedures.approach_details[0].transitions.size() == 1 &&
                parsed_airport->procedures.approach_details[0].transitions[0].identifier == "KENMO" &&
                parsed_airport->procedures.approach_details[0].transitions[0].legs.size() == 2 &&
                parsed_airport->procedures.approach_details[0].final_legs.size() == 2 &&
                parsed_airport->procedures.approach_details[0].final_legs.back().runway &&
                std::abs(parsed_airport->procedures.approach_details[0].final_legs.back().latitude_degrees -
                         47.463794) < 0.0001 &&
                parsed_airport->procedures.approach_details[0].final_legs.back().identifier == "RW16L",
            "CIFP parser preserves transitions, final legs, runway coordinates, and excludes missed legs");
    openefb::AirportInfoSnapshot ksan_procedures;
    std::istringstream ksan_cifp{
        "RWY:RW09,     ,      ,00071, ,ISAN,3,   ;N32439500,W117115000,0000;\n"
        "APPCH:010,A,I09-Y,MZB,MZB,K2,D, ,V   , ,   ,IF, , , , , ,      ,    ,    ,    ,    , ,     ,     ,18000;\n"
        "APPCH:020,A,I09-Y,MZB,GATTO,K2,P,C,E   , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,02800,     ,     ;\n"
        "APPCH:030,A,I09-Y,MZB,GATTO,K2,P,C,E  A,R,   ,PI, ,BJC,K2,D, ,      ,2267,7270,2300,0100,+,02000,     ,     ;\n"
        "APPCH:040,A,I09-Y,MZB,SARGS,K2,E,A,EE B, ,   ,CF, ,ISAN,K2,P,I,      ,2750,0113,0950,0059,+,02000,     ,     ;\n"
        "APPCH:010,I,I09-Y, ,SARGS,K2,E,A,E  I, ,   ,IF, ,ISAN,K2,P,I,      ,2750,0113,    ,    ,J,02000,02000,18000;\n"};
    openefb::parse_airport_procedures(ksan_cifp, ksan_procedures);
    require(ksan_procedures.procedures.approach_details.size() == 1 &&
                ksan_procedures.procedures.approach_details[0].runway == "09" &&
                ksan_procedures.procedures.approach_details[0].transitions.size() == 1 &&
                ksan_procedures.procedures.approach_details[0].transitions[0].legs.size() == 4 &&
                ksan_procedures.procedures.approach_details[0].transitions[0].legs[1].identifier == "GATTO" &&
                ksan_procedures.procedures.approach_details[0].transitions[0].legs[2].identifier == "GATTO",
            "approach variants keep Y/Z in the procedure while normalizing the runway and preserving repeated transition legs");

    openefb::WeatherModel weather_model;
    require(!weather_model.snapshot().available, "weather starts unavailable");
    openefb::WeatherSnapshot weather;
    weather.departure = {"KSEA", "KSEA 251653Z 18008KT 10SM FEW020 15/09 A3004",
                         openefb::WeatherSource::online};
    weather.destination = {"KPDX", "KPDX 251653Z 22005KT 10SM SCT025 17/10 A3001",
                           openefb::WeatherSource::cache};
    weather.departure.taf = "KSEA 251720Z 2518/2624 18008KT P6SM SCT020";
    weather.departure.forecast_source = openefb::WeatherSource::online;
    weather.route_revision = 4;
    weather_model.update(weather);
    require(weather_model.snapshot().available, "weather update marks the service available");
    require(weather_model.snapshot().departure.airport_id == "KSEA",
            "departure weather is retained");
    require(weather_model.snapshot().destination.metar.find("KPDX") == 0,
            "destination METAR is retained");
    require(weather_model.snapshot().departure.source == openefb::WeatherSource::online &&
                weather_model.snapshot().destination.source == openefb::WeatherSource::cache,
            "weather source priority is visible to the UI");
    require(weather_model.snapshot().departure.taf.find("KSEA") == 0 &&
                weather_model.snapshot().departure.forecast_source ==
                    openefb::WeatherSource::online,
            "published TAF forecast and its source are retained independently of METAR");
    require(weather_model.snapshot().route_revision == 4,
            "weather records their source route revision");
    require(weather_model.snapshot().revision == 1, "weather updates have revisions");
    weather_model.mark_unavailable();
    require(!weather_model.snapshot().available, "weather can be marked unavailable");

    openefb::NavigationDatabaseModel navigation_database;
    navigation_database.begin_load();
    require(navigation_database.snapshot().loading, "navigation database reports loading");
    navigation_database.update({{"KSEA", "Seattle Tacoma Intl", openefb::WaypointKind::airport,
                                 47.449, -122.309}}, false);
    const auto navigation_snapshot = navigation_database.snapshot();
    require(navigation_snapshot.available && navigation_snapshot.points->size() == 1,
            "navigation database publishes map points");
    require(navigation_snapshot.points->front().identifier == "KSEA",
            "navigation map points retain identifiers");

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

    fuel_model.update(180.0, 36.0, 120.0, 11);
    progress_telemetry.ground_speed_knots = 120.0;
    route_progress_model.update(progress_telemetry, progress_route);
    openefb::PlanningModel planning_model;
    const openefb::AircraftLoading loading{780.0, 180.0, 180.0, 1140.0,
                                            1300.0, 260.0, 0.08};
    planning_model.update(loading, fuel_model.snapshot(), route_progress_model.snapshot());
    const auto& plan = planning_model.snapshot();
    require(plan.available && !plan.overweight && plan.gross_margin_kg == 160.0,
            "planning model evaluates aircraft-specific gross weight limits");
    require(plan.fuel_plan_available && plan.trip_fuel_kg > 30.0 &&
                plan.reserve_fuel_kg == 27.0 && plan.fuel_margin_kg > 115.0 &&
                plan.fuel_flow_us_gallons_per_hour > 13.2,
            "planning model combines route ETE, live burn, and reserve fuel");
    require(plan.predicted_landing_weight_kg < loading.gross_weight_kg && plan.dispatch_ready,
            "planning model predicts landing weight and a passing dispatch outlook");
    require(planning_model.adjust_reserve_minutes(15) &&
                planning_model.snapshot().reserve_minutes == 60,
            "reserve time can be adjusted in bounded increments");
    planning_model.update(loading, fuel_model.snapshot(), route_progress_model.snapshot());
    require(planning_model.snapshot().reserve_fuel_kg == 36.0,
            "changed reserve time is reflected in the next live calculation");
    auto overloaded = loading;
    overloaded.gross_weight_kg = 1350.0;
    planning_model.update(overloaded, fuel_model.snapshot(), route_progress_model.snapshot());
    require(planning_model.snapshot().overweight && !planning_model.snapshot().dispatch_ready,
            "overweight aircraft never receive a passing planning outlook");

    openefb::BriefingModel briefing_model;
    require(briefing_model.active_tab() == openefb::BriefingTab::summary,
            "briefing starts on the combined flight summary");
    briefing_model.select_tab(openefb::BriefingTab::checklist);
    require(briefing_model.toggle_checklist_item(0) &&
                briefing_model.completed_checklist_items() == 1,
            "briefing checklist items can be completed independently");
    briefing_model.reset_checklist();
    require(briefing_model.completed_checklist_items() == 0,
            "briefing checklist can be reset for a new flight");
    briefing_model.configure_checklist_for_aircraft("Cessna 172 SP");
    briefing_model.select_checklist_phase(openefb::ChecklistPhase::takeoff_cruise);
    require(briefing_model.checklist_aircraft() == "Cessna 172 SP" &&
                briefing_model.checklist_phase() == openefb::ChecklistPhase::takeoff_cruise &&
                !briefing_model.checklist().empty(),
            "offline checklists expose aircraft context and selectable flight phases");
    briefing_model.set_notes("Departure note");
    require(briefing_model.append_note('\n') && briefing_model.append_note('A') &&
                briefing_model.notes() == "Departure note\nA",
            "flight notes accept multiline printable input");
    require(briefing_model.backspace_note() && briefing_model.notes() == "Departure note\n",
            "flight notes support editing");
    std::vector<openefb::LibraryEntry> library_entries{
        {openefb::LibraryCategory::chart, "KSEA chart.pdf", "Charts/KSEA chart.pdf", {}},
        {openefb::LibraryCategory::document, "Checklist.txt", "Documents/Checklist.txt", "Line one"},
    };
    briefing_model.update_library(std::move(library_entries), "Local library");
    require(briefing_model.library().size() == 2 && briefing_model.selected_entry() &&
                briefing_model.selected_entry()->category == openefb::LibraryCategory::chart,
            "local chart and document entries are published to the briefing model");
    require(briefing_model.select_entry(1) &&
                briefing_model.selected_entry()->text_content == "Line one",
            "briefing library selection exposes in-EFB text documents");
    const auto selected_library_path = briefing_model.selected_entry()->path;
    briefing_model.select_library_airport("kpdx");
    briefing_model.update_library({{openefb::LibraryCategory::chart, "KSEA/Arrival.pdf", "chart.pdf", {}},
                                    {openefb::LibraryCategory::document, "KPDX/Briefing.txt",
                                     selected_library_path, "Updated"}}, "Refreshed");
    require(briefing_model.library_airport() == "KPDX" && briefing_model.selected_entry() &&
                briefing_model.selected_entry()->path == selected_library_path,
            "library refresh preserves airport filter and selected document");
    require(openefb::faa_cycle_for_date(2026, 7, 25) == "2607" &&
                openefb::faa_cycle_for_date(2026, 8, 6) == "2608" &&
                openefb::faa_cycle_for_date(2026, 1, 10) == "2513",
            "FAA chart cycle selection follows 28-day effective dates across years");
    const std::string faa_catalog =
        "<digital_tpp><state_code><airport_name ID=\"SEATTLE\" icao_ident=\" KSEA \">"
        "<record><chart_code>APD</chart_code><chart_name>AIRPORT &amp; DIAGRAM</chart_name>"
        "<pdf_name>00582AD.PDF</pdf_name></record>"
        "<record><chart_code>IAP</chart_code><chart_name>ILS RWY 16L</chart_name>"
        "<pdf_name>00582I16L.PDF</pdf_name></record></airport_name>"
        "<airport_name ID=\"PORTLAND\" icao_ident=\"KPDX\"><record>"
        "<chart_code>APD</chart_code><chart_name>AIRPORT DIAGRAM</chart_name>"
        "<pdf_name>00330AD.PDF</pdf_name></record></airport_name></state_code></digital_tpp>";
    const auto faa_charts = openefb::parse_faa_chart_catalog(faa_catalog, "ksea");
    require(faa_charts.size() == 2 && faa_charts[0].code == "APD" &&
                faa_charts[0].name == "AIRPORT & DIAGRAM" &&
                faa_charts[1].pdf_name == "00582I16L.PDF",
            "FAA d-TPP catalog parser selects and decodes one airport's charts");
    const std::string faa_domestic_catalog =
        "<airport_name ID=\"SMALL FIELD\" apt_ident=\"ABC\" icao_ident=\"\">"
        "<record><chart_code>APD</chart_code><chart_name>AIRPORT DIAGRAM</chart_name>"
        "<useraction></useraction><pdf_name>00123AD.PDF</pdf_name></record>"
        "<record><chart_code>STAR</chart_code><chart_name>DELETED</chart_name>"
        "<useraction>D</useraction><pdf_name>DEL_APT_SERVED.PDF</pdf_name></record>"
        "</airport_name>";
    const auto domestic_charts = openefb::parse_faa_chart_catalog(faa_domestic_catalog, "KABC");
    require(domestic_charts.size() == 1 && domestic_charts.front().pdf_name == "00123AD.PDF",
            "FAA parser falls back to domestic identifiers and ignores deletion placeholders");

    const std::string geopdf_metadata =
        "/MediaBox[0 0 387.36 594] /VP[<</BBox[9.18 2.628 378.18 591.372]"
        "/Measure<</GPTS[32.36286 -117.09282 32.36285 -116.53839 "
        "33.11397 -116.53576 33.11398 -117.09540]"
        "/LPTS[0.1 0.1 0.9 0.1 0.9 0.9 0.1 0.9]>>>>]";
    const auto geopdf = openefb::parse_geopdf_reference(geopdf_metadata);
    const auto chart_center = geopdf ? openefb::project_geopdf_position(
        *geopdf, 32.7384, -116.8155) : std::nullopt;
    require(geopdf && chart_center && chart_center->x > 180.0 && chart_center->x < 210.0 &&
                chart_center->y > 280.0 && chart_center->y < 315.0,
            "FAA GeoPDF calibration projects live aircraft coordinates onto an approach plate");
    require(!openefb::project_geopdf_position(*geopdf, 40.0, -116.8),
            "aircraft outside an approach plate's published vicinity is not drawn on the chart");

    const auto places = openefb::parse_overpass_pois(
        "101\t24.556\t-81.782\tHarbor Cafe\tcafe\t\t\t\t\n"
        "102\t24.560\t-81.790\tIsland Golf Club\t\t\t\tyes\t\n"
        "103\t24.551\t-81.800\tHistoric Fort\t\tattraction\t\t\tfort\n");
    require(places.size() == 3 && places[0].category == openefb::PoiCategory::food &&
                places[1].category == openefb::PoiCategory::golf &&
                places[2].category == openefb::PoiCategory::attraction,
            "Overpass place data becomes independent food, golf, and attraction layers");

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
    require(moving_map.poi_enabled(), "points of interest are visible by default");
    moving_map.toggle_pois();
    require(!moving_map.poi_enabled() &&
                !moving_map.layer_enabled(openefb::MapLayer::food) &&
                !moving_map.layer_enabled(openefb::MapLayer::golf) &&
                !moving_map.layer_enabled(openefb::MapLayer::attractions),
            "one POI switch hides every place category");
    moving_map.toggle_pois();
    require(moving_map.poi_enabled(), "one POI switch restores every place category");
    require(moving_map.range_nm() == 40.0, "moving map starts at a useful regional range");
    require(moving_map.zoom_in() && moving_map.range_nm() == 20.0,
            "moving map can zoom in");
    require(moving_map.apply_wheel(-2) && moving_map.range_nm() == 80.0,
            "mouse wheel can zoom the moving map out");
    moving_map.update_aircraft_position(47.449, -122.309);
    require(moving_map.following_aircraft() && moving_map.center_available(),
            "moving map initially follows the live aircraft");
    moving_map.pan_by(2.0, 1.0);
    require(!moving_map.following_aircraft() &&
                moving_map.center_longitude_degrees() > -122.309,
            "dragging creates an independent map center");
    moving_map.recenter_on_aircraft();
    require(moving_map.following_aircraft() &&
                std::abs(moving_map.center_latitude_degrees() - 47.449) < 0.001,
            "map recenter returns to the live aircraft");
    while (moving_map.zoom_in()) {}
    require(moving_map.range_nm() == 0.005,
            "airport surface zoom reaches approximately a 30-foot map radius");
    const auto same_position = moving_map.project(47.449, -122.309, 47.449, -122.309);
    require(same_position.valid && std::abs(same_position.east_nm) < 0.001 &&
                std::abs(same_position.north_nm) < 0.001,
            "aircraft position projects to map center");
    const auto portland = moving_map.project(47.449, -122.309, 45.589, -122.597);
    require(portland.valid && portland.east_nm < 0.0 && portland.north_nm < -100.0,
            "route points project to local nautical-mile offsets");
    const auto round_trip = moving_map.unproject(47.449, -122.309,
                                                  portland.east_nm, portland.north_nm);
    require(round_trip.valid && std::abs(round_trip.latitude_degrees - 45.589) < 0.001 &&
                std::abs(round_trip.longitude_degrees - (-122.597)) < 0.001,
            "clickable map coordinates invert the moving-map projection");
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

    std::istringstream fms_input{
        "I\n1100 Version\nCYCLE 2607\nADEP KSEA\nADES KPDX\nNUMENR 3\n"
        "1 KSEA ADEP 433 47.449000 -122.309000\n"
        "11 OLM DRCT 8000 46.971000 -122.902000\n"
        "1 KPDX ADES 31 45.589000 -122.597000\n"};
    const auto imported_plan = openefb::parse_xplane_fms(fms_input);
    require(imported_plan.success && imported_plan.legs.size() == 3 &&
                imported_plan.legs.front().kind == openefb::WaypointKind::airport &&
                imported_plan.legs[1].kind == openefb::WaypointKind::fix &&
                imported_plan.legs.back().identifier == "KPDX",
            "X-Plane 11/12 FMS plans import with endpoint and waypoint data");
    std::ostringstream fms_output;
    require(openefb::write_xplane_fms(fms_output, imported_plan.legs) &&
                fms_output.str().find("NUMENR 3") != std::string::npos &&
                fms_output.str().find("ADEP KSEA") != std::string::npos,
            "active routes export in X-Plane 1100 FMS format");
    std::ostringstream current_cycle_fms;
    require(openefb::write_xplane_fms(current_cycle_fms, imported_plan.legs, "2406") &&
                current_cycle_fms.str().find("CYCLE 2406") != std::string::npos,
            "generated FMS plans carry X-Plane's active AIRAC cycle");
    std::ostringstream approach_fms;
    const openefb::FlightPlanApproachSelection approach_selection{
        "KPDX", "10L", "I10L", "BTG", "2607"};
    require(openefb::write_xplane_fms_with_approach(
                approach_fms, imported_plan.legs, approach_selection) &&
                approach_fms.str().find("DESRWY RW10L") != std::string::npos &&
                approach_fms.str().find("APP I10L") != std::string::npos &&
                approach_fms.str().find("APPTRANS BTG") != std::string::npos &&
                approach_fms.str().find("NUMENR 3") != std::string::npos,
            "procedure-aware FMS plans preserve approach and transition metadata for VPATH");
    std::ostringstream variant_approach_fms;
    const openefb::FlightPlanApproachSelection variant_selection{
        "KSAN", "09", "I09-Y", "MZB", "2406"};
    auto ksan_route = imported_plan.legs;
    ksan_route.back().identifier = "KSAN";
    require(openefb::write_xplane_fms_with_approach(
                variant_approach_fms, ksan_route, variant_selection) &&
                variant_approach_fms.str().find("DESRWY RW09\n") != std::string::npos &&
                variant_approach_fms.str().find("DESRWY RW09-Y") == std::string::npos &&
                variant_approach_fms.str().find("APP I09-Y\n") != std::string::npos,
            "Y/Z approach variants never become invalid runway identifiers in X-Plane plans");
    require(openefb::xplane_fms_filename(imported_plan.legs) == "KSEA-KPDX.fms",
            "exported FMS filenames identify departure and destination");
    auto unusual_filename_legs = imported_plan.legs;
    unusual_filename_legs.front().identifier = "K/SEA";
    require(openefb::xplane_fms_filename(unusual_filename_legs) == "KSEA-KPDX.fms",
            "exported route names remove filesystem-unsafe characters");
    std::istringstream invalid_fms{"I\n1100 Version\nNUMENR 2\n1 KSEA ADEP 0 1 2\n"};
    require(!openefb::parse_xplane_fms(invalid_fms).success,
            "incomplete FMS plans are rejected safely");

    openefb::TrafficModel traffic_model;
    traffic_model.update({
        {0xABC123, "TEST123", "B738", 47.5, -122.3, 6000.0, 180.0, 220.0, 500.0, false},
        {0xBAD001, "INVALID", "C172", 120.0, 0.0, 1000.0, 0.0, 0.0, 0.0, false},
    }, openefb::TrafficSource::blended, 1, 1,
       "ADSB.LOL request failed / retry 15s / TCAS 1", true);
    require(traffic_model.snapshot().available &&
                traffic_model.snapshot().targets.size() == 1 &&
                traffic_model.snapshot().targets.front().callsign == "TEST123" &&
                traffic_model.snapshot().source == openefb::TrafficSource::blended &&
                traffic_model.snapshot().online_target_count == 1 &&
                traffic_model.snapshot().online_degraded,
            "traffic model publishes source-aware targets and rejects invalid coordinates");
    traffic_model.set_online_range_nm(10);
    require(traffic_model.snapshot().online_range_nm == 25,
            "traffic range clamps to the provider-safe minimum");
    traffic_model.set_online_range_nm(500);
    require(traffic_model.snapshot().online_range_nm == 200,
            "traffic range clamps to the provider-safe maximum");
    traffic_model.set_online_range_nm(100);
    const auto route_revision = traffic_model.snapshot().route_request_revision;
    traffic_model.request_route_lookup("TEST123");
    require(traffic_model.snapshot().route_request_callsign == "TEST123" &&
                traffic_model.snapshot().route_request_revision == route_revision + 1,
            "traffic model publishes on-demand route lookup requests for selected targets");
    traffic_model.set_injection_requested(true);
    traffic_model.set_injection_state(true, "Active - 1 online target in X-Plane TCAS");
    require(traffic_model.snapshot().injection_requested &&
                traffic_model.snapshot().injection_active &&
                traffic_model.snapshot().injection_status.find("Active") != std::string::npos,
            "traffic model exposes requested and active TCAS injection state");
    traffic_model.set_visual_traffic_requested(true);
    traffic_model.set_visual_traffic_state(true, "Active - 1 moving 3D aircraft within 25 NM");
    require(traffic_model.snapshot().visual_traffic_requested &&
                traffic_model.snapshot().visual_traffic_active &&
                traffic_model.snapshot().visual_traffic_status.find("3D") != std::string::npos,
            "traffic model exposes optional exterior 3D traffic state");
    traffic_model.set_visual_traffic_requested(false);
    require(!traffic_model.snapshot().visual_traffic_active &&
                traffic_model.snapshot().visual_traffic_status == "Disabled",
            "disabling exterior traffic clears its active state");
    traffic_model.set_injection_requested(false);
    require(!traffic_model.snapshot().injection_requested &&
                !traffic_model.snapshot().injection_active,
            "disabling traffic injection clears the active state");
    traffic_model.mark_unavailable();
    require(!traffic_model.snapshot().available && traffic_model.snapshot().targets.empty() &&
                !traffic_model.snapshot().online_degraded,
            "traffic model clears stale targets when the simulator source stops");

    openefb::FlightPlanSnapshot log_route;
    log_route.available = true;
    log_route.legs = {
        {0, "KSEA", openefb::WaypointKind::airport, 0, 47.449, -122.309, false},
        {1, "KPDX", openefb::WaypointKind::airport, 0, 45.589, -122.597, false},
    };
    openefb::TelemetrySnapshot log_telemetry;
    log_telemetry.available = true;
    log_telemetry.aircraft_name = "C172";
    log_telemetry.latitude_degrees = 47.449;
    log_telemetry.longitude_degrees = -122.309;
    log_telemetry.altitude_feet = 500.0;
    log_telemetry.ground_speed_knots = 10.0;
    openefb::FlightLogModel flight_log;
    flight_log.update(log_telemetry, log_route, true, 1.0, "2026-07-27 10:00 UTC");
    log_telemetry.ground_speed_knots = 70.0;
    log_telemetry.altitude_feet = 1200.0;
    log_telemetry.latitude_degrees += 0.01;
    flight_log.update(log_telemetry, log_route, false, 31.0, "2026-07-27 10:01 UTC");
    require(flight_log.snapshot().airborne && flight_log.snapshot().departure == "KSEA" &&
                flight_log.snapshot().destination == "KPDX",
            "flight logger starts airborne route tracking from the active plan");
    log_telemetry.latitude_degrees += 0.01;
    log_telemetry.altitude_feet = 2500.0;
    log_telemetry.ground_speed_knots = 135.0;
    log_telemetry.vertical_speed_fpm = 900.0;
    flight_log.update(log_telemetry, log_route, false, 60.0, "2026-07-27 10:02 UTC");
    log_telemetry.ground_speed_knots = 20.0;
    log_telemetry.vertical_speed_fpm = -420.0;
    flight_log.update(log_telemetry, log_route, true, 1.0, "2026-07-27 10:03 UTC");
    require(!flight_log.snapshot().entries.empty() &&
                flight_log.snapshot().entries.front().maximum_altitude_feet >= 2500.0 &&
                flight_log.snapshot().entries.front().landing_vertical_speed_fpm == -420.0 &&
                flight_log.snapshot().entries.front().maximum_ground_speed_knots >= 135.0 &&
                flight_log.snapshot().entries.front().maximum_climb_rate_fpm >= 900.0 &&
                flight_log.snapshot().entries.front().completed_utc == "2026-07-27 10:03 UTC" &&
                flight_log.snapshot().entries.front().track.size() >= 3,
            "flight logger records timestamped landing metrics and route track");

    std::cout << "OpenEFB core tests passed\n";
    return EXIT_SUCCESS;
}
