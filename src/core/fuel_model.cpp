#include "openefb/core/fuel_model.hpp"

#include <algorithm>
#include <cmath>

namespace openefb {

namespace {

constexpr double standard_avgas_kg_per_us_gallon = 2.72155422;

} // namespace

void FuelModel::update(double fuel_remaining_kg, double fuel_flow_kg_per_hour,
                       double ground_speed_knots, std::uint64_t telemetry_revision) {
    FuelSnapshot snapshot;
    snapshot.available = std::isfinite(fuel_remaining_kg) && std::isfinite(fuel_flow_kg_per_hour);
    snapshot.telemetry_revision = telemetry_revision;
    if (snapshot.available) {
        snapshot.fuel_remaining_kg = std::max(0.0, fuel_remaining_kg);
        snapshot.fuel_flow_kg_per_hour = std::max(0.0, fuel_flow_kg_per_hour);
        snapshot.fuel_flow_us_gallons_per_hour =
            snapshot.fuel_flow_kg_per_hour / standard_avgas_kg_per_us_gallon;
        if (snapshot.fuel_flow_kg_per_hour >= 0.01) {
            snapshot.endurance_available = true;
            snapshot.endurance_hours = snapshot.fuel_remaining_kg / snapshot.fuel_flow_kg_per_hour;
            if (std::isfinite(ground_speed_knots) && ground_speed_knots >= 1.0) {
                snapshot.range_available = true;
                snapshot.estimated_range_nm = snapshot.endurance_hours * ground_speed_knots;
            }
        }
    }
    snapshot.revision = next_revision_++;
    snapshot_ = snapshot;
}

void FuelModel::mark_unavailable() noexcept { snapshot_.available = false; }

const FuelSnapshot& FuelModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
