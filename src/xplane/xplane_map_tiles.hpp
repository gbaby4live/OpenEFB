#pragma once

#include "openefb/core/moving_map_model.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace openefb::xplane {

enum class MapTileSource { vector_only, online, cache };

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
    [[nodiscard]] MapTileSource source() const noexcept;
    [[nodiscard]] std::string status_text() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
