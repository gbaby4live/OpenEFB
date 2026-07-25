#include "openefb/core/application.hpp"
#include "openefb/core/window_controller.hpp"

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

    std::cout << "OpenEFB core tests passed\n";
    return EXIT_SUCCESS;
}
