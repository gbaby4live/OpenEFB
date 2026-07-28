#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/telemetry_model.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openefb {

struct FlightTrackPoint {
    double latitude_degrees{};
    double longitude_degrees{};
    double altitude_feet{};
};

struct FlightLogEntry {
    std::string completed_utc;
    std::string aircraft_name;
    std::string departure;
    std::string destination;
    int airborne_seconds{};
    double distance_nm{};
    double maximum_altitude_feet{};
    double landing_vertical_speed_fpm{};
    std::vector<FlightTrackPoint> track;
};

struct FlightLogSnapshot {
    bool tracking{false};
    bool airborne{false};
    std::string aircraft_name{"Unknown aircraft"};
    std::string departure{"Not set"};
    std::string destination{"Not set"};
    int airborne_seconds{};
    double distance_nm{};
    double maximum_altitude_feet{};
    double last_landing_vertical_speed_fpm{};
    std::vector<FlightLogEntry> entries;
    std::uint64_t revision{};
};

class FlightLogModel final {
public:
    void update(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                bool on_ground, double elapsed_seconds,
                std::string_view completed_utc = {});
    void replace_entries(std::vector<FlightLogEntry> entries);
    void reset_current_flight() noexcept;
    [[nodiscard]] const FlightLogSnapshot& snapshot() const noexcept;

private:
    void update_route(const FlightPlanSnapshot& flight_plan);
    void complete_flight(const TelemetrySnapshot& telemetry, std::string_view completed_utc);

    FlightLogSnapshot snapshot_;
    double previous_latitude_{};
    double previous_longitude_{};
    bool has_previous_position_{false};
    std::vector<FlightTrackPoint> current_track_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
