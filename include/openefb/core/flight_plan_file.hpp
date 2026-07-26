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

[[nodiscard]] FlightPlanFileResult parse_xplane_fms(std::istream& input);
[[nodiscard]] bool write_xplane_fms(std::ostream& output,
                                    const std::vector<FlightPlanLeg>& legs);
[[nodiscard]] std::string xplane_fms_filename(const std::vector<FlightPlanLeg>& legs);

} // namespace openefb
