#pragma once

#include <cstdint>

namespace openefb {

struct FuelSnapshot {
    bool available{false};
    double fuel_remaining_kg{};
    double fuel_flow_kg_per_hour{};
    double fuel_flow_us_gallons_per_hour{};
    bool endurance_available{false};
    double endurance_hours{};
    bool range_available{false};
    double estimated_range_nm{};
    std::uint64_t telemetry_revision{};
    std::uint64_t revision{};
};

class FuelModel final {
public:
    void update(double fuel_remaining_kg, double fuel_flow_kg_per_hour,
                double ground_speed_knots, std::uint64_t telemetry_revision);
    void mark_unavailable() noexcept;

    [[nodiscard]] const FuelSnapshot& snapshot() const noexcept;

private:
    FuelSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
