#pragma once

#include "openefb/core/window_controller.hpp"

#include <XPLMDisplay.h>

#include <memory>

namespace openefb::xplane {

class XPlaneWindow final : public WindowSurface {
public:
    static std::unique_ptr<WindowSurface> create();

    ~XPlaneWindow() override;

    XPlaneWindow(const XPlaneWindow&) = delete;
    XPlaneWindow& operator=(const XPlaneWindow&) = delete;

    void show() override;
    void hide() override;
    [[nodiscard]] bool visible() const override;

private:
    XPlaneWindow();

    static void draw(XPLMWindowID window_id, void* refcon);
    static int handle_mouse(XPLMWindowID window_id, int x, int y, XPLMMouseStatus status, void* refcon);
    static void handle_key(XPLMWindowID window_id, char key, XPLMKeyFlags flags, char virtual_key,
                           void* refcon, int losing_focus);
    static XPLMCursorStatus handle_cursor(XPLMWindowID window_id, int x, int y, void* refcon);
    static int handle_wheel(XPLMWindowID window_id, int x, int y, int wheel, int clicks, void* refcon);

    XPLMWindowID window_id_{nullptr};
};

} // namespace openefb::xplane
