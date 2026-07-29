#pragma once

#include "openefb/core/telemetry_model.hpp"

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>

#include <string>

namespace openefb::xplane {

class XPlaneTelemetry final {
public:
    explicit XPlaneTelemetry(TelemetryModel& model);
    ~XPlaneTelemetry();

    XPlaneTelemetry(const XPlaneTelemetry&) = delete;
    XPlaneTelemetry& operator=(const XPlaneTelemetry&) = delete;

    bool start();
    void stop();
    void refresh_aircraft_identity();

private:
    bool find_datarefs();
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    TelemetryModel& model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    XPLMDataRef latitude_{nullptr};
    XPLMDataRef longitude_{nullptr};
    XPLMDataRef elevation_{nullptr};
    XPLMDataRef indicated_altitude_{nullptr};
    XPLMDataRef ground_speed_{nullptr};
    XPLMDataRef heading_{nullptr};
    XPLMDataRef magnetic_heading_{nullptr};
    XPLMDataRef vertical_speed_{nullptr};
    XPLMDataRef aircraft_name_{nullptr};
    std::string aircraft_name_value_{"Unknown aircraft"};
};

} // namespace openefb::xplane
