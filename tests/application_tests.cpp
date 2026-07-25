#include "openefb/core/application.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

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

    std::cout << "OpenEFB core tests passed\n";
    return EXIT_SUCCESS;
}
