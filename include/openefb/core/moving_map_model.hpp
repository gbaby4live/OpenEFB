#pragma once

#include <array>
#include <cstddef>

namespace openefb {

struct MapOffset {
    bool valid{false};
    double east_nm{};
    double north_nm{};
};

enum class MapStyle {
    street,
    topographic,
};

class MovingMapModel final {
public:
    [[nodiscard]] double range_nm() const noexcept;
    [[nodiscard]] std::size_t zoom_level() const noexcept;
    [[nodiscard]] MapStyle style() const noexcept;
    void select_style(MapStyle style) noexcept;

    bool zoom_in() noexcept;
    bool zoom_out() noexcept;
    bool apply_wheel(int clicks) noexcept;

    [[nodiscard]] MapOffset project(double center_latitude_degrees,
                                    double center_longitude_degrees,
                                    double latitude_degrees,
                                    double longitude_degrees) const noexcept;

private:
    static constexpr std::array ranges_nm_{5.0, 10.0, 20.0, 40.0, 80.0, 160.0, 320.0};
    std::size_t zoom_level_{3};
    MapStyle style_{MapStyle::street};
};

} // namespace openefb
