#include "xplane_telemetry.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 0.2F;
constexpr double meters_to_feet = 3.280839895;
constexpr double meters_per_second_to_knots = 1.943844492;
constexpr double meters_per_second_to_feet_per_minute = 196.8503937;

} // namespace

XPlaneTelemetry::XPlaneTelemetry(TelemetryModel& model) : model_(model) {}

XPlaneTelemetry::~XPlaneTelemetry() { stop(); }

bool XPlaneTelemetry::start() {
    if (flight_loop_id_) {
        return true;
    }
    if (!find_datarefs()) {
        model_.mark_unavailable();
        return false;
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

    refresh_aircraft_identity();
    sample();
    XPLMScheduleFlightLoop(flight_loop_id_, sample_interval_seconds, 1);
    return true;
}

void XPlaneTelemetry::refresh_aircraft_identity() {
    if (!aircraft_name_) {
        aircraft_name_value_ = "Unknown aircraft";
        return;
    }

    std::array<char, 256> value{};
    const int length = XPLMGetDatab(aircraft_name_, value.data(), 0, static_cast<int>(value.size() - 1));
    if (length <= 0) {
        aircraft_name_value_ = "Unknown aircraft";
        return;
    }
    value[std::min(static_cast<std::size_t>(length), value.size() - 1)] = '\0';
    aircraft_name_value_ = value.data();
}

void XPlaneTelemetry::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    model_.mark_unavailable();
}

bool XPlaneTelemetry::find_datarefs() {
    latitude_ = XPLMFindDataRef("sim/flightmodel/position/latitude");
    longitude_ = XPLMFindDataRef("sim/flightmodel/position/longitude");
    elevation_ = XPLMFindDataRef("sim/flightmodel/position/elevation");
    ground_speed_ = XPLMFindDataRef("sim/flightmodel/position/groundspeed");
    heading_ = XPLMFindDataRef("sim/flightmodel/position/true_psi");
    vertical_speed_ = XPLMFindDataRef("sim/flightmodel/position/local_vy");
    aircraft_name_ = XPLMFindDataRef("sim/aircraft/view/acf_descrip");
    return latitude_ && longitude_ && elevation_ && ground_speed_ && heading_ && vertical_speed_;
}

void XPlaneTelemetry::sample() {
    TelemetrySnapshot snapshot;
    snapshot.latitude_degrees = XPLMGetDatad(latitude_);
    snapshot.longitude_degrees = XPLMGetDatad(longitude_);
    snapshot.altitude_feet = XPLMGetDatad(elevation_) * meters_to_feet;
    snapshot.ground_speed_knots = XPLMGetDataf(ground_speed_) * meters_per_second_to_knots;
    snapshot.heading_degrees = XPLMGetDataf(heading_);
    snapshot.vertical_speed_fpm = XPLMGetDataf(vertical_speed_) * meters_per_second_to_feet_per_minute;
    snapshot.aircraft_name = aircraft_name_value_;
    model_.update(std::move(snapshot));
}

float XPlaneTelemetry::flight_loop(float, float, int, void* refcon) {
    auto* telemetry = static_cast<XPlaneTelemetry*>(refcon);
    if (!telemetry) {
        return 0.0F;
    }
    telemetry->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
