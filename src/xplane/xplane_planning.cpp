#include "xplane_planning.hpp"

namespace openefb::xplane {
namespace { constexpr float sample_interval_seconds = 1.0F; }

XPlanePlanning::XPlanePlanning(PlanningModel& model, const FuelModel& fuel_model,
                               const RouteProgressModel& progress_model)
    : model_(model), fuel_model_(fuel_model), progress_model_(progress_model) {}

XPlanePlanning::~XPlanePlanning() { stop(); }

bool XPlanePlanning::start() {
    if (flight_loop_id_) return true;
    if (!find_datarefs()) { model_.mark_unavailable(); return false; }
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) { model_.mark_unavailable(); return false; }
    sample();
    XPLMScheduleFlightLoop(flight_loop_id_, sample_interval_seconds, 1);
    return true;
}

void XPlanePlanning::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    model_.mark_unavailable();
}

bool XPlanePlanning::find_datarefs() {
    empty_weight_ = XPLMFindDataRef("sim/aircraft/weight/acf_m_empty");
    payload_weight_ = XPLMFindDataRef("sim/flightmodel/weight/m_fixed");
    fuel_weight_ = XPLMFindDataRef("sim/flightmodel/weight/m_fuel_total");
    gross_weight_ = XPLMFindDataRef("sim/flightmodel/weight/m_total");
    maximum_gross_weight_ = XPLMFindDataRef("sim/aircraft/weight/acf_m_max");
    fuel_capacity_ = XPLMFindDataRef("sim/aircraft/weight/acf_m_fuel_tot");
    cg_offset_ = XPLMFindDataRef("sim/flightmodel/misc/cgz_ref_to_default");
    return empty_weight_ && payload_weight_ && fuel_weight_ && gross_weight_ &&
           maximum_gross_weight_ && cg_offset_;
}

void XPlanePlanning::sample() {
    AircraftLoading loading;
    loading.empty_weight_kg = XPLMGetDataf(empty_weight_);
    loading.payload_weight_kg = XPLMGetDataf(payload_weight_);
    loading.fuel_weight_kg = XPLMGetDataf(fuel_weight_);
    loading.gross_weight_kg = XPLMGetDataf(gross_weight_);
    loading.maximum_gross_weight_kg = XPLMGetDataf(maximum_gross_weight_);
    loading.fuel_capacity_kg = fuel_capacity_ ? XPLMGetDataf(fuel_capacity_) : 0.0;
    loading.cg_offset_meters = XPLMGetDataf(cg_offset_);
    model_.update(loading, fuel_model_.snapshot(), progress_model_.snapshot());
}

float XPlanePlanning::flight_loop(float, float, int, void* refcon) {
    auto* planning = static_cast<XPlanePlanning*>(refcon);
    if (!planning) return 0.0F;
    planning->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
