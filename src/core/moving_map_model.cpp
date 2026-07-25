#include "openefb/core/moving_map_model.hpp"

#include <cmath>

namespace openefb {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double nautical_miles_per_degree = 60.0;

bool valid_coordinates(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude) &&
           latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

double normalized_longitude_delta(double delta) {
    while (delta > 180.0) {
        delta -= 360.0;
    }
    while (delta < -180.0) {
        delta += 360.0;
    }
    return delta;
}

} // namespace

double MovingMapModel::range_nm() const noexcept { return ranges_nm_[zoom_level_]; }

std::size_t MovingMapModel::zoom_level() const noexcept { return zoom_level_; }

MapStyle MovingMapModel::style() const noexcept { return style_; }

void MovingMapModel::select_style(MapStyle style) noexcept { style_ = style; }

bool MovingMapModel::layer_enabled(MapLayer layer) const noexcept {
    return layers_[static_cast<std::size_t>(layer)];
}

void MovingMapModel::toggle_layer(MapLayer layer) noexcept {
    const auto index = static_cast<std::size_t>(layer);
    layers_[index] = !layers_[index];
}

bool MovingMapModel::zoom_in() noexcept {
    if (zoom_level_ == 0) {
        return false;
    }
    --zoom_level_;
    return true;
}

bool MovingMapModel::zoom_out() noexcept {
    if (zoom_level_ + 1 >= ranges_nm_.size()) {
        return false;
    }
    ++zoom_level_;
    return true;
}

bool MovingMapModel::apply_wheel(int clicks) noexcept {
    bool changed = false;
    while (clicks > 0) {
        changed = zoom_in() || changed;
        --clicks;
    }
    while (clicks < 0) {
        changed = zoom_out() || changed;
        ++clicks;
    }
    return changed;
}

MapOffset MovingMapModel::project(double center_latitude_degrees,
                                  double center_longitude_degrees,
                                  double latitude_degrees,
                                  double longitude_degrees) const noexcept {
    MapOffset offset;
    if (!valid_coordinates(center_latitude_degrees, center_longitude_degrees) ||
        !valid_coordinates(latitude_degrees, longitude_degrees)) {
        return offset;
    }
    const double longitude_delta =
        normalized_longitude_delta(longitude_degrees - center_longitude_degrees);
    offset.valid = true;
    offset.east_nm = longitude_delta * nautical_miles_per_degree *
                     std::cos(center_latitude_degrees * pi / 180.0);
    offset.north_nm = (latitude_degrees - center_latitude_degrees) * nautical_miles_per_degree;
    return offset;
}

} // namespace openefb
