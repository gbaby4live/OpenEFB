#pragma once

#include "openefb/core/moving_map_model.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace openefb::xplane {

struct MapTileViewport {
    int left{};
    int top{};
    int right{};
    int bottom{};
    double latitude_degrees{};
    double longitude_degrees{};
    double range_nm{};
};

class XPlaneMapTiles final {
public:
    explicit XPlaneMapTiles(std::filesystem::path cache_directory);
    ~XPlaneMapTiles();

    XPlaneMapTiles(const XPlaneMapTiles&) = delete;
    XPlaneMapTiles& operator=(const XPlaneMapTiles&) = delete;

    void draw(MapStyle style, const MapTileViewport& viewport);

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
