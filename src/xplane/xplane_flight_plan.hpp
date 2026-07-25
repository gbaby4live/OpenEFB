#pragma once

#include "openefb/core/flight_plan_model.hpp"

#include <XPLMProcessing.h>

#include <optional>
#include <string>
#include <vector>

namespace openefb::xplane {

struct FlightPlanEditResult {
    bool success{false};
    std::string message;
};

class XPlaneFlightPlan final {
public:
    explicit XPlaneFlightPlan(FlightPlanModel& model);
    ~XPlaneFlightPlan();

    XPlaneFlightPlan(const XPlaneFlightPlan&) = delete;
    XPlaneFlightPlan& operator=(const XPlaneFlightPlan&) = delete;

    bool start();
    void stop();
    void refresh();

    [[nodiscard]] std::optional<FlightPlanLeg> find_waypoint(
        std::string identifier, double near_latitude, double near_longitude) const;
    [[nodiscard]] FlightPlanEditResult apply_route(const std::vector<FlightPlanLeg>& legs);

private:
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    FlightPlanModel& model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
};

} // namespace openefb::xplane
