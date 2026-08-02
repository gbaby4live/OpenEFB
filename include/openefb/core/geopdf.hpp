#pragma once

#include <array>
#include <optional>
#include <string_view>

namespace openefb {

struct GeoPdfReference {
    std::array<double, 4> media_box{};
    std::array<double, 4> viewport_box{};
    std::array<double, 8> geographic_points{};
    std::array<double, 8> local_points{};
};

struct GeoPdfPagePoint {
    double x{};
    double y{};
};

[[nodiscard]] std::optional<GeoPdfReference> parse_geopdf_reference(std::string_view pdf);
[[nodiscard]] std::optional<GeoPdfPagePoint> project_geopdf_position(
    const GeoPdfReference& reference, double latitude_degrees, double longitude_degrees);

} // namespace openefb
