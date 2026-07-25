#include "openefb/core/flight_plan_model.hpp"

#include <utility>

namespace openefb {

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
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

void FlightPlanModel::mark_unavailable() noexcept { snapshot_.available = false; }

const FlightPlanSnapshot& FlightPlanModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
