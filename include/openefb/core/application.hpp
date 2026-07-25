#pragma once

#include <functional>
#include <string_view>

namespace openefb {

using LogSink = std::function<void(std::string_view)>;

enum class LifecycleState {
    stopped,
    started,
    enabled,
};

class Application final {
public:
    explicit Application(LogSink log_sink = {});

    bool start();
    void stop();
    bool enable();
    void disable();
    void on_flight_loaded();

    [[nodiscard]] LifecycleState state() const noexcept;
    [[nodiscard]] bool flight_loaded() const noexcept;

private:
    void log(std::string_view message) const;

    LogSink log_sink_;
    LifecycleState state_{LifecycleState::stopped};
    bool flight_loaded_{false};
};

} // namespace openefb
