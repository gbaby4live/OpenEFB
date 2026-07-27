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

bool MovingMapModel::poi_enabled() const noexcept {
    return layer_enabled(MapLayer::food) || layer_enabled(MapLayer::golf) ||
           layer_enabled(MapLayer::attractions);
}

void MovingMapModel::toggle_pois() noexcept {
    const bool enabled = !poi_enabled();
    layers_[static_cast<std::size_t>(MapLayer::food)] = enabled;
    layers_[static_cast<std::size_t>(MapLayer::golf)] = enabled;
    layers_[static_cast<std::size_t>(MapLayer::attractions)] = enabled;
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

void MovingMapModel::update_aircraft_position(double latitude_degrees,
                                               double longitude_degrees) noexcept {
    if (!valid_coordinates(latitude_degrees, longitude_degrees)) return;
    aircraft_latitude_degrees_ = latitude_degrees;
    aircraft_longitude_degrees_ = longitude_degrees;
    if (following_aircraft_ || !center_available_) {
        center_latitude_degrees_ = latitude_degrees;
        center_longitude_degrees_ = longitude_degrees;
        center_available_ = true;
    }
}

void MovingMapModel::pan_to(double latitude_degrees, double longitude_degrees) noexcept {
    if (!valid_coordinates(latitude_degrees, longitude_degrees)) return;
    center_latitude_degrees_ = latitude_degrees;
    center_longitude_degrees_ = longitude_degrees;
    center_available_ = true;
    following_aircraft_ = false;
}

void MovingMapModel::pan_by(double east_nm, double north_nm) noexcept {
    if (!center_available_) return;
    const auto coordinate = unproject(center_latitude_degrees_, center_longitude_degrees_,
                                      east_nm, north_nm);
    if (coordinate.valid) pan_to(coordinate.latitude_degrees, coordinate.longitude_degrees);
}

void MovingMapModel::recenter_on_aircraft() noexcept {
    following_aircraft_ = true;
    if (valid_coordinates(aircraft_latitude_degrees_, aircraft_longitude_degrees_)) {
        center_latitude_degrees_ = aircraft_latitude_degrees_;
        center_longitude_degrees_ = aircraft_longitude_degrees_;
        center_available_ = true;
    }
}

bool MovingMapModel::following_aircraft() const noexcept { return following_aircraft_; }
bool MovingMapModel::center_available() const noexcept { return center_available_; }
double MovingMapModel::center_latitude_degrees() const noexcept {
    return center_latitude_degrees_;
}
double MovingMapModel::center_longitude_degrees() const noexcept {
    return center_longitude_degrees_;
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

MapCoordinate MovingMapModel::unproject(double center_latitude_degrees,
                                        double center_longitude_degrees,
                                        double east_nm,
                                        double north_nm) const noexcept {
    MapCoordinate coordinate;
    if (!valid_coordinates(center_latitude_degrees, center_longitude_degrees) ||
        !std::isfinite(east_nm) || !std::isfinite(north_nm)) return coordinate;
    const double cosine = std::cos(center_latitude_degrees * pi / 180.0);
    if (std::abs(cosine) < 0.001) return coordinate;
    coordinate.latitude_degrees = center_latitude_degrees + north_nm / nautical_miles_per_degree;
    coordinate.longitude_degrees = center_longitude_degrees +
        east_nm / (nautical_miles_per_degree * cosine);
    while (coordinate.longitude_degrees > 180.0) coordinate.longitude_degrees -= 360.0;
    while (coordinate.longitude_degrees < -180.0) coordinate.longitude_degrees += 360.0;
    coordinate.valid = valid_coordinates(coordinate.latitude_degrees,
                                         coordinate.longitude_degrees);
    return coordinate;
}

} // namespace openefb
