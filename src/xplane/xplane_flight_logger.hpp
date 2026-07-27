#pragma once

#include "openefb/core/flight_log_model.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/telemetry_model.hpp"

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>

namespace openefb::xplane {
class XPlaneFlightLogger final {
public:
    XPlaneFlightLogger(FlightLogModel& model, const TelemetryModel& telemetry,
                       const FlightPlanModel& flight_plan);
    ~XPlaneFlightLogger();
    bool start();
    void stop();
private:
    void sample(float elapsed_seconds);
    static float flight_loop(float elapsed, float, int, void* refcon);
    FlightLogModel& model_;
    const TelemetryModel& telemetry_;
    const FlightPlanModel& flight_plan_;
    XPLMDataRef on_ground_{nullptr};
    XPLMFlightLoopID flight_loop_id_{nullptr};
};
} // namespace openefb::xplane
