#include "xplane_fuel.hpp"

#include <algorithm>
#include <array>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 1.0F;
constexpr double seconds_per_hour = 3600.0;

} // namespace

XPlaneFuel::XPlaneFuel(FuelModel& model, const TelemetryModel& telemetry_model)
    : model_(model), telemetry_model_(telemetry_model) {}

XPlaneFuel::~XPlaneFuel() { stop(); }

bool XPlaneFuel::start() {
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

    sample();
    XPLMScheduleFlightLoop(flight_loop_id_, sample_interval_seconds, 1);
    return true;
}

void XPlaneFuel::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    model_.mark_unavailable();
}

bool XPlaneFuel::find_datarefs() {
    fuel_remaining_ = XPLMFindDataRef("sim/flightmodel/weight/m_fuel_total");
    engine_fuel_flow_ = XPLMFindDataRef("sim/flightmodel/engine/ENGN_FF_");
    return fuel_remaining_ && engine_fuel_flow_;
}

void XPlaneFuel::sample() {
    std::array<float, 8> engine_flows{};
    const int count = XPLMGetDatavf(engine_fuel_flow_, engine_flows.data(), 0,
                                    static_cast<int>(engine_flows.size()));
    double total_flow_kg_per_second = 0.0;
    const int safe_count = std::clamp(count, 0, static_cast<int>(engine_flows.size()));
    for (int index = 0; index < safe_count; ++index) {
        total_flow_kg_per_second += std::max(0.0F, engine_flows[static_cast<std::size_t>(index)]);
    }
    const auto& telemetry = telemetry_model_.snapshot();
    model_.update(XPLMGetDataf(fuel_remaining_), total_flow_kg_per_second * seconds_per_hour,
                  telemetry.ground_speed_knots, telemetry.revision);
}

float XPlaneFuel::flight_loop(float, float, int, void* refcon) {
    auto* fuel = static_cast<XPlaneFuel*>(refcon);
    if (!fuel) {
        return 0.0F;
    }
    fuel->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
