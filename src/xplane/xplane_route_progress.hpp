#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/route_progress_model.hpp"
#include "openefb/core/telemetry_model.hpp"

#include <XPLMProcessing.h>

namespace openefb::xplane {

class XPlaneRouteProgress final {
public:
    XPlaneRouteProgress(RouteProgressModel& model, const TelemetryModel& telemetry_model,
                        const FlightPlanModel& flight_plan_model);
    ~XPlaneRouteProgress();

    XPlaneRouteProgress(const XPlaneRouteProgress&) = delete;
    XPlaneRouteProgress& operator=(const XPlaneRouteProgress&) = delete;

    bool start();
    void stop();

private:
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    RouteProgressModel& model_;
    const TelemetryModel& telemetry_model_;
    const FlightPlanModel& flight_plan_model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
};

} // namespace openefb::xplane
