#include "xplane_traffic.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace openefb::xplane {
namespace {
constexpr float sample_interval_seconds = 1.0F;
constexpr int target_count = 64;
constexpr int text_width = 8;
constexpr double meters_to_feet = 3.280839895;
constexpr double meters_per_second_to_knots = 1.943844492;
constexpr double feet_to_meters = 0.3048;
constexpr double nautical_miles_to_meters = 1852.0;
constexpr double pi = 3.14159265358979323846;

std::string packed_string(const std::array<char, target_count * text_width>& values, int index) {
    const auto start = values.data() + index * text_width;
    std::string result(start, start + text_width);
    const auto terminator = result.find('\0');
    if (terminator != std::string::npos) result.resize(terminator);
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
        result.pop_back();
    }
    return result;
}

std::pair<double, double> bearing_and_distance_nm(double latitude_1, double longitude_1,
                                                   double latitude_2, double longitude_2) {
    double longitude_delta = longitude_2 - longitude_1;
    while (longitude_delta > 180.0) longitude_delta -= 360.0;
    while (longitude_delta < -180.0) longitude_delta += 360.0;
    const double mean_latitude = (latitude_1 + latitude_2) * 0.5 * pi / 180.0;
    const double east_nm = longitude_delta * std::cos(mean_latitude) * 60.0;
    const double north_nm = (latitude_2 - latitude_1) * 60.0;
    double bearing = std::atan2(east_nm, north_nm) * 180.0 / pi;
    if (bearing < 0.0) bearing += 360.0;
    return {bearing, std::hypot(east_nm, north_nm)};
}

void pack_text(std::array<char, target_count * text_width>& values, int index,
               const std::string& text) {
    const auto length = std::min<std::size_t>(text_width - 1, text.size());
    std::memcpy(values.data() + index * text_width, text.data(), length);
}
}

XPlaneTraffic::XPlaneTraffic(TrafficModel& model, TelemetryModel& telemetry_model)
    : model_(model), telemetry_model_(telemetry_model) {}
XPlaneTraffic::~XPlaneTraffic() { stop(); }

bool XPlaneTraffic::start() {
    if (flight_loop_id_) return true;
    tcas_available_ = find_datarefs();
    injection_datarefs_available_ = override_tcas_ && relative_bearing_ &&
        relative_distance_ && relative_altitude_ && target_heading_ && mode_s_id_ &&
        ssr_mode_ && flight_id_ && aircraft_type_ && vertical_speed_;
    online_traffic_.start();
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) {
        online_traffic_.stop();
        model_.mark_unavailable();
        return false;
    }
    sample();
    sample_elapsed_seconds_ = 0.0F;
    XPLMScheduleFlightLoop(flight_loop_id_, -1.0F, 1);
    return true;
}

void XPlaneTraffic::stop() {
    if (owns_tcas_) release_tcas("Traffic adapter stopped");
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    online_traffic_.stop();
    tcas_available_ = false;
    waiting_for_tcas_ = false;
    yielded_to_provider_ = false;
    model_.mark_unavailable();
}

bool XPlaneTraffic::find_datarefs() {
    mode_s_id_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/modeS_id");
    ssr_mode_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/ssr_mode");
    flight_id_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/flight_id");
    aircraft_type_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/icao_type");
    latitude_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/lat");
    longitude_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/lon");
    elevation_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/ele");
    track_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/hpath");
    speed_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/V_msc");
    vertical_speed_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/vertical_speed");
    on_ground_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/weight_on_wheels");
    override_tcas_ = XPLMFindDataRef("sim/operation/override/override_TCAS");
    relative_bearing_ = XPLMFindDataRef("sim/cockpit2/tcas/indicators/relative_bearing_degs");
    relative_distance_ = XPLMFindDataRef("sim/cockpit2/tcas/indicators/relative_distance_mtrs");
    relative_altitude_ = XPLMFindDataRef("sim/cockpit2/tcas/indicators/relative_altitude_mtrs");
    target_heading_ = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/psi");
    return mode_s_id_ && ssr_mode_ && flight_id_ && aircraft_type_ && latitude_ &&
           longitude_ && elevation_ && track_ && speed_ && vertical_speed_ && on_ground_;
}

void XPlaneTraffic::sample() {
    const auto route_request_revision = model_.snapshot().route_request_revision;
    const auto route_request_callsign = model_.snapshot().route_request_callsign;
    if (route_request_revision != last_route_request_revision_) {
        last_route_request_revision_ = route_request_revision;
        online_traffic_.request_route(route_request_callsign);
    }

    std::array<int, target_count> mode_s{};
    std::array<int, target_count> ssr{};
    std::array<int, target_count> ground{};
    std::array<float, target_count> latitude{};
    std::array<float, target_count> longitude{};
    std::array<float, target_count> elevation{};
    std::array<float, target_count> track{};
    std::array<float, target_count> speed{};
    std::array<float, target_count> vertical_speed{};
    std::array<char, target_count * text_width> flight_ids{};
    std::array<char, target_count * text_width> aircraft_types{};
    if (tcas_available_) {
        XPLMGetDatavi(mode_s_id_, mode_s.data(), 0, target_count);
        XPLMGetDatavi(ssr_mode_, ssr.data(), 0, target_count);
        XPLMGetDatavi(on_ground_, ground.data(), 0, target_count);
        XPLMGetDatavf(latitude_, latitude.data(), 0, target_count);
        XPLMGetDatavf(longitude_, longitude.data(), 0, target_count);
        XPLMGetDatavf(elevation_, elevation.data(), 0, target_count);
        XPLMGetDatavf(track_, track.data(), 0, target_count);
        XPLMGetDatavf(speed_, speed.data(), 0, target_count);
        XPLMGetDatavf(vertical_speed_, vertical_speed.data(), 0, target_count);
        XPLMGetDatab(flight_id_, flight_ids.data(), 0, static_cast<int>(flight_ids.size()));
        XPLMGetDatab(aircraft_type_, aircraft_types.data(), 0,
                     static_cast<int>(aircraft_types.size()));
    }

    std::vector<TrafficTarget> targets;
    targets.reserve(target_count - 1);
    for (int index = 1; tcas_available_ && !owns_tcas_ && index < target_count; ++index) {
        const std::string callsign = packed_string(flight_ids, index);
        const std::string aircraft_type = packed_string(aircraft_types, index);
        const bool valid_coordinates =
            std::isfinite(latitude[index]) && std::isfinite(longitude[index]) &&
            latitude[index] >= -90.0F && latitude[index] <= 90.0F &&
            longitude[index] >= -180.0F && longitude[index] <= 180.0F;
        const bool populated = mode_s[index] > 0 || ssr[index] > 0 ||
            !callsign.empty() || !aircraft_type.empty() ||
            std::abs(elevation[index]) > 1.0F || std::abs(speed[index]) > 0.1F ||
            ground[index] != 0;
        if (!valid_coordinates || !populated) continue;
        TrafficTarget target;
        target.mode_s_id = static_cast<std::uint32_t>(std::max(0, mode_s[index]));
        target.callsign = callsign;
        target.aircraft_type = aircraft_type;
        target.latitude_degrees = latitude[index];
        target.longitude_degrees = longitude[index];
        target.altitude_feet = elevation[index] * meters_to_feet;
        target.track_degrees = track[index];
        target.ground_speed_knots = speed[index] * meters_per_second_to_knots;
        target.vertical_speed_fpm = vertical_speed[index];
        target.on_ground = ground[index] != 0;
        if (target.callsign.empty()) {
            target.callsign = target.aircraft_type.empty()
                ? "TRAFFIC " + std::to_string(index) : target.aircraft_type;
        }
        targets.push_back(std::move(target));
    }
    const std::size_t simulator_count = targets.size();
    const auto& telemetry = telemetry_model_.snapshot();
    if (telemetry.available) {
        online_traffic_.request(
            telemetry.latitude_degrees,
            telemetry.longitude_degrees,
            model_.snapshot().online_range_nm);
    }
    auto online = online_traffic_.snapshot();
    std::vector<TrafficTarget> combined;
    combined.reserve(online.targets.size() + targets.size());
    for (auto& target : online.targets) combined.push_back(std::move(target));
    const std::size_t online_count = combined.size();
    for (auto& target : targets) {
        const bool duplicate = target.mode_s_id != 0 && std::any_of(
            combined.begin(), combined.end(), [&target](const TrafficTarget& existing) {
                return existing.mode_s_id == target.mode_s_id;
            });
        if (!duplicate) combined.push_back(std::move(target));
    }
    const TrafficSource source = online.available
        ? (simulator_count > 0 ? TrafficSource::blended : TrafficSource::online)
        : TrafficSource::simulator;
    std::string status = online.status;
    if (!tcas_available_) status += " / TCAS unavailable";
    else if (simulator_count == 0) status += " / TCAS 0";
    else status += " / TCAS " + std::to_string(simulator_count);
    model_.update(std::move(combined), source, simulator_count, online_count,
                  std::move(status), online.degraded);
}

void XPlaneTraffic::publish_injection_state(bool active, std::string status) {
    const auto& current = model_.snapshot();
    if (current.injection_active == active && current.injection_status == status) return;
    model_.set_injection_state(active, std::move(status));
}

void XPlaneTraffic::try_acquire_tcas() {
    if (owns_tcas_ || waiting_for_tcas_ || yielded_to_provider_) return;
    if (!XPLMAcquirePlanes(nullptr, planes_available, this)) {
        waiting_for_tcas_ = true;
        publish_injection_state(false, "Waiting - another traffic plugin owns X-Plane TCAS");
        return;
    }
    owns_tcas_ = true;
    waiting_for_tcas_ = false;
    XPLMSetDatai(override_tcas_, 1);
    publish_injection_state(true, "Active - OpenEFB owns X-Plane TCAS");
}

void XPlaneTraffic::release_tcas(std::string status) {
    if (owns_tcas_) {
        XPLMSetActiveAircraftCount(0);
        XPLMSetDatai(override_tcas_, 0);
        XPLMReleasePlanes();
        owns_tcas_ = false;
    }
    waiting_for_tcas_ = false;
    publish_injection_state(false, std::move(status));
}

void XPlaneTraffic::on_release_requested() {
    if (!owns_tcas_) return;
    yielded_to_provider_ = true;
    release_tcas("Yielded to another traffic plugin - turn off/on to retry");
}

void XPlaneTraffic::planes_available(void* refcon) {
    auto* traffic = static_cast<XPlaneTraffic*>(refcon);
    if (!traffic) return;
    traffic->waiting_for_tcas_ = false;
    if (traffic->model_.snapshot().injection_requested && !traffic->yielded_to_provider_) {
        traffic->try_acquire_tcas();
    }
}

void XPlaneTraffic::update_injection() {
    const auto& state = model_.snapshot();
    if (!state.injection_requested) {
        yielded_to_provider_ = false;
        if (owns_tcas_) release_tcas("Disabled");
        else publish_injection_state(false, "Disabled");
        return;
    }
    if (!injection_datarefs_available_) {
        publish_injection_state(false, "Unavailable - required X-Plane TCAS datarefs are missing");
        return;
    }
    if (yielded_to_provider_) {
        publish_injection_state(false, "Yielded to another traffic plugin - turn off/on to retry");
        return;
    }
    const auto online = online_traffic_.snapshot();
    const auto& ownship = telemetry_model_.snapshot();
    if (!online.available || online.targets.empty() || !ownship.available) {
        if (owns_tcas_) release_tcas("Waiting for online traffic");
        else publish_injection_state(false, "Waiting for online traffic");
        return;
    }
    if (!owns_tcas_) try_acquire_tcas();
    if (!owns_tcas_) return;

    const int count = static_cast<int>(std::min<std::size_t>(
        target_count - 1, online.targets.size()));
    std::array<int, target_count> ids{};
    std::array<int, target_count> transponder_modes{};
    std::array<float, target_count> bearings{};
    std::array<float, target_count> distances{};
    std::array<float, target_count> altitudes{};
    std::array<float, target_count> headings{};
    std::array<float, target_count> vertical_speeds{};
    std::array<char, target_count * text_width> callsigns{};
    std::array<char, target_count * text_width> aircraft_types{};
    for (int slot = 1; slot <= count; ++slot) {
        const auto& target = online.targets[static_cast<std::size_t>(slot - 1)];
        const auto [true_bearing, distance_nm] = bearing_and_distance_nm(
            ownship.latitude_degrees, ownship.longitude_degrees,
            target.latitude_degrees, target.longitude_degrees);
        const double ownship_true_heading = ownship.true_heading_degrees != 0.0
            ? ownship.true_heading_degrees : ownship.heading_degrees;
        double relative_bearing = true_bearing - ownship_true_heading;
        while (relative_bearing > 180.0) relative_bearing -= 360.0;
        while (relative_bearing < -180.0) relative_bearing += 360.0;
        ids[slot] = static_cast<int>(target.mode_s_id != 0
            ? target.mode_s_id : 0xE00000U + static_cast<std::uint32_t>(slot));
        // Mode 7 allows X-Plane 12.4.1+ to generate native TA/RA advisories
        // when the user's aircraft is equipped for them. Ground targets use
        // the documented ground-surveillance mode.
        transponder_modes[slot] = target.on_ground ? 5 : 7;
        bearings[slot] = static_cast<float>(relative_bearing);
        distances[slot] = static_cast<float>(distance_nm * nautical_miles_to_meters);
        altitudes[slot] = static_cast<float>(
            (target.altitude_feet - (ownship.geometric_altitude_feet != 0.0
                ? ownship.geometric_altitude_feet : ownship.altitude_feet)) * feet_to_meters);
        headings[slot] = static_cast<float>(target.track_degrees);
        vertical_speeds[slot] = static_cast<float>(target.vertical_speed_fpm);
        pack_text(callsigns, slot, target.callsign);
        pack_text(aircraft_types, slot, target.aircraft_type);
    }
    XPLMSetActiveAircraftCount(count);
    XPLMSetDatavi(mode_s_id_, ids.data() + 1, 1, count);
    XPLMSetDatavi(ssr_mode_, transponder_modes.data() + 1, 1, count);
    XPLMSetDatavf(relative_bearing_, bearings.data() + 1, 1, count);
    XPLMSetDatavf(relative_distance_, distances.data() + 1, 1, count);
    XPLMSetDatavf(relative_altitude_, altitudes.data() + 1, 1, count);
    XPLMSetDatavf(target_heading_, headings.data() + 1, 1, count);
    XPLMSetDatavf(vertical_speed_, vertical_speeds.data() + 1, 1, count);
    XPLMSetDatab(flight_id_, callsigns.data() + text_width, text_width,
                 count * text_width);
    XPLMSetDatab(aircraft_type_, aircraft_types.data() + text_width, text_width,
                 count * text_width);
    publish_injection_state(true, "Active - " + std::to_string(count) +
                                  " targets available to X-Plane ATC/TCAS");
}

float XPlaneTraffic::flight_loop(float elapsed_since_last_call, float, int, void* refcon) {
    auto* traffic = static_cast<XPlaneTraffic*>(refcon);
    if (!traffic) return 0.0F;
    traffic->sample_elapsed_seconds_ += std::max(0.0F, elapsed_since_last_call);
    if (traffic->sample_elapsed_seconds_ >= sample_interval_seconds) {
        traffic->sample_elapsed_seconds_ = 0.0F;
        traffic->sample();
    }
    traffic->update_injection();
    return -1.0F;
}

} // namespace openefb::xplane
