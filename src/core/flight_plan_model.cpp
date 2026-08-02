#include "openefb/core/flight_plan_model.hpp"

#include <utility>
#include <cstddef>
#include <cmath>

namespace openefb {

std::vector<FlightPlanLeg> complete_flight_plan_legs(const FlightPlanSnapshot& snapshot) {
    if (snapshot.approach_legs.empty()) return snapshot.legs;
    auto result = snapshot.legs;
    const std::size_t insertion = !result.empty() &&
            result.back().kind == WaypointKind::airport
        ? result.size() - 1 : result.size();
    std::vector<FlightPlanLeg> navigable_approach;
    for (auto leg : snapshot.approach_legs) {
        if (leg.kind == WaypointKind::other) {
            if (!std::isfinite(leg.latitude_degrees) || !std::isfinite(leg.longitude_degrees) ||
                (leg.latitude_degrees == 0.0 && leg.longitude_degrees == 0.0)) continue;
            leg.kind = WaypointKind::coordinate;
        }
        navigable_approach.push_back(std::move(leg));
    }
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(insertion),
                  navigable_approach.begin(), navigable_approach.end());
    for (std::size_t index = 0; index < result.size(); ++index)
        result[index].index = static_cast<int>(index);
    return result;
}

void FlightPlanModel::update(FlightPlanSnapshot snapshot) {
    snapshot.available = true;
    bool active_leg_found = false;
    for (auto& leg : snapshot.legs) {
        leg.active = leg.index == snapshot.active_leg_index;
        active_leg_found = active_leg_found || leg.active;
        if (leg.identifier.empty()) {
            leg.identifier = "WPT " + std::to_string(leg.index + 1);
        }
    }
    if (!active_leg_found) {
        snapshot.active_leg_index = -1;
    }
    bool active_approach_leg_found = false;
    for (auto& leg : snapshot.approach_legs) {
        leg.active = leg.index == snapshot.active_approach_leg_index;
        active_approach_leg_found = active_approach_leg_found || leg.active;
        if (leg.identifier.empty()) {
            leg.identifier = "APP " + std::to_string(leg.index + 1);
        }
    }
    if (!active_approach_leg_found) snapshot.active_approach_leg_index = -1;
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

void FlightPlanModel::mark_unavailable() noexcept { snapshot_.available = false; }

const FlightPlanSnapshot& FlightPlanModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
