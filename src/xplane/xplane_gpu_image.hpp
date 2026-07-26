#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openefb::xplane {

enum class GpuPixelFormat { rgba, bgra };

class XPlaneGpuImage final {
public:
    XPlaneGpuImage();
    ~XPlaneGpuImage();
    XPlaneGpuImage(const XPlaneGpuImage&) = delete;
    XPlaneGpuImage& operator=(const XPlaneGpuImage&) = delete;

    bool upload(int width, int height, const std::vector<std::uint8_t>& pixels,
                GpuPixelFormat format);
    bool draw(double left, double bottom, double right, double top,
              double u_left = 0.0, double v_bottom = 1.0,
              double u_right = 1.0, double v_top = 0.0) const;
    [[nodiscard]] std::string status() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
