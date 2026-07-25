#pragma once

namespace openefb {

struct WindowGeometry {
    int left{};
    int top{};
    int right{};
    int bottom{};

    [[nodiscard]] int width() const noexcept { return right - left; }
    [[nodiscard]] int height() const noexcept { return top - bottom; }
    [[nodiscard]] bool valid() const noexcept {
        return width() >= 480 && height() >= 320 && width() <= 3200 && height() <= 2400;
    }
};

} // namespace openefb
