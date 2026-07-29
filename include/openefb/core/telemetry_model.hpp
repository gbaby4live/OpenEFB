#pragma once

#include <cstdint>
#include <string>

namespace openefb {

struct TelemetrySnapshot {
    bool available{false};
    std::string aircraft_name{"Unknown aircraft"};
    double latitude_degrees{};
    double longitude_degrees{};
    double altitude_feet{};
    double geometric_altitude_feet{};
    double ground_speed_knots{};
    double heading_degrees{};
    double true_heading_degrees{};
    double vertical_speed_fpm{};
    std::uint64_t revision{};
};

class TelemetryModel final {
public:
    void update(TelemetrySnapshot snapshot);
    void mark_unavailable() noexcept;

    [[nodiscard]] const TelemetrySnapshot& snapshot() const noexcept;

private:
    TelemetrySnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
