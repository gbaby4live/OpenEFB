#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace openefb {

enum class WaypointKind {
    airport,
    vor,
    ndb,
    fix,
    coordinate,
    other,
};

struct FlightPlanLeg {
    int index{};
    std::string identifier;
    WaypointKind kind{WaypointKind::other};
    int altitude_feet{};
    double latitude_degrees{};
    double longitude_degrees{};
    bool active{false};
};

struct FlightPlanSnapshot {
    bool available{false};
    std::vector<FlightPlanLeg> legs;
    int active_leg_index{-1};
    std::uint64_t revision{};
};

class FlightPlanModel final {
public:
    void update(FlightPlanSnapshot snapshot);
    void mark_unavailable() noexcept;

    [[nodiscard]] const FlightPlanSnapshot& snapshot() const noexcept;

private:
    FlightPlanSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
