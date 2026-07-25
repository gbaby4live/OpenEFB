#pragma once

#include "openefb/core/airspace_model.hpp"

#include <filesystem>
#include <thread>

namespace openefb::xplane {

class XPlaneAirspace final {
public:
    explicit XPlaneAirspace(AirspaceModel& model);
    ~XPlaneAirspace();
    XPlaneAirspace(const XPlaneAirspace&) = delete;
    XPlaneAirspace& operator=(const XPlaneAirspace&) = delete;
    bool start();
    void stop();

private:
    void load();
    AirspaceModel& model_;
    std::filesystem::path xplane_root_;
    std::thread worker_;
};

} // namespace openefb::xplane
