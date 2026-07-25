#include "openefb/core/planning_model.hpp"

#include <algorithm>
#include <cmath>

namespace openefb {

void PlanningModel::update(AircraftLoading loading, const FuelSnapshot& fuel,
                           const RouteProgressSnapshot& progress) {
    const int reserve_minutes = snapshot_.reserve_minutes;
    snapshot_ = {};
    snapshot_.reserve_minutes = reserve_minutes;
    snapshot_.loading = loading;
    snapshot_.available = std::isfinite(loading.empty_weight_kg) &&
        std::isfinite(loading.payload_weight_kg) && std::isfinite(loading.fuel_weight_kg) &&
        std::isfinite(loading.gross_weight_kg) && std::isfinite(loading.maximum_gross_weight_kg) &&
        loading.empty_weight_kg > 0.0 && loading.gross_weight_kg > 0.0 &&
        loading.maximum_gross_weight_kg > 0.0;
    if (snapshot_.available) calculate(fuel, progress);
    snapshot_.revision = next_revision_++;
}

void PlanningModel::calculate(const FuelSnapshot& fuel, const RouteProgressSnapshot& progress) {
    snapshot_.gross_margin_kg = snapshot_.loading.maximum_gross_weight_kg -
                                snapshot_.loading.gross_weight_kg;
    snapshot_.loading_percent = 100.0 * snapshot_.loading.gross_weight_kg /
                                snapshot_.loading.maximum_gross_weight_kg;
    snapshot_.overweight = snapshot_.gross_margin_kg < 0.0;
    if (fuel.available) snapshot_.fuel_flow_us_gallons_per_hour = fuel.fuel_flow_us_gallons_per_hour;
    if (!fuel.available || fuel.fuel_flow_kg_per_hour < 0.01 ||
        !progress.destination.ete_available) return;
    snapshot_.fuel_plan_available = true;
    snapshot_.trip_fuel_kg = fuel.fuel_flow_kg_per_hour * progress.destination.ete_minutes / 60.0;
    snapshot_.reserve_fuel_kg = fuel.fuel_flow_kg_per_hour * snapshot_.reserve_minutes / 60.0;
    snapshot_.fuel_margin_kg = snapshot_.loading.fuel_weight_kg - snapshot_.trip_fuel_kg -
                               snapshot_.reserve_fuel_kg;
    snapshot_.predicted_landing_weight_kg = snapshot_.loading.gross_weight_kg -
        std::min(snapshot_.loading.fuel_weight_kg, snapshot_.trip_fuel_kg);
    snapshot_.dispatch_ready = !snapshot_.overweight && snapshot_.fuel_margin_kg >= 0.0;
}

void PlanningModel::mark_unavailable() noexcept { snapshot_.available = false; }

bool PlanningModel::adjust_reserve_minutes(int delta_minutes) noexcept {
    const int adjusted = std::clamp(snapshot_.reserve_minutes + delta_minutes, 0, 180);
    if (adjusted == snapshot_.reserve_minutes) return false;
    snapshot_.reserve_minutes = adjusted;
    ++snapshot_.revision;
    return true;
}

const PlanningSnapshot& PlanningModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
