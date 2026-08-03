#pragma once

#include "openefb/core/airport_info.hpp"
#include "openefb/core/briefing_model.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "openefb/core/traffic_model.hpp"
#include "openefb/core/weather_model.hpp"
#include "windows_tls.hpp"

#include <XPLMProcessing.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace openefb::xplane {

class XPlaneAirportData;
class XPlaneBriefingLibrary;
class XPlaneFlightPlan;

struct MobileServerStatus {
    bool running{false};
    std::string url;
    std::string pairing_code;
    std::string identity_code;
    std::string message{"Mobile companion stopped"};
};

class XPlaneMobileServer final {
public:
    XPlaneMobileServer(TelemetryModel& telemetry_model,
                       FlightPlanModel& flight_plan_model,
                       RouteProgressModel& route_progress_model,
                       WeatherModel& weather_model,
                       TrafficModel& traffic_model,
                       AirportInfoModel& airport_info_model,
                       BriefingModel& briefing_model,
                       XPlaneFlightPlan& xplane_flight_plan,
                       XPlaneAirportData& xplane_airport_data,
                       XPlaneBriefingLibrary& briefing_library,
                       std::filesystem::path web_root);
    ~XPlaneMobileServer();

    XPlaneMobileServer(const XPlaneMobileServer&) = delete;
    XPlaneMobileServer& operator=(const XPlaneMobileServer&) = delete;

    bool start();
    void stop();
    [[nodiscard]] MobileServerStatus status() const;

private:
    enum class CommandKind { apply_route, search_airport, apply_approach, refresh_library };
    struct MobileCommand {
        std::uint64_t id{};
        CommandKind kind{CommandKind::apply_route};
        std::uint64_t expected_revision{};
        std::vector<FlightPlanLeg> route;
        std::string airport;
        std::string approach;
        std::string transition;
        std::vector<std::string> excluded_fixes;
        bool destination_endpoint{true};
    };
    struct CommandResult {
        std::uint64_t id{};
        bool success{};
        std::string message;
    };

    void sample();
    void process_commands();
    void serve();
    void handle_client(std::uintptr_t client_socket);
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    TelemetryModel& telemetry_model_;
    FlightPlanModel& flight_plan_model_;
    RouteProgressModel& route_progress_model_;
    WeatherModel& weather_model_;
    TrafficModel& traffic_model_;
    AirportInfoModel& airport_info_model_;
    BriefingModel& briefing_model_;
    XPlaneFlightPlan& xplane_flight_plan_;
    XPlaneAirportData& xplane_airport_data_;
    XPlaneBriefingLibrary& briefing_library_;
    std::filesystem::path web_root_;
    mutable std::mutex mutex_;
    std::string snapshot_json_{"{\"version\":\"2.0\",\"telemetry\":{\"available\":false}}"};
    std::string airport_json_{"{\"state\":\"idle\"}"};
    std::string library_json_{"{\"entries\":[]}"};
    std::vector<std::filesystem::path> library_paths_;
    std::deque<MobileCommand> pending_commands_;
    std::deque<CommandResult> command_results_;
    std::uint64_t next_command_id_{1};
    std::string session_token_;
    WindowsTlsContext tls_context_;
    MobileServerStatus status_;
    std::thread worker_;
    std::atomic_bool stopping_{false};
    std::uintptr_t listen_socket_{};
    XPLMFlightLoopID flight_loop_id_{nullptr};
};

} // namespace openefb::xplane
