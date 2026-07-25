#pragma once

#include "openefb/core/fuel_model.hpp"
#include "openefb/core/telemetry_model.hpp"

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>

namespace openefb::xplane {

class XPlaneFuel final {
public:
    XPlaneFuel(FuelModel& model, const TelemetryModel& telemetry_model);
    ~XPlaneFuel();

    XPlaneFuel(const XPlaneFuel&) = delete;
    XPlaneFuel& operator=(const XPlaneFuel&) = delete;

    bool start();
    void stop();

private:
    bool find_datarefs();
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    FuelModel& model_;
    const TelemetryModel& telemetry_model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    XPLMDataRef fuel_remaining_{nullptr};
    XPLMDataRef engine_fuel_flow_{nullptr};
};

} // namespace openefb::xplane
