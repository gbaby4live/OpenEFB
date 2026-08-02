#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/airport_info.hpp"

#include <XPLMProcessing.h>

#include <optional>
#include <array>
#include <filesystem>
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
    [[nodiscard]] FlightPlanEditResult apply_approach(
        const ApproachProcedure& procedure, std::string transition_identifier,
        std::string airport_identifier, bool destination_endpoint,
        const std::vector<std::string>& excluded_fixes = {});
    [[nodiscard]] FlightPlanEditResult insert_after_active(FlightPlanLeg leg,
                                                            std::string display_name);
    [[nodiscard]] FlightPlanEditResult remove_route_leg(std::size_t index,
                                                         std::string display_name);
    [[nodiscard]] FlightPlanEditResult import_latest(const std::filesystem::path& directory);
    [[nodiscard]] FlightPlanEditResult export_current(const std::filesystem::path& directory) const;

private:
    struct AppliedApproach {
        std::string airport_identifier;
        std::vector<std::string> route_signature;
        std::size_t insertion_start{};
        std::size_t insertion_count{};
    };

    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    FlightPlanModel& model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    std::array<std::optional<AppliedApproach>, 2> applied_approaches_;
};

} // namespace openefb::xplane
