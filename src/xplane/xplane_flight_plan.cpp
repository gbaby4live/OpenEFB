#include "xplane_flight_plan.hpp"
#include "openefb/core/flight_plan_file.hpp"

#include <XPLMNavigation.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 1.0F;
constexpr int maximum_fms_entries = 100;
constexpr XPLMNavType editable_waypoint_types =
    xplm_Nav_Airport | xplm_Nav_NDB | xplm_Nav_VOR | xplm_Nav_Fix;

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

WaypointKind waypoint_kind(XPLMNavType type) {
    if (type & xplm_Nav_Airport) {
        return WaypointKind::airport;
    }
    if (type & xplm_Nav_VOR) {
        return WaypointKind::vor;
    }
    if (type & xplm_Nav_NDB) {
        return WaypointKind::ndb;
    }
    if (type & xplm_Nav_Fix) {
        return WaypointKind::fix;
    }
    if (type & xplm_Nav_LatLon) {
        return WaypointKind::coordinate;
    }
    return WaypointKind::other;
}

} // namespace

XPlaneFlightPlan::XPlaneFlightPlan(FlightPlanModel& model) : model_(model) {}

XPlaneFlightPlan::~XPlaneFlightPlan() { stop(); }

bool XPlaneFlightPlan::start() {
    if (flight_loop_id_) {
        return true;
    }

    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) {
        model_.mark_unavailable();
        return false;
    }

    sample();
    XPLMScheduleFlightLoop(flight_loop_id_, sample_interval_seconds, 1);
    return true;
}

void XPlaneFlightPlan::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    model_.mark_unavailable();
}

void XPlaneFlightPlan::refresh() { sample(); }

std::optional<FlightPlanLeg> XPlaneFlightPlan::find_waypoint(
    std::string identifier, double near_latitude, double near_longitude) const {
    identifier = uppercase(std::move(identifier));
    if (identifier.empty() || !std::isfinite(near_latitude) || !std::isfinite(near_longitude)) {
        return std::nullopt;
    }
    float latitude = static_cast<float>(near_latitude);
    float longitude = static_cast<float>(near_longitude);
    const XPLMNavRef reference = XPLMFindNavAid(nullptr, identifier.c_str(), &latitude, &longitude,
                                                nullptr, editable_waypoint_types);
    if (reference == XPLM_NAV_NOT_FOUND) {
        return std::nullopt;
    }
    XPLMNavType type{xplm_Nav_Unknown};
    std::array<char, 256> found_identifier{};
    float found_latitude{};
    float found_longitude{};
    XPLMGetNavAidInfo(reference, &type, &found_latitude, &found_longitude, nullptr, nullptr,
                      nullptr, found_identifier.data(), nullptr, nullptr);
    if (uppercase(found_identifier.data()) != identifier) {
        return std::nullopt;
    }
    FlightPlanLeg leg;
    leg.identifier = identifier;
    leg.kind = waypoint_kind(type);
    leg.latitude_degrees = found_latitude;
    leg.longitude_degrees = found_longitude;
    return leg;
}

FlightPlanEditResult XPlaneFlightPlan::apply_route(const std::vector<FlightPlanLeg>& legs) {
    if (legs.size() > maximum_fms_entries) {
        return {false, "Route exceeds X-Plane's 100-waypoint limit"};
    }
    struct ResolvedEntry {
        bool coordinate{false};
        XPLMNavRef reference{XPLM_NAV_NOT_FOUND};
        float latitude{};
        float longitude{};
        int altitude{};
    };
    std::vector<ResolvedEntry> resolved;
    resolved.reserve(legs.size());
    for (const auto& leg : legs) {
        ResolvedEntry entry;
        entry.altitude = leg.altitude_feet;
        if (leg.kind == WaypointKind::coordinate) {
            entry.coordinate = true;
            entry.latitude = static_cast<float>(leg.latitude_degrees);
            entry.longitude = static_cast<float>(leg.longitude_degrees);
        } else {
            float latitude = static_cast<float>(leg.latitude_degrees);
            float longitude = static_cast<float>(leg.longitude_degrees);
            entry.reference = XPLMFindNavAid(nullptr, leg.identifier.c_str(), &latitude, &longitude,
                                             nullptr, editable_waypoint_types);
            if (entry.reference == XPLM_NAV_NOT_FOUND) {
                return {false, "Waypoint no longer exists: " + leg.identifier};
            }
            std::array<char, 256> found_identifier{};
            XPLMGetNavAidInfo(entry.reference, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                              found_identifier.data(), nullptr, nullptr);
            if (uppercase(found_identifier.data()) != uppercase(leg.identifier)) {
                return {false, "Waypoint could not be resolved exactly: " + leg.identifier};
            }
        }
        resolved.push_back(entry);
    }

    const int previous_count = std::clamp(XPLMCountFMSEntries(), 0, maximum_fms_entries);
    const int previous_destination = XPLMGetDestinationFMSEntry();
    const int previous_displayed = XPLMGetDisplayedFMSEntry();
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const auto& entry = resolved[index];
        if (entry.coordinate) {
            XPLMSetFMSEntryLatLon(static_cast<int>(index), entry.latitude, entry.longitude,
                                  entry.altitude);
        } else {
            XPLMSetFMSEntryInfo(static_cast<int>(index), entry.reference, entry.altitude);
        }
    }
    for (int index = previous_count - 1; index >= static_cast<int>(resolved.size()); --index) {
        XPLMClearFMSEntry(index);
    }
    if (!resolved.empty()) {
        const int last_index = static_cast<int>(resolved.size()) - 1;
        XPLMSetDisplayedFMSEntry(std::clamp(previous_displayed, 0, last_index));
        XPLMSetDestinationFMSEntry(std::clamp(previous_destination, 0, last_index));
    }
    sample();
    return {true, "Route applied to X-Plane FMS"};
}

FlightPlanEditResult XPlaneFlightPlan::insert_after_active(FlightPlanLeg leg,
                                                           std::string display_name) {
    auto route = model_.snapshot();
    std::vector<FlightPlanLeg> legs = route.available ? route.legs : std::vector<FlightPlanLeg>{};
    const int active = std::clamp(route.active_leg_index, -1,
                                  static_cast<int>(legs.size()) - 1);
    // Keep the arrival as the final FMS entry. Map points always become
    // enroute legs, even when X-Plane currently considers the arrival active.
    const std::size_t insertion = legs.size() >= 2
        ? std::min(static_cast<std::size_t>(active + 1), legs.size() - 1)
        : static_cast<std::size_t>(active + 1);
    legs.insert(legs.begin() + static_cast<std::ptrdiff_t>(insertion), std::move(leg));
    auto result = apply_route(legs);
    if (!result.success) return result;
    XPLMSetDisplayedFMSEntry(static_cast<int>(insertion));
    XPLMSetDestinationFMSEntry(static_cast<int>(insertion));
    sample();
    result.message = "Added " + std::move(display_name) + " to the active X-Plane FMS route";
    return result;
}

FlightPlanEditResult XPlaneFlightPlan::remove_route_leg(std::size_t index,
                                                         std::string display_name) {
    const auto route = model_.snapshot();
    if (!route.available || index >= route.legs.size()) {
        return {false, "The selected waypoint is no longer in the active route"};
    }
    if (index == 0 || index + 1 == route.legs.size()) {
        return {false, "Departure and destination must be changed in Flight Plan"};
    }

    auto legs = route.legs;
    const int previous_active = std::clamp(route.active_leg_index, 0,
                                           static_cast<int>(legs.size()) - 1);
    legs.erase(legs.begin() + static_cast<std::ptrdiff_t>(index));
    auto result = apply_route(legs);
    if (!result.success) return result;

    // If the active leg was removed, the entry which followed it now occupies
    // the same index. X-Plane therefore continues toward the next waypoint.
    const int next_active = previous_active > static_cast<int>(index)
        ? previous_active - 1 : std::min(previous_active, static_cast<int>(legs.size()) - 1);
    if (!legs.empty()) {
        XPLMSetDisplayedFMSEntry(next_active);
        XPLMSetDestinationFMSEntry(next_active);
    }
    sample();
    result.message = "Removed " + std::move(display_name) +
                     "; route continues to the next waypoint";
    return result;
}

FlightPlanEditResult XPlaneFlightPlan::import_latest(const std::filesystem::path& directory) {
    try {
        std::filesystem::path newest;
        std::filesystem::file_time_type newest_time{};
        if (!std::filesystem::is_directory(directory)) {
            return {false, "X-Plane FMS plans folder was not found"};
        }
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (!entry.is_regular_file() || uppercase(entry.path().extension().string()) != ".FMS") continue;
            const auto time = entry.last_write_time();
            if (newest.empty() || time > newest_time) {
                newest = entry.path();
                newest_time = time;
            }
        }
        if (newest.empty()) return {false, "No .fms plan was found in Output/FMS plans"};
        std::ifstream input(newest);
        auto parsed = parse_xplane_fms(input);
        if (!parsed.success) return {false, parsed.message};
        auto applied = apply_route(parsed.legs);
        if (applied.success) applied.message = "Imported " + newest.filename().string() + " to X-Plane FMS";
        return applied;
    } catch (...) {
        return {false, "The latest FMS plan could not be imported"};
    }
}

FlightPlanEditResult XPlaneFlightPlan::export_current(const std::filesystem::path& directory) const {
    try {
        const auto& route = model_.snapshot();
        if (!route.available || route.legs.empty()) return {false, "There is no route to export"};
        std::filesystem::create_directories(directory);
        const auto filename = xplane_fms_filename(route.legs);
        const auto path = directory / filename;
        std::ofstream output(path, std::ios::trunc);
        if (!write_xplane_fms(output, route.legs)) return {false, "The route could not be exported"};
        return {true, "Saved Output/FMS plans/" + filename};
    } catch (...) {
        return {false, "The route could not be exported"};
    }
}

void XPlaneFlightPlan::sample() {
    FlightPlanSnapshot snapshot;
    const int entry_count = std::clamp(XPLMCountFMSEntries(), 0, maximum_fms_entries);
    snapshot.active_leg_index = XPLMGetDestinationFMSEntry();
    snapshot.legs.reserve(static_cast<std::size_t>(entry_count));

    for (int index = 0; index < entry_count; ++index) {
        XPLMNavType type{xplm_Nav_Unknown};
        std::array<char, 256> identifier{};
        XPLMNavRef nav_reference{XPLM_NAV_NOT_FOUND};
        int altitude{};
        float latitude{};
        float longitude{};
        XPLMGetFMSEntryInfo(index, &type, identifier.data(), &nav_reference,
                            &altitude, &latitude, &longitude);

        FlightPlanLeg leg;
        leg.index = index;
        leg.identifier = identifier.data();
        leg.kind = waypoint_kind(type);
        leg.altitude_feet = altitude;
        leg.latitude_degrees = latitude;
        leg.longitude_degrees = longitude;
        snapshot.legs.push_back(std::move(leg));
    }
    model_.update(std::move(snapshot));
}

float XPlaneFlightPlan::flight_loop(float, float, int, void* refcon) {
    auto* flight_plan = static_cast<XPlaneFlightPlan*>(refcon);
    if (!flight_plan) {
        return 0.0F;
    }
    flight_plan->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
