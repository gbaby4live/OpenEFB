#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace openefb {

enum class PoiCategory {
    food,
    golf,
    attraction,
};

struct MapPoi {
    long long osm_id{};
    PoiCategory category{PoiCategory::attraction};
    std::string name;
    std::string detail;
    double latitude_degrees{};
    double longitude_degrees{};
};

[[nodiscard]] std::vector<MapPoi> parse_overpass_pois(std::string_view tab_separated);
[[nodiscard]] std::string_view poi_category_label(PoiCategory category) noexcept;

} // namespace openefb
