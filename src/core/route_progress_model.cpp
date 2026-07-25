#include "openefb/core/route_progress_model.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openefb {

namespace {

constexpr double earth_radius_nm = 3440.065;
constexpr double pi = 3.14159265358979323846;

double radians(double degrees) { return degrees * pi / 180.0; }

bool valid_coordinates(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude) &&
           latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

RouteProgressPoint progress_to(const TelemetrySnapshot& telemetry, const FlightPlanLeg& leg) {
    RouteProgressPoint progress;
    if (!valid_coordinates(telemetry.latitude_degrees, telemetry.longitude_degrees) ||
        !valid_coordinates(leg.latitude_degrees, leg.longitude_degrees)) {
        return progress;
    }

    const double latitude_1 = radians(telemetry.latitude_degrees);
    const double latitude_2 = radians(leg.latitude_degrees);
    const double longitude_delta = radians(leg.longitude_degrees - telemetry.longitude_degrees);
    const double latitude_delta = latitude_2 - latitude_1;
    const double sin_latitude = std::sin(latitude_delta / 2.0);
    const double sin_longitude = std::sin(longitude_delta / 2.0);
    const double haversine = std::clamp(sin_latitude * sin_latitude +
                                            std::cos(latitude_1) * std::cos(latitude_2) *
                                                sin_longitude * sin_longitude,
                                        0.0, 1.0);

    progress.available = true;
    progress.identifier = leg.identifier;
    progress.distance_nm = earth_radius_nm * 2.0 * std::asin(std::sqrt(haversine));
    const double bearing = std::atan2(std::sin(longitude_delta) * std::cos(latitude_2),
                                      std::cos(latitude_1) * std::sin(latitude_2) -
                                          std::sin(latitude_1) * std::cos(latitude_2) *
                                              std::cos(longitude_delta));
    progress.bearing_degrees = std::fmod(bearing * 180.0 / pi + 360.0, 360.0);
    if (std::isfinite(telemetry.ground_speed_knots) && telemetry.ground_speed_knots >= 1.0) {
        progress.ete_available = true;
        progress.ete_minutes = progress.distance_nm / telemetry.ground_speed_knots * 60.0;
    }
    return progress;
}

} // namespace

void RouteProgressModel::update(const TelemetrySnapshot& telemetry,
                                const FlightPlanSnapshot& flight_plan) {
    RouteProgressSnapshot snapshot;
    snapshot.telemetry_revision = telemetry.revision;
    snapshot.route_revision = flight_plan.revision;
    snapshot.available = telemetry.available && flight_plan.available && !flight_plan.legs.empty();
    if (snapshot.available) {
        const auto active = std::find_if(flight_plan.legs.begin(), flight_plan.legs.end(),
                                         [](const FlightPlanLeg& leg) { return leg.active; });
        if (active != flight_plan.legs.end()) {
            snapshot.active_waypoint = progress_to(telemetry, *active);
        }
        const auto destination = std::find_if(flight_plan.legs.rbegin(), flight_plan.legs.rend(),
                                              [](const FlightPlanLeg& leg) {
                                                  return leg.kind == WaypointKind::airport;
                                              });
        snapshot.destination = progress_to(
            telemetry, destination != flight_plan.legs.rend() ? *destination : flight_plan.legs.back());
    }
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

void RouteProgressModel::mark_unavailable() noexcept { snapshot_.available = false; }

const RouteProgressSnapshot& RouteProgressModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
