#pragma once

#include "openefb/core/traffic_model.hpp"

#include <memory>
#include <string>
#include <vector>

namespace openefb::xplane {

struct OnlineTrafficSnapshot {
    bool available{false};
    std::vector<TrafficTarget> targets;
    std::string status;
    bool degraded{false};
};

class XPlaneOnlineTraffic final {
public:
    XPlaneOnlineTraffic();
    ~XPlaneOnlineTraffic();

    XPlaneOnlineTraffic(const XPlaneOnlineTraffic&) = delete;
    XPlaneOnlineTraffic& operator=(const XPlaneOnlineTraffic&) = delete;

    void start();
    void stop();
    void request(double latitude_degrees, double longitude_degrees, int radius_nm);
    void request_route(std::string callsign);
    [[nodiscard]] OnlineTrafficSnapshot snapshot() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
