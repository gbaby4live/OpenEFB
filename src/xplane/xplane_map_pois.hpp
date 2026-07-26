#pragma once

#include "openefb/core/map_poi.hpp"

#include <memory>
#include <string>
#include <vector>

namespace openefb::xplane {

class XPlaneMapPois final {
public:
    XPlaneMapPois();
    ~XPlaneMapPois();
    XPlaneMapPois(const XPlaneMapPois&) = delete;
    XPlaneMapPois& operator=(const XPlaneMapPois&) = delete;

    void update(double latitude_degrees, double longitude_degrees, double range_nm);
    [[nodiscard]] std::vector<MapPoi> snapshot() const;
    [[nodiscard]] std::string status() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
