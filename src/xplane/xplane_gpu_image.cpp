#include "xplane_gpu_image.hpp"

#include <XPLMGraphics.h>

#if APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

#include <cstddef>
#include <utility>

namespace openefb::xplane {

class XPlaneGpuImage::Implementation final {
public:
    ~Implementation() {
        if (texture_) {
            glDeleteTextures(1, &texture_);
        }
    }

    bool upload(int width, int height, const std::vector<std::uint8_t>& pixels,
                GpuPixelFormat format) {
        if (width <= 0 || height <= 0 ||
            pixels.size() != static_cast<std::size_t>(width) * height * 4) {
            status_ = "INVALID IMAGE BUFFER";
            return false;
        }

        if (!texture_) {
            int texture{};
            XPLMGenerateTextureNumbers(&texture, 1);
            texture_ = static_cast<GLuint>(texture);
        }

        // Keep this deliberately aligned with X-Plane's supported plug-in
        // texture path: bind through XPLM, upload the complete CPU-rendered
        // BGRA/RGBA surface, then let X-Plane own the fixed-function bridge.
        // Installing a plug-in shader here breaks the panel-coordinate
        // transform on some Vulkan/Zink configurations.
        XPLMBindTexture2d(static_cast<int>(texture_), 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        const GLenum external = format == GpuPixelFormat::bgra ? GL_BGRA : GL_RGBA;
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, external,
                     GL_UNSIGNED_BYTE, pixels.data());

        width_ = width;
        height_ = height;
        status_ = "X-PLANE IMAGE READY";
        return true;
    }

    bool draw(double left, double bottom, double right, double top,
              double u_left, double v_bottom, double u_right, double v_top,
              bool alpha_blend) const {
        if (!texture_ || width_ <= 0 || height_ <= 0) {
            return false;
        }

        // XPLMDrawString and other SDK drawing calls can change graphics state.
        // Bind and fully declare the required state immediately before every
        // raster draw. X-Plane supplies the panel-coordinate matrices.
        XPLMBindTexture2d(static_cast<int>(texture_), 0);
        XPLMSetGraphicsState(0, 1, 0, 0, alpha_blend ? 1 : 0, 0, 0);
        if (alpha_blend) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glColor3f(1.0F, 1.0F, 1.0F);
        glBegin(GL_QUADS);
        glTexCoord2d(u_left, v_bottom);
        glVertex2d(left, bottom);
        glTexCoord2d(u_left, v_top);
        glVertex2d(left, top);
        glTexCoord2d(u_right, v_top);
        glVertex2d(right, top);
        glTexCoord2d(u_right, v_bottom);
        glVertex2d(right, bottom);
        glEnd();
        return true;
    }

    [[nodiscard]] std::string status() const {
        return status_;
    }

private:
    GLuint texture_{};
    int width_{};
    int height_{};
    std::string status_{"X-PLANE IMAGE EMPTY"};
};

XPlaneGpuImage::XPlaneGpuImage() : implementation_(std::make_unique<Implementation>()) {}
XPlaneGpuImage::~XPlaneGpuImage() = default;

bool XPlaneGpuImage::upload(int width, int height,
                            const std::vector<std::uint8_t>& pixels,
                            GpuPixelFormat format) {
    return implementation_->upload(width, height, pixels, format);
}

bool XPlaneGpuImage::draw(double left, double bottom, double right, double top,
                          double u_left, double v_bottom,
                          double u_right, double v_top,
                          bool alpha_blend) const {
    return implementation_->draw(left, bottom, right, top,
                                 u_left, v_bottom, u_right, v_top, alpha_blend);
}

std::string XPlaneGpuImage::status() const {
    return implementation_->status();
}

} // namespace openefb::xplane
