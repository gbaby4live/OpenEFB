#include "xplane_window.hpp"

#include "openefb/core/window_geometry.hpp"

#include <XPLMGraphics.h>

#if IBM
#include <Windows.h>
#include <GL/gl.h>
#elif APL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace openefb::xplane {

namespace {

constexpr int initial_width = 860;
constexpr int initial_height = 580;

struct Color {
    float red;
    float green;
    float blue;
    float alpha;
};

constexpr Color canvas{0.045F, 0.067F, 0.094F, 1.0F};
constexpr Color status_bar{0.063F, 0.094F, 0.129F, 1.0F};
constexpr Color sidebar{0.035F, 0.051F, 0.075F, 1.0F};
constexpr Color active_navigation{0.055F, 0.255F, 0.345F, 1.0F};
constexpr Color card{0.078F, 0.110F, 0.145F, 1.0F};
constexpr Color accent{0.18F, 0.78F, 0.88F, 1.0F};
constexpr Color text_primary{0.88F, 0.93F, 0.97F, 1.0F};
constexpr Color text_muted{0.55F, 0.64F, 0.71F, 1.0F};
constexpr Color connected{0.30F, 0.86F, 0.53F, 1.0F};

void draw_rectangle(int left, int top, int right, int bottom, Color color) {
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glBegin(GL_QUADS);
    glVertex2i(left, bottom);
    glVertex2i(right, bottom);
    glVertex2i(right, top);
    glVertex2i(left, top);
    glEnd();
}

void draw_text(int x, int y, std::string_view value, Color color, XPLMFontID font = xplmFont_Proportional) {
    float rgb[]{color.red, color.green, color.blue};
    std::string mutable_value(value);
    XPLMDrawString(rgb, x, y, mutable_value.data(), nullptr, font);
}

std::string utc_time() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if IBM
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char value[16]{};
    std::snprintf(value, sizeof(value), "%02d:%02d UTC", utc.tm_hour, utc.tm_min);
    return value;
}

void draw_card(int left, int top, int right, int bottom, std::string_view title, std::string_view detail) {
    draw_rectangle(left, top, right, bottom, card);
    draw_text(left + 18, top - 30, title, text_primary);
    draw_text(left + 18, top - 57, detail, text_muted);
}

void draw_page_content(EfbPage page, int left, int top, int right, int bottom) {
    const int content_width = right - left;
    const int card_right = std::max(left + 260, right - 28);
    switch (page) {
    case EfbPage::home:
        draw_text(left, top, "Welcome aboard", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "OpenEFB is connected and ready for your flight.", text_muted);
        draw_card(left, top - 62, left + std::max(230, content_width / 2 - 10), top - 158,
                  "Flight overview", "No flight plan loaded");
        draw_card(left + std::max(250, content_width / 2 + 10), top - 62, card_right, top - 158,
                  "Simulator", "X-Plane connection active");
        draw_card(left, top - 178, card_right, top - 274,
                  "Next up", "Live aircraft data arrives in M3");
        break;
    case EfbPage::aircraft:
        draw_text(left, top, "Aircraft", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "Your active aircraft will appear here.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Simulator connection", "Connected to X-Plane");
        draw_card(left, top - 178, card_right, top - 274,
                  "Telemetry", "Position, speed, altitude, and heading in M3");
        break;
    case EfbPage::settings:
        draw_text(left, top, "Settings", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "OpenEFB preferences and display behavior.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Window geometry", "Position and size save automatically");
        draw_card(left, top - 178, card_right, top - 274,
                  "Appearance", "Dark cockpit theme");
        break;
    case EfbPage::about:
        draw_text(left, top, "About OpenEFB", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "An open-source electronic flight bag for X-Plane 12.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Version", "0.2.0 - M2 UI foundation");
        draw_card(left, top - 178, card_right, top - 274,
                  "Project", "Built in the open for the flight-sim community");
        break;
    }

    draw_text(left, bottom + 22, "OPEN EFB  /  M2", text_muted);
}

} // namespace

std::unique_ptr<WindowSurface> XPlaneWindow::create(UiModel& ui_model, XPlanePreferences& preferences) {
    auto window = std::unique_ptr<XPlaneWindow>(new XPlaneWindow(ui_model, preferences));
    if (!window->window_id_) {
        return nullptr;
    }
    return window;
}

XPlaneWindow::XPlaneWindow(UiModel& ui_model, XPlanePreferences& preferences)
    : ui_model_(ui_model), preferences_(preferences) {
    WindowGeometry geometry;
    if (const auto stored = preferences_.load_geometry()) {
        geometry = *stored;
    } else {
        int screen_left{};
        int screen_top{};
        int screen_right{};
        int screen_bottom{};
        XPLMGetScreenBoundsGlobal(&screen_left, &screen_top, &screen_right, &screen_bottom);
        const int screen_width = screen_right - screen_left;
        const int screen_height = screen_top - screen_bottom;
        geometry.left = screen_left + std::max(0, (screen_width - initial_width) / 2);
        geometry.top = screen_top - std::max(0, (screen_height - initial_height) / 2);
        geometry.right = geometry.left + initial_width;
        geometry.bottom = geometry.top - initial_height;
    }

    XPLMCreateWindow_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.left = geometry.left;
    parameters.top = geometry.top;
    parameters.right = geometry.right;
    parameters.bottom = geometry.bottom;
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
        XPLMSetWindowResizingLimits(window_id_, 680, 440, 1600, 1200);
        XPLMSetWindowPositioningMode(window_id_, xplm_WindowPositionFree, -1);
    }
}

XPlaneWindow::~XPlaneWindow() {
    if (window_id_) {
        save_geometry();
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
        save_geometry();
        XPLMSetWindowIsVisible(window_id_, 0);
    }
}

bool XPlaneWindow::visible() const {
    return window_id_ && XPLMGetWindowIsVisible(window_id_) != 0;
}

void XPlaneWindow::render(XPLMWindowID window_id) const {
    int left{};
    int top{};
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    draw_rectangle(left, top, right, bottom, canvas);
    draw_rectangle(left, top, right, top - status_bar_height, status_bar);
    draw_rectangle(left, top - status_bar_height, left + sidebar_width, bottom, sidebar);

    draw_text(left + 20, top - 35, "OpenEFB", accent, xplmFont_Basic);
    draw_text(left + sidebar_width + 24, top - 35, ui_model_.active_page_title(), text_primary);
    draw_text(right - 206, top - 35, "SIM CONNECTED", connected);
    draw_text(right - 92, top - 35, utc_time(), text_muted);

    const auto& items = navigation_items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        const int local_top = navigation_top + static_cast<int>(index) * (navigation_item_height + navigation_item_gap);
        const int item_top = top - local_top;
        if (items[index].page == ui_model_.active_page()) {
            draw_rectangle(left + 12, item_top, left + sidebar_width - 12,
                           item_top - navigation_item_height, active_navigation);
        }
        draw_text(left + 28, item_top - 28, items[index].label,
                  items[index].page == ui_model_.active_page() ? text_primary : text_muted);
    }

    const int content_left = left + sidebar_width + 30;
    const int content_top = top - status_bar_height - 38;
    draw_page_content(ui_model_.active_page(), content_left, content_top, right, bottom);
}

void XPlaneWindow::save_geometry() const {
    if (!window_id_) {
        return;
    }
    WindowGeometry geometry;
    XPLMGetWindowGeometry(window_id_, &geometry.left, &geometry.top, &geometry.right, &geometry.bottom);
    preferences_.save_geometry(geometry);
}

void XPlaneWindow::draw(XPLMWindowID window_id, void* refcon) {
    if (auto* window = static_cast<XPlaneWindow*>(refcon)) {
        window->render(window_id);
    }
}

int XPlaneWindow::handle_mouse(XPLMWindowID window_id, int x, int y, XPLMMouseStatus status, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window || status != xplm_MouseDown) {
        return 1;
    }

    int left{};
    int top{};
    XPLMGetWindowGeometry(window_id, &left, &top, nullptr, nullptr);
    window->ui_model_.select_at(x - left, top - y);
    return 1;
}

void XPlaneWindow::handle_key(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID, int, int, void*) {
    return xplm_CursorDefault;
}

int XPlaneWindow::handle_wheel(XPLMWindowID, int, int, int, int, void*) { return 0; }

} // namespace openefb::xplane
