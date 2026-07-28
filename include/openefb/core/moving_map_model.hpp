#pragma once

#include <array>
#include <cstddef>

namespace openefb {

struct MapOffset {
    bool valid{false};
    double east_nm{};
    double north_nm{};
};

struct MapCoordinate {
    bool valid{false};
    double latitude_degrees{};
    double longitude_degrees{};
};

enum class MapStyle {
    street,
    topographic,
};

enum class MapLayer {
    weather,
    airports,
    navaids,
    airspace,
    traffic,
    food,
    golf,
    attractions,
};

class MovingMapModel final {
public:
    [[nodiscard]] double range_nm() const noexcept;
    [[nodiscard]] std::size_t zoom_level() const noexcept;
    [[nodiscard]] MapStyle style() const noexcept;
    void select_style(MapStyle style) noexcept;
    [[nodiscard]] bool layer_enabled(MapLayer layer) const noexcept;
    void toggle_layer(MapLayer layer) noexcept;
    [[nodiscard]] bool poi_enabled() const noexcept;
    void toggle_pois() noexcept;

    bool zoom_in() noexcept;
    bool zoom_out() noexcept;
    bool apply_wheel(int clicks) noexcept;

    void update_aircraft_position(double latitude_degrees,
                                  double longitude_degrees) noexcept;
    void pan_to(double latitude_degrees, double longitude_degrees) noexcept;
    void pan_by(double east_nm, double north_nm) noexcept;
    void recenter_on_aircraft() noexcept;
    [[nodiscard]] bool following_aircraft() const noexcept;
    [[nodiscard]] bool center_available() const noexcept;
    [[nodiscard]] double center_latitude_degrees() const noexcept;
    [[nodiscard]] double center_longitude_degrees() const noexcept;

    [[nodiscard]] MapOffset project(double center_latitude_degrees,
                                    double center_longitude_degrees,
                                    double latitude_degrees,
                                    double longitude_degrees) const noexcept;
    [[nodiscard]] MapCoordinate unproject(double center_latitude_degrees,
                                          double center_longitude_degrees,
                                          double east_nm,
                                          double north_nm) const noexcept;

private:
    static constexpr std::array ranges_nm_{0.005, 0.01, 0.02, 0.05, 0.1, 0.25, 0.5, 1.0,
                                           2.0, 5.0, 10.0, 20.0, 40.0, 80.0, 160.0, 320.0};
    std::size_t zoom_level_{12};
    MapStyle style_{MapStyle::street};
    std::array<bool, 8> layers_{true, true, true, false, true, true, true, true};
    bool following_aircraft_{true};
    bool center_available_{false};
    double center_latitude_degrees_{};
    double center_longitude_degrees_{};
    double aircraft_latitude_degrees_{};
    double aircraft_longitude_degrees_{};
};

} // namespace openefb
