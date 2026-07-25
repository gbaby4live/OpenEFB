#pragma once

#include "openefb/core/fuel_model.hpp"
#include "openefb/core/route_progress_model.hpp"

#include <cstdint>

namespace openefb {

struct AircraftLoading {
    double empty_weight_kg{};
    double payload_weight_kg{};
    double fuel_weight_kg{};
    double gross_weight_kg{};
    double maximum_gross_weight_kg{};
    double fuel_capacity_kg{};
    double cg_offset_meters{};
};

struct PlanningSnapshot {
    bool available{false};
    AircraftLoading loading;
    int reserve_minutes{45};
    double gross_margin_kg{};
    double loading_percent{};
    bool overweight{false};
    bool fuel_plan_available{false};
    double fuel_flow_us_gallons_per_hour{};
    double trip_fuel_kg{};
    double reserve_fuel_kg{};
    double fuel_margin_kg{};
    double predicted_landing_weight_kg{};
    bool dispatch_ready{false};
    std::uint64_t revision{};
};

class PlanningModel final {
public:
    void update(AircraftLoading loading, const FuelSnapshot& fuel,
                const RouteProgressSnapshot& progress);
    void mark_unavailable() noexcept;
    bool adjust_reserve_minutes(int delta_minutes) noexcept;
    [[nodiscard]] const PlanningSnapshot& snapshot() const noexcept;

private:
    void calculate(const FuelSnapshot& fuel, const RouteProgressSnapshot& progress);
    PlanningSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
