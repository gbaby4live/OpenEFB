#include "openefb/core/application.hpp"

#include <utility>

namespace openefb {

Application::Application(LogSink log_sink) : log_sink_(std::move(log_sink)) {}

bool Application::start() {
    if (state_ != LifecycleState::stopped) {
        return false;
    }
    state_ = LifecycleState::started;
    log("started");
    return true;
}

void Application::stop() {
    if (state_ == LifecycleState::enabled) {
        disable();
    }
    if (state_ == LifecycleState::started) {
        state_ = LifecycleState::stopped;
        flight_loaded_ = false;
        log("stopped");
    }
}

bool Application::enable() {
    if (state_ != LifecycleState::started) {
        return false;
    }
    state_ = LifecycleState::enabled;
    log("enabled");
    return true;
}

void Application::disable() {
    if (state_ == LifecycleState::enabled) {
        state_ = LifecycleState::started;
        log("disabled");
    }
}

void Application::on_flight_loaded() {
    if (state_ == LifecycleState::enabled) {
        flight_loaded_ = true;
        log("flight loaded");
    }
}

LifecycleState Application::state() const noexcept { return state_; }

bool Application::flight_loaded() const noexcept { return flight_loaded_; }

void Application::log(std::string_view message) const {
    if (log_sink_) {
        log_sink_(message);
    }
}

} // namespace openefb
