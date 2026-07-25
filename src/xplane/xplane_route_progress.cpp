#include "xplane_route_progress.hpp"

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 1.0F;

} // namespace

XPlaneRouteProgress::XPlaneRouteProgress(RouteProgressModel& model,
                                         const TelemetryModel& telemetry_model,
                                         const FlightPlanModel& flight_plan_model)
    : model_(model), telemetry_model_(telemetry_model), flight_plan_model_(flight_plan_model) {}

XPlaneRouteProgress::~XPlaneRouteProgress() { stop(); }

bool XPlaneRouteProgress::start() {
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

void XPlaneRouteProgress::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    model_.mark_unavailable();
}

void XPlaneRouteProgress::sample() {
    model_.update(telemetry_model_.snapshot(), flight_plan_model_.snapshot());
}

float XPlaneRouteProgress::flight_loop(float, float, int, void* refcon) {
    auto* progress = static_cast<XPlaneRouteProgress*>(refcon);
    if (!progress) {
        return 0.0F;
    }
    progress->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
