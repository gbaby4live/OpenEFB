#include "openefb/core/traffic_model.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace openefb {
namespace {
bool valid_target(const TrafficTarget& target) {
    return std::isfinite(target.latitude_degrees) &&
           std::isfinite(target.longitude_degrees) &&
           std::isfinite(target.altitude_feet) &&
           target.latitude_degrees >= -90.0 && target.latitude_degrees <= 90.0 &&
           target.longitude_degrees >= -180.0 && target.longitude_degrees <= 180.0;
}
}

void TrafficModel::update(std::vector<TrafficTarget> targets, TrafficSource source,
                          std::size_t simulator_target_count,
                          std::size_t online_target_count, std::string status,
                          bool online_degraded) {
    std::erase_if(targets, [](const TrafficTarget& target) { return !valid_target(target); });
    if (targets.size() > 63) targets.resize(63);
    snapshot_.available = true;
    snapshot_.targets = std::move(targets);
    snapshot_.source = source;
    snapshot_.simulator_target_count = simulator_target_count;
    snapshot_.online_target_count = online_target_count;
    snapshot_.status = std::move(status);
    snapshot_.online_degraded = online_degraded;
    snapshot_.revision = next_revision_++;
}

void TrafficModel::mark_unavailable() noexcept {
    snapshot_.available = false;
    snapshot_.targets.clear();
    snapshot_.source = TrafficSource::unavailable;
    snapshot_.simulator_target_count = 0;
    snapshot_.online_target_count = 0;
    snapshot_.status.clear();
    snapshot_.online_degraded = false;
    snapshot_.injection_active = false;
    snapshot_.injection_status = snapshot_.injection_requested ? "Waiting for traffic adapter"
                                                               : "Disabled";
    snapshot_.visual_traffic_active = false;
    snapshot_.visual_traffic_status = snapshot_.visual_traffic_requested
        ? "Waiting for traffic injection" : "Disabled";
    snapshot_.revision = next_revision_++;
}

void TrafficModel::set_injection_requested(bool requested) {
    snapshot_.injection_requested = requested;
    if (!requested) {
        snapshot_.injection_active = false;
        snapshot_.injection_status = "Disabled";
    } else if (!snapshot_.injection_active) {
        snapshot_.injection_status = "Waiting for X-Plane traffic access";
    }
    snapshot_.revision = next_revision_++;
}

void TrafficModel::set_injection_state(bool active, std::string status) {
    snapshot_.injection_active = active;
    snapshot_.injection_status = std::move(status);
    snapshot_.revision = next_revision_++;
}

void TrafficModel::set_visual_traffic_requested(bool requested) {
    if (snapshot_.visual_traffic_requested == requested) return;
    snapshot_.visual_traffic_requested = requested;
    if (!requested) {
        snapshot_.visual_traffic_active = false;
        snapshot_.visual_traffic_status = "Disabled";
    } else if (!snapshot_.visual_traffic_active) {
        snapshot_.visual_traffic_status = "Waiting for traffic injection";
    }
    snapshot_.revision = next_revision_++;
}

void TrafficModel::set_visual_traffic_state(bool active, std::string status) {
    if (snapshot_.visual_traffic_active == active &&
        snapshot_.visual_traffic_status == status) return;
    snapshot_.visual_traffic_active = active;
    snapshot_.visual_traffic_status = std::move(status);
    snapshot_.revision = next_revision_++;
}

void TrafficModel::request_route_lookup(std::string callsign) {
    if (callsign.empty()) return;
    snapshot_.route_request_callsign = std::move(callsign);
    ++snapshot_.route_request_revision;
    snapshot_.revision = next_revision_++;
}

void TrafficModel::set_online_range_nm(int range_nm) noexcept {
    const int clamped = std::clamp(range_nm, 25, 200);
    if (snapshot_.online_range_nm == clamped) return;
    snapshot_.online_range_nm = clamped;
    snapshot_.revision = next_revision_++;
}

const TrafficSnapshot& TrafficModel::snapshot() const noexcept { return snapshot_; }
} // namespace openefb
