#include "openefb/core/application.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/ui_model.hpp"
#include "openefb/core/window_controller.hpp"
#include "openefb/core/window_geometry.hpp"
#include "openefb/core/weather_model.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
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
    const int aircraft_y = openefb::navigation_top + 3 * (openefb::navigation_item_height +
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

    std::cout << "OpenEFB core tests passed\n";
    return EXIT_SUCCESS;
}
