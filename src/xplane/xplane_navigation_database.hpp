#pragma once

#include "openefb/core/navigation_database_model.hpp"

#include <XPLMNavigation.h>
#include <XPLMProcessing.h>

#include <vector>

namespace openefb::xplane {

class XPlaneNavigationDatabase final {
public:
    explicit XPlaneNavigationDatabase(NavigationDatabaseModel& model);
    ~XPlaneNavigationDatabase();
    bool start();
    void stop();

private:
    static float flight_loop(float, float, int, void* refcon);
    float scan();

    NavigationDatabaseModel& model_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    XPLMNavRef next_{XPLM_NAV_NOT_FOUND};
    std::vector<MapNavigationPoint> points_;
};

} // namespace openefb::xplane
