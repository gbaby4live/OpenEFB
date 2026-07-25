#include "xplane_window.hpp"

#include <XPLMGraphics.h>

#include <algorithm>

namespace openefb::xplane {

namespace {

constexpr int initial_width = 760;
constexpr int initial_height = 540;

} // namespace

std::unique_ptr<WindowSurface> XPlaneWindow::create() {
    auto window = std::unique_ptr<XPlaneWindow>(new XPlaneWindow());
    if (!window->window_id_) {
        return nullptr;
    }
    return window;
}

XPlaneWindow::XPlaneWindow() {
    int screen_left{};
    int screen_top{};
    int screen_right{};
    int screen_bottom{};
    XPLMGetScreenBoundsGlobal(&screen_left, &screen_top, &screen_right, &screen_bottom);

    const int screen_width = screen_right - screen_left;
    const int screen_height = screen_top - screen_bottom;
    const int left = screen_left + std::max(0, (screen_width - initial_width) / 2);
    const int top = screen_top - std::max(0, (screen_height - initial_height) / 2);

    XPLMCreateWindow_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.left = left;
    parameters.top = top;
    parameters.right = left + initial_width;
    parameters.bottom = top - initial_height;
    parameters.visible = 0;
    parameters.drawWindowFunc = draw;
    parameters.handleMouseClickFunc = handle_mouse;
    parameters.handleKeyFunc = handle_key;
    parameters.handleCursorFunc = handle_cursor;
    parameters.handleMouseWheelFunc = handle_wheel;
    parameters.refcon = this;
    parameters.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    parameters.layer = xplm_WindowLayerFloatingWindows;
    parameters.handleRightClickFunc = handle_mouse;

    window_id_ = XPLMCreateWindowEx(&parameters);
    if (window_id_) {
        XPLMSetWindowTitle(window_id_, "OpenEFB");
        XPLMSetWindowResizingLimits(window_id_, 480, 320, 1600, 1200);
        XPLMSetWindowPositioningMode(window_id_, xplm_WindowPositionFree, -1);
    }
}

XPlaneWindow::~XPlaneWindow() {
    if (window_id_) {
        XPLMDestroyWindow(window_id_);
    }
}

void XPlaneWindow::show() {
    if (window_id_) {
        XPLMSetWindowIsVisible(window_id_, 1);
        XPLMBringWindowToFront(window_id_);
    }
}

void XPlaneWindow::hide() {
    if (window_id_) {
        XPLMSetWindowIsVisible(window_id_, 0);
    }
}

bool XPlaneWindow::visible() const {
    return window_id_ && XPLMGetWindowIsVisible(window_id_) != 0;
}

void XPlaneWindow::draw(XPLMWindowID window_id, void*) {
    int left{};
    int top{};
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);

    XPLMSetGraphicsState(0, 0, 0, 0, 0, 0, 0);
    float color[]{0.82F, 0.88F, 0.94F};
    char heading[] = "OpenEFB";
    char status[] = "M1 shell ready";
    XPLMDrawString(color, left + 24, top - 42, heading, nullptr, xplmFont_Proportional);
    XPLMDrawString(color, left + 24, top - 68, status, nullptr, xplmFont_Proportional);
}

int XPlaneWindow::handle_mouse(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }

void XPlaneWindow::handle_key(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID, int, int, void*) {
    return xplm_CursorDefault;
}

int XPlaneWindow::handle_wheel(XPLMWindowID, int, int, int, int, void*) { return 0; }

} // namespace openefb::xplane
