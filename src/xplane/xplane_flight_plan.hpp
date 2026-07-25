#pragma once

#include "openefb/core/flight_plan_model.hpp"

#include <XPLMProcessing.h>

namespace openefb::xplane {

class XPlaneFlightPlan final {
public:
    explicit XPlaneFlightPlan(FlightPlanModel& model);
    ~XPlaneFlightPlan();

    XPlaneFlightPlan(const XPlaneFlightPlan&) = delete;
    XPlaneFlightPlan& operator=(const XPlaneFlightPlan&) = delete;

    bool start();
    void stop();

private:
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    FlightPlanModel& model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
};

} // namespace openefb::xplane
