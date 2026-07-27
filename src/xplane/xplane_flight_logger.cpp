#include "xplane_flight_logger.hpp"

namespace openefb::xplane {
XPlaneFlightLogger::XPlaneFlightLogger(FlightLogModel& model, const TelemetryModel& telemetry,
                                       const FlightPlanModel& flight_plan)
    : model_(model), telemetry_(telemetry), flight_plan_(flight_plan) {}
XPlaneFlightLogger::~XPlaneFlightLogger() { stop(); }
bool XPlaneFlightLogger::start() {
    if (flight_loop_id_) return true;
    on_ground_ = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    if (!on_ground_) return false;
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) return false;
    XPLMScheduleFlightLoop(flight_loop_id_, 1.0F, 1);
    return true;
}
void XPlaneFlightLogger::stop() {
    if (flight_loop_id_) { XPLMDestroyFlightLoop(flight_loop_id_); flight_loop_id_ = nullptr; }
}
void XPlaneFlightLogger::sample(float elapsed_seconds) {
    model_.update(telemetry_.snapshot(), flight_plan_.snapshot(), XPLMGetDatai(on_ground_) != 0, elapsed_seconds);
}
float XPlaneFlightLogger::flight_loop(float elapsed, float, int, void* refcon) {
    auto* logger = static_cast<XPlaneFlightLogger*>(refcon);
    if (!logger) return 0.0F;
    logger->sample(elapsed);
    return 1.0F;
}
} // namespace openefb::xplane
