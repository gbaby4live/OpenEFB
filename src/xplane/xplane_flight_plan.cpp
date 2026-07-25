#include "xplane_flight_plan.hpp"

#include <XPLMNavigation.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 1.0F;
constexpr int maximum_fms_entries = 100;

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
