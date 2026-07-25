#pragma once

#include "openefb/core/fuel_model.hpp"
#include "openefb/core/planning_model.hpp"
#include "openefb/core/route_progress_model.hpp"

#include <XPLMDataAccess.h>
#include <XPLMProcessing.h>

namespace openefb::xplane {

class XPlanePlanning final {
public:
    XPlanePlanning(PlanningModel& model, const FuelModel& fuel_model,
                   const RouteProgressModel& progress_model);
    ~XPlanePlanning();
    XPlanePlanning(const XPlanePlanning&) = delete;
    XPlanePlanning& operator=(const XPlanePlanning&) = delete;
    bool start();
    void stop();

private:
    bool find_datarefs();
    void sample();
    static float flight_loop(float, float, int, void* refcon);
    PlanningModel& model_;
    const FuelModel& fuel_model_;
    const RouteProgressModel& progress_model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    XPLMDataRef empty_weight_{nullptr};
    XPLMDataRef payload_weight_{nullptr};
    XPLMDataRef fuel_weight_{nullptr};
    XPLMDataRef gross_weight_{nullptr};
    XPLMDataRef maximum_gross_weight_{nullptr};
    XPLMDataRef fuel_capacity_{nullptr};
    XPLMDataRef cg_offset_{nullptr};
};

} // namespace openefb::xplane
