#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/telemetry_model.hpp"

#include <cstdint>
#include <string>

namespace openefb {

struct RouteProgressPoint {
    bool available{false};
    std::string identifier;
    double distance_nm{};
    double bearing_degrees{};
    bool ete_available{false};
    double ete_minutes{};
};

struct RouteProgressSnapshot {
    bool available{false};
    RouteProgressPoint active_waypoint;
    RouteProgressPoint destination;
    std::uint64_t telemetry_revision{};
    std::uint64_t route_revision{};
    std::uint64_t revision{};
};

class RouteProgressModel final {
public:
    void update(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan);
    void mark_unavailable() noexcept;

    [[nodiscard]] const RouteProgressSnapshot& snapshot() const noexcept;

private:
    RouteProgressSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
