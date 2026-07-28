#pragma once

#include "openefb/core/traffic_model.hpp"
#include "openefb/core/telemetry_model.hpp"
#include "xplane_online_traffic.hpp"

#include <XPLMDataAccess.h>
#include <XPLMPlanes.h>
#include <XPLMProcessing.h>

namespace openefb::xplane {

class XPlaneTraffic final {
public:
    XPlaneTraffic(TrafficModel& model, TelemetryModel& telemetry_model);
    ~XPlaneTraffic();
    bool start();
    void stop();
    void on_release_requested();

private:
    bool find_datarefs();
    void sample();
    void update_injection();
    void try_acquire_tcas();
    void release_tcas(std::string status);
    void publish_injection_state(bool active, std::string status);
    static void planes_available(void* refcon);
    static float flight_loop(float, float, int, void* refcon);

    TrafficModel& model_;
    TelemetryModel& telemetry_model_;
    XPlaneOnlineTraffic online_traffic_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    bool tcas_available_{false};
    XPLMDataRef mode_s_id_{nullptr};
    XPLMDataRef ssr_mode_{nullptr};
    XPLMDataRef flight_id_{nullptr};
    XPLMDataRef aircraft_type_{nullptr};
    XPLMDataRef latitude_{nullptr};
    XPLMDataRef longitude_{nullptr};
    XPLMDataRef elevation_{nullptr};
    XPLMDataRef track_{nullptr};
    XPLMDataRef speed_{nullptr};
    XPLMDataRef vertical_speed_{nullptr};
    XPLMDataRef on_ground_{nullptr};
    XPLMDataRef override_tcas_{nullptr};
    XPLMDataRef relative_bearing_{nullptr};
    XPLMDataRef relative_distance_{nullptr};
    XPLMDataRef relative_altitude_{nullptr};
    XPLMDataRef target_heading_{nullptr};
    bool injection_datarefs_available_{false};
    bool owns_tcas_{false};
    bool waiting_for_tcas_{false};
    bool yielded_to_provider_{false};
    std::uint64_t last_route_request_revision_{0};
    float sample_elapsed_seconds_{1.0F};
};

} // namespace openefb::xplane
