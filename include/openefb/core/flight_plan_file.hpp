#pragma once

#include "openefb/core/flight_plan_model.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace openefb {

struct FlightPlanFileResult {
    bool success{false};
    std::vector<FlightPlanLeg> legs;
    std::string message;
};

struct FlightPlanApproachSelection {
    std::string airport_identifier;
    std::string runway;
    std::string approach_identifier;
    std::string transition_identifier;
    std::string cycle{"0000"};
};

[[nodiscard]] FlightPlanFileResult parse_xplane_fms(std::istream& input);
[[nodiscard]] bool write_xplane_fms(std::ostream& output,
                                    const std::vector<FlightPlanLeg>& legs,
                                    std::string cycle = "0000");
[[nodiscard]] bool write_xplane_fms_with_approach(
    std::ostream& output, const std::vector<FlightPlanLeg>& legs,
    const FlightPlanApproachSelection& approach);
[[nodiscard]] std::string xplane_fms_filename(const std::vector<FlightPlanLeg>& legs);

} // namespace openefb
