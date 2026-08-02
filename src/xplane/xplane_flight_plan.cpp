#include "xplane_flight_plan.hpp"
#include "openefb/core/flight_plan_file.hpp"

#include <XPLMNavigation.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <sstream>
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

std::vector<std::string> route_signature(const std::vector<FlightPlanLeg>& legs) {
    std::vector<std::string> signature;
    signature.reserve(legs.size());
    for (const auto& leg : legs) signature.push_back(uppercase(leg.identifier));
    return signature;
}

std::string current_airac_cycle() {
    std::array<char, 1024> system_path{};
    XPLMGetSystemPath(system_path.data());
    const std::filesystem::path root(system_path.data());
    const std::array candidates{
        root / "Custom Data" / "cycle_info.txt",
        root / "Custom Data" / "earth_nav.dat",
        root / "Resources" / "default data" / "cycle_info.txt",
        root / "Resources" / "default data" / "earth_nav.dat"};
    for (const auto& path : candidates) {
        std::ifstream input(path);
        std::string line;
        for (int count = 0; input && count < 8 && std::getline(input, line); ++count) {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            const auto marker = lower.find("cycle");
            if (marker == std::string::npos) continue;
            for (std::size_t index = marker + 5; index + 4 <= line.size(); ++index) {
                const auto value = line.substr(index, 4);
                if (std::all_of(value.begin(), value.end(), [](unsigned char character) {
                        return std::isdigit(character) != 0;
                    })) return value;
            }
        }
    }
    return "0000";
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

#if defined(XPLM410)
    // A manually edited builder route is authoritative. Clear any separately
    // loaded native approach so removed fixes cannot remain active or appear
    // twice beside the newly written primary sequence.
    for (int index = XPLMCountFMSFlightPlanEntries(xplm_Fpl_Pilot_Approach) - 1;
         index >= 0; --index)
        XPLMClearFMSFlightPlanEntry(xplm_Fpl_Pilot_Approach, index);
#endif

    const int previous_count = std::clamp(XPLMCountFMSEntries(), 0, maximum_fms_entries);
    const int previous_destination = XPLMGetDestinationFMSEntry();
    const int previous_displayed = XPLMGetDisplayedFMSEntry();
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const auto& entry = resolved[index];
        if (entry.coordinate) {
#if defined(XPLM410)
            const auto& identifier = legs[index].identifier;
            XPLMSetFMSFlightPlanEntryLatLonWithId(
                xplm_Fpl_Pilot_Primary, static_cast<int>(index),
                entry.latitude, entry.longitude, entry.altitude,
                identifier.c_str(), static_cast<unsigned int>(identifier.size()));
#else
            XPLMSetFMSEntryLatLon(static_cast<int>(index), entry.latitude, entry.longitude,
                                  entry.altitude);
#endif
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

FlightPlanEditResult XPlaneFlightPlan::apply_approach(
    const ApproachProcedure& procedure, std::string transition_identifier,
    std::string airport_identifier, bool destination_endpoint,
    const std::vector<std::string>& excluded_fixes) {
    airport_identifier = uppercase(std::move(airport_identifier));
    transition_identifier = uppercase(std::move(transition_identifier));
    const auto current = model_.snapshot();
    if (!current.available || current.legs.empty())
        return {false, "Load a route with departure and destination airports first"};

    auto route = current.legs;
    const std::size_t endpoint_slot = destination_endpoint ? 1U : 0U;
    auto& previous = applied_approaches_[endpoint_slot];
    if (previous && previous->airport_identifier == airport_identifier &&
        previous->route_signature == route_signature(route) &&
        previous->insertion_start + previous->insertion_count <= route.size()) {
        route.erase(route.begin() + static_cast<std::ptrdiff_t>(previous->insertion_start),
                    route.begin() + static_cast<std::ptrdiff_t>(
                        previous->insertion_start + previous->insertion_count));
    }
    previous.reset();

    auto airport_position = route.end();
    if (destination_endpoint) {
        airport_position = std::find_if(route.rbegin(), route.rend(), [&](const auto& leg) {
            return leg.kind == WaypointKind::airport &&
                   uppercase(leg.identifier) == airport_identifier;
        }).base();
        if (airport_position != route.begin()) --airport_position;
        else airport_position = route.end();
    } else {
        airport_position = std::find_if(route.begin(), route.end(), [&](const auto& leg) {
            return leg.kind == WaypointKind::airport &&
                   uppercase(leg.identifier) == airport_identifier;
        });
    }
    if (airport_position == route.end())
        return {false, airport_identifier + " is no longer an endpoint in the active route"};
    const std::size_t airport_index = static_cast<std::size_t>(
        std::distance(route.begin(), airport_position));

#if defined(XPLM410)
    // X-Plane's procedure-aware loader preserves RF/IF/CF legs and altitude
    // constraints that cannot be represented by the legacy waypoint setters.
    // This is the path required for the stock avionics to calculate VPATH.
    if (excluded_fixes.empty() && destination_endpoint && airport_index + 1 == route.size() &&
        !procedure.runway.empty()) {
        std::ostringstream native_plan;
        const FlightPlanApproachSelection selection{
            airport_identifier, procedure.runway, procedure.identifier,
            transition_identifier, current_airac_cycle()};
        if (write_xplane_fms_with_approach(native_plan, route, selection)) {
            const auto buffer = native_plan.str();
            XPLMLoadFMSFlightPlan(0, buffer.data(),
                                  static_cast<unsigned int>(buffer.size()));
            sample();
            FlightPlanEditResult result{true, "Loaded " + procedure.display_name};
            if (!transition_identifier.empty()) result.message += " via " + transition_identifier;
            result.message += " as a native X-Plane approach with VPATH constraints";
            return result;
        }
    }
#endif

#if defined(XPLM410)
    if (!excluded_fixes.empty()) {
        for (int index = XPLMCountFMSFlightPlanEntries(xplm_Fpl_Pilot_Approach) - 1;
             index >= 0; --index)
            XPLMClearFMSFlightPlanEntry(xplm_Fpl_Pilot_Approach, index);
    }
#endif

    std::vector<ApproachLeg> selected_legs;
    if (!transition_identifier.empty()) {
        const auto transition = std::find_if(
            procedure.transitions.begin(), procedure.transitions.end(), [&](const auto& value) {
                return uppercase(value.identifier) == transition_identifier;
            });
        if (transition == procedure.transitions.end())
            return {false, "The selected approach transition is no longer available"};
        selected_legs.insert(selected_legs.end(), transition->legs.begin(), transition->legs.end());
    }
    selected_legs.insert(selected_legs.end(), procedure.final_legs.begin(),
                         procedure.final_legs.end());
    if (selected_legs.empty()) return {false, "The selected approach has no navigable legs"};

    double near_latitude = airport_position->latitude_degrees;
    double near_longitude = airport_position->longitude_degrees;
    std::vector<FlightPlanLeg> inserted;
    for (const auto& procedure_leg : selected_legs) {
        const std::string leg_key = procedure_leg.identifier + "#" +
                                    std::to_string(procedure_leg.sequence);
        if (procedure_leg.identifier.empty() ||
            std::find(excluded_fixes.begin(), excluded_fixes.end(),
                      leg_key) != excluded_fixes.end()) continue;
        FlightPlanLeg leg;
        if (procedure_leg.runway && std::isfinite(procedure_leg.latitude_degrees) &&
            std::isfinite(procedure_leg.longitude_degrees) &&
            (procedure_leg.latitude_degrees != 0.0 || procedure_leg.longitude_degrees != 0.0)) {
            leg.identifier = procedure_leg.identifier;
            leg.kind = WaypointKind::coordinate;
            leg.latitude_degrees = procedure_leg.latitude_degrees;
            leg.longitude_degrees = procedure_leg.longitude_degrees;
        } else {
            auto resolved = find_waypoint(procedure_leg.identifier, near_latitude, near_longitude);
            if (!resolved) {
                return {false, "Approach fix could not be resolved in X-Plane: " +
                                   procedure_leg.identifier};
            }
            leg = std::move(*resolved);
        }
        leg.altitude_feet = procedure_leg.altitude_feet;
        near_latitude = leg.latitude_degrees;
        near_longitude = leg.longitude_degrees;
        inserted.push_back(std::move(leg));
    }
    if (inserted.empty()) return {false, "The selected approach has no resolvable FMS fixes"};
    if (route.size() + inserted.size() > maximum_fms_entries)
        return {false, "Approach would exceed X-Plane's 100-waypoint limit"};

    const std::size_t insertion_start = airport_index;
    route.insert(route.begin() + static_cast<std::ptrdiff_t>(insertion_start),
                 inserted.begin(), inserted.end());
    int next_active = current.active_leg_index;
    if (!destination_endpoint) {
        next_active = static_cast<int>(insertion_start);
    } else if (next_active >= static_cast<int>(airport_index)) {
        next_active = static_cast<int>(insertion_start);
    }

    auto result = apply_route(route);
    if (!result.success) return result;
    next_active = std::clamp(next_active, 0, static_cast<int>(route.size()) - 1);
    XPLMSetDisplayedFMSEntry(next_active);
    XPLMSetDestinationFMSEntry(next_active);
#if defined(XPLM410)
    XPLMSetDirectToFMSFlightPlanEntry(xplm_Fpl_Pilot_Primary, next_active);
#endif
    sample();
    previous = AppliedApproach{airport_identifier, route_signature(model_.snapshot().legs),
                               insertion_start, inserted.size()};
    result.message = "Loaded " + procedure.display_name;
    if (!transition_identifier.empty()) result.message += " via " + transition_identifier;
    result.message += " into X-Plane FMS (" + std::to_string(inserted.size()) + " legs)";
    if (!excluded_fixes.empty()) result.message += " as a custom fix sequence";
    return result;
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
        if (!write_xplane_fms(output, route.legs, current_airac_cycle()))
            return {false, "The route could not be exported"};
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
#if defined(XPLM410)
    const int approach_count = std::clamp(
        XPLMCountFMSFlightPlanEntries(xplm_Fpl_Pilot_Approach), 0, maximum_fms_entries);
    snapshot.active_approach_leg_index =
        XPLMGetDestinationFMSFlightPlanEntry(xplm_Fpl_Pilot_Approach);
    snapshot.approach_legs.reserve(static_cast<std::size_t>(approach_count));
    for (int index = 0; index < approach_count; ++index) {
        XPLMNavType type{xplm_Nav_Unknown};
        std::array<char, 256> identifier{};
        XPLMNavRef nav_reference{XPLM_NAV_NOT_FOUND};
        int altitude{};
        float latitude{};
        float longitude{};
        XPLMGetFMSFlightPlanEntryInfo(
            xplm_Fpl_Pilot_Approach, index, &type, identifier.data(), &nav_reference,
            &altitude, &latitude, &longitude);
        FlightPlanLeg leg;
        leg.index = index;
        leg.identifier = identifier.data();
        leg.kind = waypoint_kind(type);
        leg.altitude_feet = altitude;
        leg.latitude_degrees = latitude;
        leg.longitude_degrees = longitude;
        snapshot.approach_legs.push_back(std::move(leg));
    }
#endif
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
