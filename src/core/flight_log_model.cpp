#include "openefb/core/flight_log_model.hpp"

#include <algorithm>
#include <cmath>

namespace openefb {
namespace {
constexpr double earth_radius_nm = 3440.065;

double distance_nm(double latitude_a, double longitude_a, double latitude_b, double longitude_b) {
    constexpr double radians = 0.017453292519943295;
    const double lat_a = latitude_a * radians;
    const double lat_b = latitude_b * radians;
    const double delta_lat = (latitude_b - latitude_a) * radians;
    const double delta_lon = (longitude_b - longitude_a) * radians;
    const double value = std::sin(delta_lat / 2.0) * std::sin(delta_lat / 2.0) +
                         std::cos(lat_a) * std::cos(lat_b) *
                         std::sin(delta_lon / 2.0) * std::sin(delta_lon / 2.0);
    return earth_radius_nm * 2.0 * std::atan2(std::sqrt(value), std::sqrt(1.0 - value));
}
}

void FlightLogModel::update_route(const FlightPlanSnapshot& flight_plan) {
    if (flight_plan.legs.empty()) return;
    snapshot_.departure = flight_plan.legs.front().identifier.empty() ? "Not set" : flight_plan.legs.front().identifier;
    snapshot_.destination = flight_plan.legs.back().identifier.empty() ? "Not set" : flight_plan.legs.back().identifier;
}

void FlightLogModel::complete_flight(const TelemetrySnapshot& telemetry) {
    if (snapshot_.airborne_seconds < 30) return;
    FlightLogEntry entry{snapshot_.aircraft_name, snapshot_.departure, snapshot_.destination,
                         snapshot_.airborne_seconds, snapshot_.distance_nm,
                         snapshot_.maximum_altitude_feet, telemetry.vertical_speed_fpm};
    snapshot_.last_landing_vertical_speed_fpm = telemetry.vertical_speed_fpm;
    snapshot_.entries.insert(snapshot_.entries.begin(), std::move(entry));
    if (snapshot_.entries.size() > 12) snapshot_.entries.pop_back();
}

void FlightLogModel::update(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                            bool on_ground, double elapsed_seconds) {
    if (!telemetry.available) return;
    snapshot_.tracking = true;
    snapshot_.aircraft_name = telemetry.aircraft_name;
    update_route(flight_plan);

    if (snapshot_.airborne && elapsed_seconds > 0.0) {
        snapshot_.airborne_seconds += static_cast<int>(std::lround(elapsed_seconds));
        snapshot_.maximum_altitude_feet = std::max(snapshot_.maximum_altitude_feet, telemetry.altitude_feet);
        if (has_previous_position_) {
            const double segment = distance_nm(previous_latitude_, previous_longitude_,
                                               telemetry.latitude_degrees, telemetry.longitude_degrees);
            if (segment < 5.0) snapshot_.distance_nm += segment;
        }
    }
    previous_latitude_ = telemetry.latitude_degrees;
    previous_longitude_ = telemetry.longitude_degrees;
    has_previous_position_ = true;

    const bool airborne_now = !on_ground && telemetry.ground_speed_knots > 25.0;
    if (!snapshot_.airborne && airborne_now) {
        snapshot_.airborne = true;
        snapshot_.airborne_seconds = 0;
        snapshot_.distance_nm = 0.0;
        snapshot_.maximum_altitude_feet = telemetry.altitude_feet;
    } else if (snapshot_.airborne && on_ground && telemetry.ground_speed_knots < 45.0) {
        complete_flight(telemetry);
        snapshot_.airborne = false;
        snapshot_.airborne_seconds = 0;
        snapshot_.distance_nm = 0.0;
        snapshot_.maximum_altitude_feet = 0.0;
    }
    snapshot_.revision = next_revision_++;
}

void FlightLogModel::reset_current_flight() noexcept {
    snapshot_.airborne = false;
    snapshot_.airborne_seconds = 0;
    snapshot_.distance_nm = 0.0;
    snapshot_.maximum_altitude_feet = 0.0;
    snapshot_.revision = next_revision_++;
}

const FlightLogSnapshot& FlightLogModel::snapshot() const noexcept { return snapshot_; }
} // namespace openefb
