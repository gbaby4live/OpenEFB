#include "xplane_window.hpp"

#include "xplane_gpu_image.hpp"

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
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>

namespace openefb::xplane {

namespace {

constexpr int initial_width = 1000;
constexpr int initial_height = 700;
constexpr int minimum_width = 360;
constexpr int minimum_height = 300;
constexpr int compact_width_threshold = 720;

struct Color {
    float red;
    float green;
    float blue;
    float alpha;
};

constexpr Color canvas{0.105F, 0.135F, 0.160F, 1.0F};
constexpr Color status_bar{0.035F, 0.055F, 0.072F, 1.0F};
constexpr Color sidebar{0.075F, 0.105F, 0.130F, 1.0F};
constexpr Color active_navigation{0.025F, 0.365F, 0.300F, 1.0F};
constexpr Color card{0.155F, 0.195F, 0.225F, 1.0F};
constexpr Color accent{0.16F, 0.84F, 0.96F, 1.0F};
constexpr Color hover_accent{1.0F, 0.82F, 0.18F, 1.0F};
constexpr Color text_primary{1.0F, 1.0F, 1.0F, 1.0F};
constexpr Color text_muted{0.80F, 0.84F, 0.87F, 1.0F};
constexpr Color connected{0.10F, 1.0F, 0.35F, 1.0F};
constexpr Color map_grid{0.34F, 0.40F, 0.43F, 1.0F};
constexpr Color route_line{1.0F, 0.12F, 0.82F, 1.0F};
bool high_contrast_mode = false;
int hover_cursor_x{};
int hover_cursor_y{};
bool hover_cursor_active{false};
int text_clip_left{-100000};
int text_clip_top{100000};
int text_clip_right{100000};
int text_clip_bottom{-100000};

std::string shortened(std::string_view value, std::size_t maximum);

std::string traffic_key(const TrafficTarget& target) {
    if (target.mode_s_id != 0) return "HEX:" + std::to_string(target.mode_s_id);
    return "CALL:" + target.callsign;
}

int matching_route_index(const FlightPlanSnapshot& route, const FlightPlanLeg& candidate) {
    for (std::size_t index = 0; index < route.legs.size(); ++index) {
        const auto& leg = route.legs[index];
        const bool same_identifier = candidate.kind != WaypointKind::coordinate &&
            !candidate.identifier.empty() && leg.kind == candidate.kind &&
            leg.identifier == candidate.identifier;
        const bool same_coordinate =
            std::abs(leg.latitude_degrees - candidate.latitude_degrees) < 0.00015 &&
            std::abs(std::remainder(leg.longitude_degrees - candidate.longitude_degrees,
                                    360.0)) < 0.00015;
        if (same_identifier || same_coordinate) return static_cast<int>(index);
    }
    return -1;
}

PendingMapNavigation map_navigation_for(const FlightPlanSnapshot& route,
                                        FlightPlanLeg leg, std::string name,
                                        std::string detail) {
    PendingMapNavigation pending{std::move(leg), std::move(name), std::move(detail)};
    pending.existing_route_index = matching_route_index(route, pending.leg);
    pending.removable_from_route = pending.existing_route_index > 0 &&
        pending.existing_route_index + 1 < static_cast<int>(route.legs.size());
    return pending;
}

bool compact_layout(int left, int right) {
    return right - left < compact_width_threshold;
}

int content_left_for(int left, int right) {
    return compact_layout(left, right) ? left + 12 : left + sidebar_width + 30;
}

int viewer_left_for(int left, int right) {
    return compact_layout(left, right) ? left + 8 : left + sidebar_width + 18;
}

int responsive_card_right(int left, int right, int preferred_minimum = 260) {
    const int margin = right - left < 520 ? 10 : 28;
    return std::max(left + std::min(preferred_minimum, std::max(220, right - left - margin)),
                    right - margin);
}

int page_content_height(EfbPage page) {
    switch (page) {
    case EfbPage::home: return 0;
    case EfbPage::flight_plan: return 620;
    case EfbPage::airports: return 620;
    case EfbPage::progress: return 470;
    case EfbPage::weather: return 430;
    case EfbPage::planning: return 500;
    case EfbPage::briefing: return 650;
    case EfbPage::logbook: return 570;
    case EfbPage::settings: return 1156;
    case EfbPage::about: return 660;
    }
    return 0;
}

int page_scroll_limit(EfbPage page, int content_top, int bottom) {
    const int available = std::max(1, content_top - (bottom + 24));
    return std::max(0, page_content_height(page) - available);
}


void prepare_solid_geometry() {
    // Raster images and SDK text both mutate the legacy bridge state. Declare
    // an untextured, conventional-alpha state before every solid primitive so
    // cards cannot accidentally inherit the basemap texture or blend mode.
    XPLMSetGraphicsState(0, 0, 0, 0, 0, 0, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

struct SolidFillTexture {
    std::array<std::uint8_t, 3> rgb{};
    std::unique_ptr<XPlaneGpuImage> image;
};

bool draw_textured_solid_fill(int left, int top, int right, int bottom, Color color) {
    if (color.alpha < 0.999F) return false;
    const auto channel = [](float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    const std::array<std::uint8_t, 3> rgb{channel(color.red), channel(color.green), channel(color.blue)};
    static std::vector<SolidFillTexture> fills;
    auto found = std::find_if(fills.begin(), fills.end(), [&rgb](const SolidFillTexture& fill) {
        return fill.rgb == rgb;
    });
    if (found == fills.end()) {
        // BGRA, alpha 255: uses the same X-Plane-managed texture bridge that
        // already renders map tiles correctly under Vulkan/Zink.
        std::vector<std::uint8_t> pixels{
            rgb[2], rgb[1], rgb[0], 255,
            rgb[2], rgb[1], rgb[0], 255,
            rgb[2], rgb[1], rgb[0], 255,
            rgb[2], rgb[1], rgb[0], 255,
        };
        SolidFillTexture fill;
        fill.rgb = rgb;
        fill.image = std::make_unique<XPlaneGpuImage>();
        if (!fill.image->upload(2, 2, pixels, GpuPixelFormat::bgra)) return false;
        fills.push_back(std::move(fill));
        found = std::prev(fills.end());
    }
    return found->image->draw(left, bottom, right, top);
}

void draw_rectangle(int left, int top, int right, int bottom, Color color) {
    left = std::max(left, text_clip_left);
    right = std::min(right, text_clip_right);
    top = std::min(top, text_clip_top);
    bottom = std::max(bottom, text_clip_bottom);
    if (left >= right || bottom >= top) return;
    if (draw_textured_solid_fill(left, top, right, bottom, color)) return;
    prepare_solid_geometry();
    if (color.alpha < 0.999F) {
        XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glBegin(GL_QUADS);
    glVertex2i(left, bottom);
    glVertex2i(right, bottom);
    glVertex2i(right, top);
    glVertex2i(left, top);
    glEnd();
}

void draw_border(int left, int top, int right, int bottom, Color color, float width = 2.0F) {
    left = std::max(left, text_clip_left);
    right = std::min(right, text_clip_right);
    top = std::min(top, text_clip_top);
    bottom = std::max(bottom, text_clip_bottom);
    if (left >= right || bottom >= top) return;
    prepare_solid_geometry();
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glLineWidth(width);
    glBegin(GL_LINE_LOOP);
    glVertex2i(left, bottom);
    glVertex2i(right, bottom);
    glVertex2i(right, top);
    glVertex2i(left, top);
    glEnd();
    glLineWidth(1.0F);
}

void draw_line(double x_1, double y_1, double x_2, double y_2, Color color, float width = 1.0F) {
    if ((x_1 < text_clip_left && x_2 < text_clip_left) ||
        (x_1 > text_clip_right && x_2 > text_clip_right) ||
        (y_1 < text_clip_bottom && y_2 < text_clip_bottom) ||
        (y_1 > text_clip_top && y_2 > text_clip_top)) return;
    x_1 = std::clamp(x_1, static_cast<double>(text_clip_left),
                     static_cast<double>(text_clip_right));
    x_2 = std::clamp(x_2, static_cast<double>(text_clip_left),
                     static_cast<double>(text_clip_right));
    y_1 = std::clamp(y_1, static_cast<double>(text_clip_bottom),
                     static_cast<double>(text_clip_top));
    y_2 = std::clamp(y_2, static_cast<double>(text_clip_bottom),
                     static_cast<double>(text_clip_top));
    prepare_solid_geometry();
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glLineWidth(width);
    glBegin(GL_LINES);
    glVertex2d(x_1, y_1);
    glVertex2d(x_2, y_2);
    glEnd();
    glLineWidth(1.0F);
}

bool clip_line(double& x_1, double& y_1, double& x_2, double& y_2,
               int left, int top, int right, int bottom) {
    const double delta_x = x_2 - x_1;
    const double delta_y = y_2 - y_1;
    const double p[]{-delta_x, delta_x, -delta_y, delta_y};
    const double q[]{x_1 - left, right - x_1, y_1 - bottom, top - y_1};
    double minimum = 0.0;
    double maximum = 1.0;
    for (int index = 0; index < 4; ++index) {
        if (std::abs(p[index]) < 0.000001) {
            if (q[index] < 0.0) {
                return false;
            }
            continue;
        }
        const double ratio = q[index] / p[index];
        if (p[index] < 0.0) {
            minimum = std::max(minimum, ratio);
        } else {
            maximum = std::min(maximum, ratio);
        }
        if (minimum > maximum) {
            return false;
        }
    }
    const double original_x = x_1;
    const double original_y = y_1;
    x_1 = original_x + minimum * delta_x;
    y_1 = original_y + minimum * delta_y;
    x_2 = original_x + maximum * delta_x;
    y_2 = original_y + maximum * delta_y;
    return true;
}

void draw_text(int x, int y, std::string_view value, Color color, XPLMFontID font = xplmFont_Proportional) {
    if (x < text_clip_left || x >= text_clip_right ||
        y > text_clip_top || y < text_clip_bottom) return;
    if (high_contrast_mode) {
        color.red = std::max(color.red, 0.88F);
        color.green = std::max(color.green, 0.88F);
        color.blue = std::max(color.blue, 0.88F);
    }
    float rgb[]{color.red, color.green, color.blue};
    std::string mutable_value(value);
    const float available_width = static_cast<float>(text_clip_right - x - 5);
    if (available_width <= 4.0F) return;
    if (XPLMMeasureString(font, mutable_value.c_str(), static_cast<int>(mutable_value.size())) >
        available_width) {
        constexpr std::string_view ellipsis = "...";
        if (XPLMMeasureString(font, ellipsis.data(), static_cast<int>(ellipsis.size())) >
            available_width) return;
        std::size_t low = 0;
        std::size_t high = mutable_value.size();
        while (low < high) {
            const std::size_t middle = (low + high + 1) / 2;
            const std::string candidate = mutable_value.substr(0, middle) + std::string(ellipsis);
            if (XPLMMeasureString(font, candidate.c_str(), static_cast<int>(candidate.size())) <=
                available_width) {
                low = middle;
            } else {
                high = middle - 1;
            }
        }
        mutable_value = mutable_value.substr(0, low) + std::string(ellipsis);
    }
    XPLMDrawString(rgb, x, y, mutable_value.data(), nullptr, font);
}

bool point_in_rectangle(int x, int y, int left, int top, int right, int bottom) {
    return x >= left && x <= right && y <= top && y >= bottom;
}

void draw_shadowed_text(int x, int y, std::string_view value, Color color,
                        XPLMFontID font = xplmFont_Proportional) {
    draw_text(x + 1, y - 1, value, Color{0.0F, 0.0F, 0.0F, 1.0F}, font);
    draw_text(x, y, value, color, font);
}

void draw_tactical_corners(int left, int top, int right, int bottom, Color color,
                           int length = 6) {
    draw_line(left, top, left + length, top, color, 1.5F);
    draw_line(left, top, left, top - length, color, 1.5F);
    draw_line(right, top, right - length, top, color, 1.5F);
    draw_line(right, top, right, top - length, color, 1.5F);
    draw_line(left, bottom, left + length, bottom, color, 1.5F);
    draw_line(left, bottom, left, bottom + length, color, 1.5F);
    draw_line(right, bottom, right - length, bottom, color, 1.5F);
    draw_line(right, bottom, right, bottom + length, color, 1.5F);
}

void draw_map_label(int x, int y, std::string_view value, Color color) {
    const int width = std::clamp(static_cast<int>(value.size()) * 7 + 10, 34, 250);
    draw_rectangle(x - 4, y + 13, x + width, y - 5,
                   Color{0.018F, 0.045F, 0.070F, 0.88F});
    draw_border(x - 4, y + 13, x + width, y - 5,
                Color{0.32F, 0.82F, 0.94F, 0.90F}, 1.0F);
    draw_tactical_corners(x - 4, y + 13, x + width, y - 5, color, 5);
    // X-Plane's proportional face remains sharp through Vulkan/Zink and gives
    // map labels a consistent cockpit-instrument typography on every DPI.
    draw_shadowed_text(x + 1, y, shortened(value, 34), text_primary,
                       xplmFont_Proportional);
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
    draw_rectangle(left + 4, top - 4, right + 4, bottom - 4, status_bar);
    draw_rectangle(left, top, right, bottom, card);
    draw_rectangle(left, top, right, top - 3, accent);
    draw_rectangle(left, top, left + 4, bottom, active_navigation);
    draw_text(left + 18, top - 30, title, text_primary);
    draw_text(left + 18, top - 57, detail, text_muted);
}

constexpr int minimum_route_line_width = 2;
constexpr int maximum_route_line_width = 12;

int route_width_from_slider(int x, int left, int right) {
    if (right <= left) return minimum_route_line_width;
    const double ratio = static_cast<double>(std::clamp(x, left, right) - left) /
                         static_cast<double>(right - left);
    return std::clamp(
        minimum_route_line_width + static_cast<int>(std::lround(
            ratio * (maximum_route_line_width - minimum_route_line_width))),
        minimum_route_line_width, maximum_route_line_width);
}

int route_slider_position(int width, int left, int right) {
    const double ratio = static_cast<double>(
        std::clamp(width, minimum_route_line_width, maximum_route_line_width) -
        minimum_route_line_width) /
        static_cast<double>(maximum_route_line_width - minimum_route_line_width);
    return left + static_cast<int>(std::lround(ratio * (right - left)));
}

void draw_button(int left, int top, int right, int bottom, std::string_view label,
                 bool emphasized = false) {
    const bool hovered = hover_cursor_active &&
                         point_in_rectangle(hover_cursor_x, hover_cursor_y,
                                            left, top, right, bottom);
    const Color fill = hovered ? Color{0.08F, 0.46F, 0.40F, 1.0F}
        : emphasized ? active_navigation
                     : Color{0.115F, 0.175F, 0.215F, 1.0F};
    draw_rectangle(left + 3, top - 3, right + 3, bottom - 3, status_bar);
    draw_rectangle(left, top, right, bottom, fill);
    // Filled control blocks deliberately replace the former thin outline style.
    draw_rectangle(left, top, right, top - 3,
                   hovered ? hover_accent : (emphasized ? connected : map_grid));
    const float width = XPLMMeasureString(xplmFont_Basic, label.data(),
                                          static_cast<int>(label.size()));
    draw_shadowed_text(std::max(left + 6, (left + right - static_cast<int>(width)) / 2),
                       bottom + 9, label, text_primary);
}

std::string format_leg_detail(const FlightPlanLeg& leg) {
    char value[112]{};
    if (leg.altitude_feet != 0) {
        std::snprintf(value, sizeof(value), "%d ft  |  %.4f, %.4f",
                      leg.altitude_feet, leg.latitude_degrees, leg.longitude_degrees);
    } else {
        std::snprintf(value, sizeof(value), "%.4f, %.4f",
                      leg.latitude_degrees, leg.longitude_degrees);
    }
    return value;
}

std::size_t editor_visible_start(const FlightPlanEditor& editor, std::size_t visible_count) {
    if (editor.selected_index() < 0 || visible_count >= editor.legs().size()) {
        return 0;
    }
    const std::size_t selected = static_cast<std::size_t>(editor.selected_index());
    const std::size_t half = visible_count / 2;
    return std::min(selected > half ? selected - half : 0,
                    editor.legs().size() - visible_count);
}

void draw_flight_plan_editor(const FlightPlanEditor& editor,
                             int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right, 420);
    draw_text(left, top, "Flight Plan Builder", text_primary, xplmFont_Proportional);
    draw_button(card_right - 108, top + 8, card_right, top - 25, "Approach", true);
    std::string editor_message(editor.message());
    if (editor_message.size() > 62) editor_message = editor_message.substr(0, 59) + "...";
    draw_text(left, top - 27, editor_message, text_muted);

    const int endpoint_gap = 8;
    const int endpoint_middle = (left + card_right) / 2;
    draw_rectangle(left, top - 41, endpoint_middle - endpoint_gap / 2, top - 76, card);
    draw_border(left, top - 41, endpoint_middle - endpoint_gap / 2, top - 76, map_grid, 1.0F);
    draw_text(left + 11, top - 63,
              "DEPARTURE  " + std::string(editor.departure() ? editor.departure()->identifier : "NOT SET"),
              editor.departure() ? text_primary : text_muted);
    draw_rectangle(endpoint_middle + endpoint_gap / 2, top - 41, card_right, top - 76, card);
    draw_border(endpoint_middle + endpoint_gap / 2, top - 41, card_right, top - 76, map_grid, 1.0F);
    draw_text(endpoint_middle + endpoint_gap / 2 + 11, top - 63,
              "DESTINATION  " + std::string(editor.destination() ? editor.destination()->identifier : "NOT SET"),
              editor.destination() ? text_primary : text_muted);

    const int set_departure_left = card_right - 226;
    draw_rectangle(left, top - 87, set_departure_left - 8, top - 122, card);
    draw_border(left, top - 87, set_departure_left - 8, top - 122, map_grid, 1.0F);
    const std::string entry = editor.input().empty()
                                  ? "Type waypoint identifier..."
                                  : std::string(editor.input()) + "_";
    draw_text(left + 12, top - 110, entry, editor.input().empty() ? text_muted : text_primary);
    draw_button(set_departure_left, top - 87, card_right - 156, top - 122, "Set DEP", true);
    draw_button(card_right - 148, top - 87, card_right - 78, top - 122, "Set DEST", true);
    draw_button(card_right - 70, top - 87, card_right, top - 122, "Add VIA");

    constexpr int action_top_offset = 134;
    constexpr int action_bottom_offset = 165;
    draw_button(left, top - action_top_offset, left + 56, top - action_bottom_offset, "Up");
    draw_button(left + 64, top - action_top_offset, left + 132, top - action_bottom_offset, "Down");
    draw_button(left + 140, top - action_top_offset, left + 222, top - action_bottom_offset, "Remove");
    draw_button(card_right - 158, top - action_top_offset, card_right - 84,
                top - action_bottom_offset, "Flight Plan");
    draw_button(card_right - 76, top - action_top_offset, card_right,
                top - action_bottom_offset, "Apply", editor.dirty());

    constexpr int row_height = 42;
    constexpr int row_gap = 5;
    const int row_start = top - 180;
    const int available_height = std::max(row_height, row_start - (bottom + 48));
    const std::size_t visible_count = std::min<std::size_t>(
        editor.legs().size(), static_cast<std::size_t>(std::max(1, available_height / (row_height + row_gap))));
    const std::size_t start = editor_visible_start(editor, visible_count);
    int row_top = row_start;
    for (std::size_t offset = 0; offset < visible_count; ++offset) {
        const std::size_t index = start + offset;
        const auto& leg = editor.legs()[index];
        const bool selected = static_cast<int>(index) == editor.selected_index();
        draw_rectangle(left, row_top, card_right, row_top - row_height,
                       selected ? active_navigation : card);
        if (selected) {
            draw_border(left, row_top, card_right, row_top - row_height, accent, 1.0F);
        }
        draw_text(left + 12, row_top - 26,
                  std::to_string(index + 1) + "  " + leg.identifier,
                  selected ? accent : text_primary);
        draw_text(left + 180, row_top - 26, format_leg_detail(leg), text_muted);
        row_top -= row_height + row_gap;
    }
    if (editor.legs().empty()) {
        draw_text(left + 12, row_start - 28,
                  "Draft is empty. Add a departure airport or waypoint above.", text_muted);
    } else if (visible_count < editor.legs().size()) {
        draw_text(left + 12, bottom + 36,
                  "Showing " + std::to_string(start + 1) + "-" +
                      std::to_string(start + visible_count) + " of " +
                      std::to_string(editor.legs().size()),
                  text_muted);
    }
}

void draw_flight_plan_page(const FlightPlanSnapshot& flight_plan,
                           const FlightPlanEditor& editor,
                           std::string_view route_file_message,
                           bool expanded,
                           int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right);
    if (editor.active()) {
        draw_flight_plan_editor(editor, left, top, right, bottom);
        return;
    }
    static_cast<void>(expanded);
    const auto display_legs = complete_flight_plan_legs(flight_plan);
    draw_text(left, top, "Flight Plan", text_primary, xplmFont_Proportional);
    const int toolbar_left = std::max(left + 112, card_right - 306);
    const int toolbar_width = std::max(60, (card_right - toolbar_left - 12) / 3);
    draw_button(toolbar_left, top + 8, toolbar_left + toolbar_width, top - 23, "Import");
    draw_button(toolbar_left + toolbar_width + 6, top + 8,
                toolbar_left + toolbar_width * 2 + 6, top - 23, "Export");
    draw_button(toolbar_left + toolbar_width * 2 + 12, top + 8,
                card_right, top - 23, "Edit", true);

    if (!route_file_message.empty()) {
        draw_text(left, top - 28, route_file_message, accent);
    }

    if (!flight_plan.available) {
        if (route_file_message.empty()) draw_text(left, top - 28, "Waiting for X-Plane FMS data...", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Route status", "Flight-plan service is starting");
        return;
    }
    if (display_legs.empty()) {
        if (route_file_message.empty()) draw_text(left, top - 28, "No active route is loaded.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "X-Plane FMS", "Select Edit to create a route from waypoint identifiers");
        return;
    }

    const auto& first = display_legs.front();
    const auto& last = display_legs.back();
    const auto active_leg = std::find_if(display_legs.begin(), display_legs.end(),
                                         [](const auto& leg) { return leg.active; });
    const int active_display = active_leg == display_legs.end() ? 0 :
        static_cast<int>(std::distance(display_legs.begin(), active_leg)) + 1;
    char summary[128]{};
    std::snprintf(summary, sizeof(summary), "Departure %s  |  Destination %s  |  %zu legs  |  Active %d",
                  first.identifier.c_str(), last.identifier.c_str(), display_legs.size(),
                  active_display);
    draw_text(left, top - (route_file_message.empty() ? 28 : 50), summary, text_muted);

    const std::size_t start = 0;
    const std::size_t visible_count = display_legs.size();
    constexpr int row_height = 48;
    constexpr int row_gap = 6;
    int row_top = top - (route_file_message.empty() ? 58 : 78);
    for (std::size_t offset = 0; offset < visible_count; ++offset) {
        const auto& leg = display_legs[start + offset];
        draw_rectangle(left, row_top, card_right, row_top - row_height,
                       leg.active ? active_navigation : card);
        std::string label = (leg.active ? ">  " : "   ") + std::to_string(leg.index + 1) + "  " + leg.identifier;
        if (label.size() > 22) {
            label.resize(22);
        }
        draw_text(left + 14, row_top - 29, label, leg.active ? accent : text_primary);
        draw_text(left + 190, row_top - 29, format_leg_detail(leg), text_muted);
        row_top -= row_height + row_gap;
    }

}

std::pair<std::string, std::string> briefing_airports(
    const FlightPlanSnapshot& flight_plan);
std::vector<std::string> wrap_text(std::string_view value,
                                   std::size_t maximum_characters);

const ApproachProcedure* selected_approach(const AirportInfoSnapshot& airport,
                                           std::string_view identifier) {
    const auto found = std::find_if(
        airport.procedures.approach_details.begin(),
        airport.procedures.approach_details.end(), [&](const auto& procedure) {
            return procedure.identifier == identifier;
        });
    return found == airport.procedures.approach_details.end() ? nullptr : &*found;
}

std::size_t approach_visible_count(int list_top, int list_bottom) {
    return static_cast<std::size_t>(std::max(2, (list_top - list_bottom) / 42));
}

struct ApproachFixPreview {
    std::string key;
    std::string label;
};

std::vector<ApproachFixPreview> approach_fix_names(const ApproachProcedure& approach,
                                                    int selected_transition) {
    std::vector<ApproachFixPreview> fixes;
    const auto append = [&](const ApproachLeg& leg, std::string_view group) {
        if (leg.identifier.empty()) return;
        const std::string key = leg.identifier + "#" + std::to_string(leg.sequence);
        std::string label(group);
        label += " " + std::to_string(leg.sequence) + "  " + leg.identifier;
        if (!leg.path_terminator.empty()) label += "  " + leg.path_terminator;
        if (leg.altitude_feet > 0) label += "  " + std::to_string(leg.altitude_feet) + " FT";
        fixes.push_back({key, std::move(label)});
    };
    if (selected_transition > 0 &&
        selected_transition <= static_cast<int>(approach.transitions.size())) {
        for (const auto& leg : approach.transitions[
                 static_cast<std::size_t>(selected_transition - 1)].legs)
            append(leg, "T");
    }
    for (const auto& leg : approach.final_legs) append(leg, "F");
    return fixes;
}

void draw_approach_panel(const AirportInfoSnapshot& airport,
                         const FlightPlanSnapshot& flight_plan,
                         bool destination_endpoint,
                         std::string_view selected_identifier,
                         int selected_transition,
                         bool transition_picker_open,
                         const std::vector<std::string>& excluded_fixes,
                         int scroll,
                         int fix_scroll,
                         std::string_view message,
                         int left, int top, int right, int bottom) {
    const int panel_right = responsive_card_right(left, right, 420);
    const int panel_bottom = bottom + 22;
    draw_rectangle(left - 5, top + 5, panel_right + 5, panel_bottom - 5, status_bar);
    draw_rectangle(left, top, panel_right, panel_bottom, Color{0.075F, 0.105F, 0.130F, 1.0F});
    draw_border(left, top, panel_right, panel_bottom, accent, 2.0F);
    draw_tactical_corners(left, top, panel_right, panel_bottom, connected, 13);
    draw_text(left + 16, top - 25, "APPROACH CONTROL", text_primary, xplmFont_Proportional);
    draw_text(left + 176, top - 25, "Installed X-Plane navigation procedures", text_muted);

    const auto [departure, destination] = briefing_airports(flight_plan);
    const int endpoint_width = std::max(100, (panel_right - left - 38) / 2);
    draw_button(left + 14, top - 42, left + 14 + endpoint_width, top - 76,
                departure.empty() ? "DEP --" : "DEP  " + departure,
                !destination_endpoint);
    draw_button(left + 24 + endpoint_width, top - 42, panel_right - 14, top - 76,
                destination.empty() ? "DEST --" : "DEST  " + destination,
                destination_endpoint);

    std::string status;
    if (airport.state == AirportLookupState::loading) status = "Loading installed CIFP procedures...";
    else if (airport.state != AirportLookupState::ready) status = airport.message;
    else status = airport.identifier + "  " + airport.name + "  |  " +
                  std::to_string(airport.procedures.approach_count) + " approaches";
    draw_text(left + 16, top - 99, shortened(status, 78),
              airport.state == AirportLookupState::ready ? accent : text_muted);

    const int actions_top = panel_bottom + 52;
    const int actions_bottom = panel_bottom + 18;
    const int list_top = top - 113;
    const int list_bottom = actions_top + 12;
    const bool compact = panel_right - left < 520;
    const int list_right = left + (panel_right - left) * 45 / 100;
    const auto& approaches = airport.procedures.approach_details;
    const auto* selected = selected_approach(airport, selected_identifier);
    const std::size_t visible = std::min(approach_visible_count(list_top, list_bottom),
                                         approaches.size());
    const std::size_t maximum_start = approaches.size() > visible ? approaches.size() - visible : 0;
    const std::size_t start = std::min<std::size_t>(std::max(0, scroll), maximum_start);
    int row_top = list_top;
    if (transition_picker_open && selected) {
        draw_text(left + 20, row_top - 22, "SELECT TRANSITION", accent);
        row_top -= 34;
        draw_button(left + 14, row_top, list_right, row_top - 34,
                    "VECTORS / COMMON", selected_transition == 0);
        row_top -= 40;
        const std::size_t transition_visible = static_cast<std::size_t>(
            std::max(1, (row_top - list_bottom) / 40));
        const std::size_t transition_start = std::min<std::size_t>(
            std::max(0, scroll), selected->transitions.size() > transition_visible
                ? selected->transitions.size() - transition_visible : 0);
        for (std::size_t offset = 0; offset < transition_visible &&
             transition_start + offset < selected->transitions.size(); ++offset) {
            const std::size_t index = transition_start + offset;
            draw_button(left + 14, row_top, list_right, row_top - 34,
                        selected->transitions[index].identifier,
                        selected_transition == static_cast<int>(index + 1));
            row_top -= 40;
        }
    } else {
        for (std::size_t offset = 0; offset < visible; ++offset) {
            const auto& approach = approaches[start + offset];
            const bool is_selected = approach.identifier == selected_identifier;
            draw_rectangle(left + 14, row_top, list_right, row_top - 36,
                           is_selected ? active_navigation : card);
            if (is_selected) draw_rectangle(left + 14, row_top, left + 18, row_top - 36, connected);
            draw_text(left + 25, row_top - 23, shortened(approach.display_name, compact ? 34 : 26),
                      is_selected ? text_primary : text_muted);
            row_top -= 42;
        }
    }
    if (approaches.empty() && airport.state == AirportLookupState::ready)
        draw_text(left + 24, list_top - 27, "No approaches are installed for this airport.", text_muted);

    {
        const int detail_left = list_right + 14;
        draw_rectangle(detail_left, list_top, panel_right - 14, list_bottom, card);
        if (selected) {
            draw_text(detail_left + 14, list_top - 26, selected->display_name,
                      accent, xplmFont_Proportional);
            draw_text(detail_left + 14, list_top - 53,
                      "Procedure " + selected->identifier + "  |  Runway " +
                          (selected->runway.empty() ? "CIRCLING" : selected->runway), text_primary);
            const std::string transition = selected_transition > 0 &&
                    selected_transition <= static_cast<int>(selected->transitions.size())
                ? selected->transitions[static_cast<std::size_t>(selected_transition - 1)].identifier
                : "VECTORS / COMMON";
            draw_text(detail_left + 14, list_top - 80, "Transition  " + transition, connected);
            const auto fixes = approach_fix_names(*selected, selected_transition);
            int line_y = list_top - 109;
            draw_text(detail_left + 14, line_y, "Click a fix to include / remove", text_muted);
            line_y -= 25;
            const std::size_t fix_visible = static_cast<std::size_t>(std::max(
                1, (line_y - (list_bottom + 38)) / 30 + 1));
            const std::size_t fix_start = std::min<std::size_t>(
                std::max(0, fix_scroll), fixes.size() > fix_visible
                    ? fixes.size() - fix_visible : 0);
            for (std::size_t offset = 0; offset < fix_visible &&
                 fix_start + offset < fixes.size(); ++offset) {
                const auto& fix = fixes[fix_start + offset];
                if (line_y - 24 < list_bottom + 38) break;
                const bool removed = std::find(excluded_fixes.begin(), excluded_fixes.end(), fix.key) !=
                                     excluded_fixes.end();
                draw_rectangle(detail_left + 12, line_y + 8, panel_right - 26, line_y - 18,
                               removed ? Color{0.28F, 0.10F, 0.10F, 1.0F} : active_navigation);
                draw_text(detail_left + 22, line_y - 9,
                          shortened((removed ? "REMOVED  " : "INCLUDE  ") + fix.label,
                                    compact ? 29 : 42),
                          removed ? Color{1.0F, 0.52F, 0.42F, 1.0F} : text_primary);
                line_y -= 30;
            }
            if (fixes.size() > fix_visible)
                draw_text(detail_left + 14, list_bottom + 42,
                          "Wheel for all " + std::to_string(fixes.size()) + " procedure legs",
                          accent);
            draw_text(detail_left + 14, list_bottom + 20,
                      "Preview first. Apply writes only after confirmation.", text_muted);
        } else {
            draw_text(detail_left + 14, list_top - 30,
                      compact ? "Select an approach." :
                                "Select an approach to preview its route and plate.", text_muted);
        }
    }

    const int available_width = panel_right - left - 38;
    const int button_width = std::max(72, (available_width - 12) / 3);
    draw_button(left + 14, actions_top, left + 14 + button_width, actions_bottom, "Close");
    draw_button(left + 20 + button_width, actions_top,
                left + 20 + button_width * 2, actions_bottom, "Plate", selected != nullptr);
    draw_button(left + 26 + button_width * 2, actions_top,
                panel_right - 14, actions_bottom, "Apply", selected != nullptr);
    if (!message.empty())
        draw_text(left + 16, panel_bottom + 8, shortened(message, 78), connected);
}

std::vector<std::string> wrap_text(std::string_view value, std::size_t maximum_characters) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t position = 0;
    while (position < value.size()) {
        while (position < value.size() && value[position] == ' ') {
            ++position;
        }
        const std::size_t end = value.find(' ', position);
        const std::string word(value.substr(position, end - position));
        if (!line.empty() && line.size() + word.size() + 1 > maximum_characters) {
            lines.push_back(std::move(line));
            line.clear();
        }
        if (!line.empty()) {
            line += ' ';
        }
        line += word;
        position = end == std::string_view::npos ? value.size() : end + 1;
    }
    if (!line.empty()) {
        lines.push_back(std::move(line));
    }
    return lines;
}

void draw_weather_card(int left, int top, int right, std::string_view role,
                       const AirportWeather& weather, bool forecast) {
    constexpr int card_height = 132;
    draw_rectangle(left, top, right, top - card_height, card);
    const std::string title = weather.airport_id.empty()
                                  ? std::string(role)
                                  : std::string(role) + "  /  " + weather.airport_id;
    draw_text(left + 18, top - 29, title, text_primary);
    const auto source_value = forecast ? weather.forecast_source : weather.source;
    const char* source = source_value == WeatherSource::online ? "ONLINE"
        : source_value == WeatherSource::simulator ? "X-PLANE"
        : source_value == WeatherSource::cache ? "SAVED CACHE" : "NO SOURCE";
    draw_text(right - 108, top - 29, source,
              source_value == WeatherSource::online ? connected : text_muted);

    if (weather.airport_id.empty()) {
        draw_text(left + 18, top - 60, "Airport not found in the active route", text_muted);
        return;
    }
    const auto& report = forecast ? weather.taf : weather.metar;
    if (report.empty()) {
        draw_text(left + 18, top - 60,
                  forecast ? "No TAF forecast is available from the internet or cache"
                           : "No METAR is available from the internet, X-Plane, or cache",
                  text_muted);
        draw_text(left + 18, top - 84,
                  forecast ? "Forecast coverage depends on the selected airport"
                           : "Check the route and network connection", text_muted);
        return;
    }

    const int available_width = std::max(252, right - left - 36);
    const auto lines = wrap_text(report, static_cast<std::size_t>(available_width / 7));
    const std::size_t visible_lines = std::min<std::size_t>(3, lines.size());
    for (std::size_t index = 0; index < visible_lines; ++index) {
        draw_text(left + 18, top - 60 - static_cast<int>(index) * 23, lines[index], text_muted);
    }
}

void draw_weather_page(const WeatherSnapshot& weather, bool forecast,
                       int left, int top, int right) {
    const int card_right = responsive_card_right(left, right);
    draw_text(left, top, "Route Weather", text_primary, xplmFont_Proportional);
    draw_text(left, top - 28,
              forecast ? "Published TAF forecast - internet first, then saved cache"
                       : "Current METAR - internet first, then X-Plane and saved cache",
              text_muted);
    draw_button(card_right - 190, top + 8, card_right - 98, top - 23,
                "CURRENT", !forecast);
    draw_button(card_right - 92, top + 8, card_right, top - 23,
                "FORECAST", forecast);
    if (!weather.available) {
        draw_card(left, top - 62, card_right, top - 158,
                  "Weather status", "Weather service is starting");
        return;
    }
    draw_weather_card(left, top - 62, card_right, "Departure", weather.departure, forecast);
    draw_weather_card(left, top - 212, card_right, "Destination", weather.destination, forecast);
    if (!weather.online_status.empty())
        draw_text(left, top - 365, shortened(weather.online_status, 78), text_muted);
}

std::string format_ete(const RouteProgressPoint& progress) {
    if (!progress.ete_available) {
        return "ETE unavailable below 1 kt groundspeed";
    }
    const int total_minutes = std::max(0, static_cast<int>(std::lround(progress.ete_minutes)));
    if (total_minutes >= 60) {
        return "ETE " + std::to_string(total_minutes / 60) + " hr " +
               std::to_string(total_minutes % 60) + " min";
    }
    return "ETE " + std::to_string(total_minutes) + " min";
}

void draw_progress_card(int left, int top, int right, std::string_view role,
                        const RouteProgressPoint& progress) {
    draw_rectangle(left, top, right, top - 112, card);
    draw_text(left + 18, top - 29, role, text_primary);
    if (!progress.available) {
        draw_text(left + 18, top - 61, "No route point is currently available", text_muted);
        return;
    }

    char navigation[112]{};
    std::snprintf(navigation, sizeof(navigation), "%s  |  %.1f NM  |  %03d deg true",
                  progress.identifier.c_str(), progress.distance_nm,
                  static_cast<int>(std::lround(progress.bearing_degrees)) % 360);
    draw_text(left + 18, top - 58, navigation, accent);
    draw_text(left + 18, top - 84, format_ete(progress), text_muted);
}

void draw_progress_page(const RouteProgressSnapshot& progress, int left, int top, int right) {
    const int card_right = responsive_card_right(left, right);
    draw_text(left, top, "Route Progress", text_primary, xplmFont_Proportional);
    draw_text(left, top - 28, "Live great-circle estimates from aircraft position and groundspeed", text_muted);
    if (!progress.available) {
        draw_card(left, top - 62, card_right, top - 158,
                  "Progress status", "Load a route and wait for live aircraft data");
        return;
    }
    draw_progress_card(left, top - 62, card_right, "Active waypoint", progress.active_waypoint);
    draw_progress_card(left, top - 192, card_right, "Destination", progress.destination);
    draw_card(left, top - 322, card_right, top - 406,
              "Estimate basis", "Direct distance and current groundspeed");
}

std::string format_weight(double kilograms) {
    char value[64]{};
    std::snprintf(value, sizeof(value), "%.0f lb", kilograms * 2.2046226218);
    return value;
}

void draw_planning_page(const PlanningSnapshot& planning, int left, int top, int right) {
    const int card_right = responsive_card_right(left, right);
    draw_text(left, top, "Aircraft Planning", text_primary, xplmFont_Proportional);
    draw_text(left, top - 28, "Live weight, balance, fuel reserve, and landing outlook", text_muted);
    if (!planning.available) {
        draw_card(left, top - 62, card_right, top - 158,
                  "Planning status", "Waiting for active-aircraft weight data");
        return;
    }
    const int gap = 12;
    const int middle = (left + card_right) / 2;
    const int left_right = middle - gap / 2;
    const int right_left = middle + gap / 2;
    const Color load_color = planning.overweight ? Color{1.0F, 0.38F, 0.30F, 1.0F} : connected;
    char gross[112]{};
    std::snprintf(gross, sizeof(gross), "%.0f / %.0f lb  |  %.0f%%  |  %s",
                  planning.loading.gross_weight_kg * 2.2046226218,
                  planning.loading.maximum_gross_weight_kg * 2.2046226218,
                  planning.loading_percent, planning.overweight ? "OVER" : "OK");
    draw_rectangle(left, top - 62, left_right, top - 148, card);
    draw_text(left + 18, top - 91, "GROSS / MAXIMUM", load_color);
    draw_text(left + 18, top - 121, gross, text_primary);
    char empty_payload[100]{};
    std::snprintf(empty_payload, sizeof(empty_payload), "Empty %.0f lb  |  Payload %.0f lb",
                  planning.loading.empty_weight_kg * 2.2046226218,
                  planning.loading.payload_weight_kg * 2.2046226218);
    draw_card(right_left, top - 62, card_right, top - 148,
              "EMPTY / PAYLOAD", empty_payload);

    std::string fuel_load = format_weight(planning.loading.fuel_weight_kg);
    if (planning.loading.fuel_capacity_kg > 0.0) {
        char capacity[48]{};
        std::snprintf(capacity, sizeof(capacity), "  |  %.0f%% capacity",
                      100.0 * planning.loading.fuel_weight_kg / planning.loading.fuel_capacity_kg);
        fuel_load += capacity;
    }
    char cg[80]{};
    std::snprintf(cg, sizeof(cg), "%.3f m  /  %.1f in from aircraft default",
                  planning.loading.cg_offset_meters, planning.loading.cg_offset_meters * 39.37007874);
    draw_card(left, top - 160, left_right, top - 246, "FUEL LOAD", fuel_load);
    draw_card(right_left, top - 160, card_right, top - 246, "CG OFFSET", cg);

    draw_text(left, top - 278, "FUEL PLAN", accent);
    draw_button(left, top - 293, left + 58, top - 326, "-5");
    char reserve_label[64]{};
    std::snprintf(reserve_label, sizeof(reserve_label), "Reserve %d min", planning.reserve_minutes);
    draw_text(left + 72, top - 315, reserve_label, text_primary);
    draw_button(left + 190, top - 293, left + 248, top - 326, "+5");
    if (!planning.fuel_plan_available) {
        draw_text(left, top - 356, "Enter a route and establish measurable fuel flow for trip planning.", text_muted);
    } else {
        char plan[180]{};
        std::snprintf(plan, sizeof(plan),
                      "Burn %.1f GPH  |  Trip %.0f lb  |  Reserve %.0f lb",
                      planning.fuel_flow_us_gallons_per_hour,
                      planning.trip_fuel_kg * 2.2046226218,
                      planning.reserve_fuel_kg * 2.2046226218);
        draw_text(left, top - 356, plan,
                  planning.fuel_margin_kg >= 0.0 ? text_primary : Color{1.0F, 0.38F, 0.30F, 1.0F});
        char landing[150]{};
        std::snprintf(landing, sizeof(landing), "Landing %.0f lb  |  Fuel margin %.0f lb  |  %s",
                      planning.predicted_landing_weight_kg * 2.2046226218,
                      planning.fuel_margin_kg * 2.2046226218,
                      planning.dispatch_ready ? "PLAN CHECKS PASS" : "REVIEW WEIGHT OR FUEL");
        draw_text(left, top - 384, landing, planning.dispatch_ready ? connected : load_color);
    }
    draw_text(left, top - 420,
              "Planning estimate only - verify aircraft POH/AFM limits and performance tables.", text_muted);
}

struct ScreenPoint {
    bool valid{false};
    double x{};
    double y{};
};

ScreenPoint map_screen_point(const MovingMapModel& map, const MapTileViewport& viewport,
                             double latitude, double longitude) {
    const auto point = project_map_coordinate(map.style(), viewport, latitude, longitude);
    return {point.valid, point.x, point.y};
}

void draw_aircraft_symbol(int center_x, int center_y, double heading_degrees) {
    prepare_solid_geometry();
    const double heading = heading_degrees * 3.14159265358979323846 / 180.0;
    const double forward_x = std::sin(heading);
    const double forward_y = std::cos(heading);
    const double right_x = std::cos(heading);
    const double right_y = -std::sin(heading);
    constexpr std::array<std::pair<double, double>, 16> silhouette{{
        {14.0, 0.0}, {5.0, 1.7}, {1.2, 8.2}, {-1.2, 8.2}, {-0.2, 2.1},
        {-7.0, 2.1}, {-9.0, 4.0}, {-10.2, 4.0}, {-8.4, 0.0}, {-10.2, -4.0},
        {-9.0, -4.0}, {-7.0, -2.1}, {-0.2, -2.1}, {-1.2, -8.2},
        {1.2, -8.2}, {5.0, -1.7}
    }};
    const auto vertex = [&](double forward, double right) {
        glVertex2d(center_x + forward_x * forward + right_x * right,
                   center_y + forward_y * forward + right_y * right);
    };
    glColor4f(0.015F, 0.36F, 0.95F, 1.0F);
    glBegin(GL_POLYGON);
    for (const auto& [forward, right] : silhouette) vertex(forward, right);
    glEnd();
    glColor4f(0.0F, 0.035F, 0.09F, 1.0F);
    glLineWidth(3.6F);
    glBegin(GL_LINE_LOOP);
    for (const auto& [forward, right] : silhouette) vertex(forward, right);
    glEnd();
    glColor4f(0.88F, 0.96F, 1.0F, 1.0F);
    glLineWidth(1.2F);
    glBegin(GL_LINE_LOOP);
    for (const auto& [forward, right] : silhouette) vertex(forward, right);
    glEnd();
    glColor4f(0.35F, 0.86F, 1.0F, 1.0F);
    glLineWidth(1.0F);
    glBegin(GL_LINES);
    vertex(11.0, 0.0);
    vertex(-6.5, 0.0);
    glEnd();
    glLineWidth(1.0F);
}

std::string map_progress_text(const RouteProgressSnapshot& progress) {
    if (!progress.destination.available) {
        return "NO ACTIVE DESTINATION";
    }
    char value[128]{};
    if (progress.destination.ete_available) {
        std::snprintf(value, sizeof(value), "DEST %s  |  %.1f NM  |  %s",
                      progress.destination.identifier.c_str(), progress.destination.distance_nm,
                      format_ete(progress.destination).c_str());
    } else {
        std::snprintf(value, sizeof(value), "DEST %s  |  %.1f NM",
                      progress.destination.identifier.c_str(), progress.destination.distance_nm);
    }
    return value;
}

struct HomeMapGeometry {
    int left{};
    int top{};
    int right{};
    int bottom{};
    int tile_top{};
};

HomeMapGeometry home_map_geometry(int left, int top, int right, int bottom) {
    HomeMapGeometry geometry;
    geometry.left = left;
    geometry.top = top + 10;
    geometry.right = responsive_card_right(left, right, 360);
    geometry.bottom = bottom + 12;
    geometry.tile_top = geometry.top - 38;
    return geometry;
}

void draw_map_style_button(int left, int top, int right, std::string_view label, bool selected) {
    const int bottom = top - 25;
    const bool hovered = hover_cursor_active &&
                         point_in_rectangle(hover_cursor_x, hover_cursor_y,
                                            left, top, right, bottom);
    const Color idle_fill{0.175F, 0.205F, 0.225F, 1.0F};
    const Color selected_fill = active_navigation;
    const Color hover_fill{0.10F, 0.52F, 0.18F, 1.0F};
    draw_rectangle(left, top, right, bottom,
                   hovered ? hover_fill : (selected ? selected_fill : idle_fill));
    draw_rectangle(left, top, right, top - 3,
                   hovered ? hover_accent : (selected ? connected : map_grid));
    draw_shadowed_text(left + 10, top - 17, label, text_primary, xplmFont_Proportional);
}

constexpr int filter_button_width = 58;

int filter_button_left(const HomeMapGeometry& panel) {
    const int topo_left = panel.right - 10 - 64;
    const int street_left = topo_left - 6 - 70;
    return street_left - 6 - filter_button_width;
}

Color airspace_color(std::string_view class_code) {
    if (class_code == "B" || class_code == "C") return {0.30F, 0.65F, 1.0F, 0.92F};
    if (class_code == "D") return {0.70F, 0.45F, 1.0F, 0.92F};
    if (class_code == "P" || class_code == "R" || class_code == "Q")
        return {1.0F, 0.36F, 0.24F, 0.95F};
    return {1.0F, 0.72F, 0.25F, 0.85F};
}

void draw_airspace_overlay(const AirspaceSnapshot& airspace, const MovingMapModel& map,
                           const MapTileViewport& viewport,
                           int left, int top, int right, int bottom) {
    if (airspace.state != AirspaceLoadState::ready || !airspace.zones) return;
    std::size_t rendered_zones = 0;
    std::size_t rendered_segments = 0;
    for (const auto& zone : *airspace.zones) {
        if (rendered_zones >= 120 || rendered_segments >= 1800 || zone.boundary.size() < 2) break;
        bool visible = false;
        ScreenPoint label_point;
        for (std::size_t index = 1; index <= zone.boundary.size(); ++index) {
            const auto& first = zone.boundary[index - 1];
            const auto& second = zone.boundary[index % zone.boundary.size()];
            auto point_1 = map_screen_point(map, viewport, first.latitude_degrees,
                                            first.longitude_degrees);
            auto point_2 = map_screen_point(map, viewport, second.latitude_degrees,
                                            second.longitude_degrees);
            if (!point_1.valid || !point_2.valid) continue;
            double x_1 = point_1.x, y_1 = point_1.y, x_2 = point_2.x, y_2 = point_2.y;
            if (clip_line(x_1, y_1, x_2, y_2, left, top, right, bottom)) {
                draw_line(x_1, y_1, x_2, y_2, airspace_color(zone.class_code), 1.5F);
                if (!visible) label_point = {true, x_1, y_1};
                visible = true;
                ++rendered_segments;
            }
            if (rendered_segments >= 1800) break;
        }
        if (visible) {
            if (rendered_zones < 20 && label_point.x < right - 130 && label_point.y < top - 15) {
                std::string label = zone.class_code;
                if (!zone.name.empty()) label += "  " + zone.name.substr(0, 22);
                draw_map_label(static_cast<int>(label_point.x) + 3,
                               static_cast<int>(label_point.y) - 3,
                               label, airspace_color(zone.class_code));
            }
            ++rendered_zones;
        }
    }
}

void draw_circle_marker(int x, int y, Color color) {
    draw_border(x - 6, y + 6, x + 6, y - 6, color, 1.5F);
    draw_tactical_corners(x - 7, y + 7, x + 7, y - 7, color, 4);
    draw_rectangle(x - 2, y + 2, x + 2, y - 2, color);
}

void draw_diamond_marker(int x, int y, Color color) {
    draw_line(x, y + 6, x + 6, y, color, 2.0F);
    draw_line(x + 6, y, x, y - 6, color, 2.0F);
    draw_line(x, y - 6, x - 6, y, color, 2.0F);
    draw_line(x - 6, y, x, y + 6, color, 2.0F);
}

enum class MarkerIconKind : std::size_t {
    airport,
    vor,
    ndb,
    fix,
    food,
    golf,
    landmark,
    traffic,
    count,
};

MarkerIconKind marker_icon_for(WaypointKind kind) {
    switch (kind) {
    case WaypointKind::airport: return MarkerIconKind::airport;
    case WaypointKind::vor: return MarkerIconKind::vor;
    case WaypointKind::ndb: return MarkerIconKind::ndb;
    case WaypointKind::fix: return MarkerIconKind::fix;
    default: return MarkerIconKind::fix;
    }
}

MarkerIconKind marker_icon_for(PoiCategory category) {
    switch (category) {
    case PoiCategory::food: return MarkerIconKind::food;
    case PoiCategory::golf: return MarkerIconKind::golf;
    case PoiCategory::attraction: return MarkerIconKind::landmark;
    }
    return MarkerIconKind::landmark;
}

class MarkerIconAtlas final {
public:
    bool draw(MarkerIconKind kind, int x, int y, int scale_percent) {
        ensure_loaded();
        const auto index = static_cast<std::size_t>(kind);
        if (!images_[index]) return false;
        const int size = std::clamp(20 * scale_percent / 100, 14, 32);
        return images_[index]->draw(x - size / 2, y - size / 2,
                                    x + size / 2, y + size / 2,
                                    0.0, 1.0, 1.0, 0.0, true);
    }

private:
    static constexpr int icon_size = 24;

    void ensure_loaded() {
        if (loaded_) return;
        const std::array<std::array<std::uint8_t, 3>, static_cast<std::size_t>(MarkerIconKind::count)> colors{{
            {35, 225, 255}, {70, 235, 190}, {255, 206, 55}, {226, 95, 255},
            {255, 145, 48}, {80, 225, 110}, {196, 95, 255}, {255, 205, 45},
        }};
        for (std::size_t index = 0; index < images_.size(); ++index) {
            auto pixels = make_icon(static_cast<MarkerIconKind>(index), colors[index]);
            auto image = std::make_unique<XPlaneGpuImage>();
            if (image->upload(icon_size, icon_size, pixels, GpuPixelFormat::bgra)) {
                images_[index] = std::move(image);
            }
        }
        loaded_ = true;
    }

    static std::vector<std::uint8_t> make_icon(
        MarkerIconKind kind, const std::array<std::uint8_t, 3>& rgb) {
        std::vector<std::uint8_t> pixels(icon_size * icon_size * 4, 0);
        const auto set = [&](int x, int y, std::array<std::uint8_t, 3> color,
                             std::uint8_t alpha = 255) {
            if (x < 0 || x >= icon_size || y < 0 || y >= icon_size) return;
            const auto offset = static_cast<std::size_t>((y * icon_size + x) * 4);
            pixels[offset] = color[2];
            pixels[offset + 1] = color[1];
            pixels[offset + 2] = color[0];
            pixels[offset + 3] = alpha;
        };
        const std::array<std::uint8_t, 3> white{255, 255, 255};
        for (int y = 1; y < icon_size - 1; ++y) {
            for (int x = 1; x < icon_size - 1; ++x) {
                const int dx = x - 11;
                const int dy = y - 11;
                if (dx * dx + dy * dy <= 100) set(x, y, rgb, 235);
            }
        }
        for (int angle = 0; angle < 360; angle += 5) {
            const double radians = angle * 3.14159265358979323846 / 180.0;
            set(11 + static_cast<int>(std::lround(std::cos(radians) * 10.0)),
                11 + static_cast<int>(std::lround(std::sin(radians) * 10.0)), white);
        }
        if (kind == MarkerIconKind::airport) {
            for (int value = 5; value <= 17; ++value) {
                set(value, 10, white); set(value, 11, white); set(10, value, white); set(11, value, white);
            }
        } else if (kind == MarkerIconKind::vor) {
            for (int value = 5; value <= 17; ++value) {
                set(value, 11, white); set(11, value, white);
                set(value, 5 + std::abs(value - 11), white);
            }
        } else if (kind == MarkerIconKind::ndb) {
            for (int angle = 0; angle < 360; angle += 12) {
                const double radians = angle * 3.14159265358979323846 / 180.0;
                set(11 + static_cast<int>(std::lround(std::cos(radians) * 5.0)),
                    11 + static_cast<int>(std::lround(std::sin(radians) * 5.0)), white);
            }
            set(11, 11, white);
        } else if (kind == MarkerIconKind::fix) {
            for (int row = 0; row < 9; ++row) {
                for (int x = 11 - row / 2; x <= 11 + row / 2; ++x) set(x, 7 + row, white);
            }
        } else if (kind == MarkerIconKind::food) {
            for (int y = 6; y <= 17; ++y) { set(8, y, white); set(14, y, white); }
            set(6, 6, white); set(7, 6, white); set(9, 6, white); set(10, 6, white);
        } else if (kind == MarkerIconKind::golf) {
            for (int y = 5; y <= 18; ++y) set(8, y, white);
            for (int y = 5; y <= 10; ++y) {
                for (int x = 9; x <= 15 - (y - 5); ++x) set(x, y, white);
            }
        } else if (kind == MarkerIconKind::traffic) {
            for (int y = 4; y <= 18; ++y) { set(11, y, white); set(12, y, white); }
            for (int x = 5; x <= 18; ++x) { set(x, 10, white); set(x, 11, white); }
            for (int x = 8; x <= 15; ++x) set(x, 17, white);
            set(10, 4, white); set(13, 4, white);
        } else {
            for (int value = 0; value <= 7; ++value) {
                set(11 - value, 11, white); set(11 + value, 11, white);
                set(11, 11 - value, white); set(11, 11 + value, white);
            }
        }
        return pixels;
    }

    std::array<std::unique_ptr<XPlaneGpuImage>,
               static_cast<std::size_t>(MarkerIconKind::count)> images_{};
    bool loaded_{false};
};

bool draw_marker_icon(int x, int y, MarkerIconKind kind, int scale_percent) {
    static MarkerIconAtlas atlas;
    return atlas.draw(kind, x, y, scale_percent);
}

Color poi_color(PoiCategory category) {
    switch (category) {
    case PoiCategory::food: return {1.0F, 0.58F, 0.18F, 1.0F};
    case PoiCategory::golf: return {0.36F, 0.92F, 0.46F, 1.0F};
    case PoiCategory::attraction: return {0.88F, 0.48F, 1.0F, 1.0F};
    }
    return accent;
}

void draw_poi_marker(int x, int y, PoiCategory category, int scale_percent) {
    if (!draw_marker_icon(x, y, marker_icon_for(category), scale_percent)) {
        draw_diamond_marker(x, y, poi_color(category));
    }
}

std::string map_range_label(double range_nm) {
    char value[40]{};
    if (range_nm < 0.1) {
        std::snprintf(value, sizeof(value), "RANGE %.0f FT", range_nm * 6076.12);
    } else if (range_nm < 1.0) {
        std::snprintf(value, sizeof(value), "RANGE %.2f NM", range_nm);
    } else {
        std::snprintf(value, sizeof(value), "RANGE %.0f NM", range_nm);
    }
    return value;
}

struct MapFilterGeometry {
    int left{};
    int top{};
    int right{};
    int bottom{};
    int first_row_top{};
    int visible_slots{};
    int max_scroll{};
};

constexpr int map_filter_content_slots = 13;

MapFilterGeometry map_filter_geometry(int map_left, int map_top, int map_right, int map_bottom) {
    constexpr int step = 25;
    const int right = map_right - 58;
    const int left = std::max(map_left + 10, right - 214);
    const int top = map_top - 10;
    const int bottom = std::max(map_bottom + 8, map_top - 331);
    const int first_row_top = map_top - 48;
    const int visible_slots = std::max(
        1, std::min(map_filter_content_slots, (first_row_top - bottom + 8) / step));
    return {left, top, right, bottom, first_row_top, visible_slots,
            std::max(0, map_filter_content_slots - visible_slots)};
}

void draw_filter_row(int left, int top, int right, std::string_view label, bool enabled) {
    constexpr int height = 22;
    draw_rectangle(left, top, right, top - height,
                   enabled ? Color{0.020F, 0.330F, 0.075F, 1.0F}
                           : Color{0.070F, 0.080F, 0.085F, 1.0F});
    draw_border(left, top, right, top - height,
                enabled ? connected : Color{0.34F, 0.38F, 0.40F, 1.0F}, 1.0F);
    draw_rectangle(left + 7, top - 7, left + 15, top - 15,
                   enabled ? connected : Color{0.18F, 0.26F, 0.30F, 1.0F});
    draw_shadowed_text(left + 24, top - 16, label, text_primary);
}

void draw_home_map(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                   const RouteProgressSnapshot& progress,
                   const WeatherSnapshot& weather, const AirspaceSnapshot& airspace,
                   const NavigationDatabaseSnapshot& navigation_database,
                   const TrafficSnapshot& traffic,
                   const MovingMapModel& map, XPlaneMapTiles& tiles, XPlaneMapPois& pois,
                   std::vector<MapHitTarget>& hit_targets,
                   std::vector<PoiHitTarget>& poi_hit_targets,
                   std::vector<TrafficHitTarget>& traffic_hit_targets,
                   const std::optional<MapPoi>& hovered_poi,
                   const std::optional<MapHitTarget>& hovered_target,
                   const std::optional<std::string>& selected_traffic_key,
                   bool filters_open, int filter_scroll,
                   bool show_aircraft, bool show_aircraft_info,
                   bool show_route, bool show_labels, int marker_scale,
                   int route_line_width,
                   int left, int top, int right, int bottom) {
    hit_targets.clear();
    poi_hit_targets.clear();
    traffic_hit_targets.clear();
    const auto panel = home_map_geometry(left, top, right, bottom);
    const int map_left = panel.left + 3;
    const int map_right = panel.right - 3;
    const int map_top = panel.tile_top;
    const int map_bottom = panel.bottom + 3;
    draw_rectangle(panel.left, panel.top, panel.right, panel.bottom, card);
    draw_rectangle(panel.left, panel.top, panel.right, panel.top - 4, accent);

    const int topo_right = panel.right - 10;
    const int topo_left = topo_right - 64;
    const int street_right = topo_left - 6;
    const int street_left = street_right - 70;
    draw_map_style_button(street_left, panel.top - 7, street_right, "Street",
                          map.style() == MapStyle::street);
    draw_map_style_button(topo_left, panel.top - 7, topo_right, "Topo",
                          map.style() == MapStyle::topographic);

    const int filter_left = filter_button_left(panel);
    draw_map_style_button(filter_left, panel.top - 7, filter_left + filter_button_width,
                          "FILTER", filters_open);

    draw_text(panel.left + 14, panel.top - 23, map_range_label(map.range_nm()), text_muted);
    draw_rectangle(map_left, map_top, map_right, map_bottom,
                   Color{1.0F, 1.0F, 1.0F, 1.0F});

    if (!telemetry.available) {
        draw_text(map_left + 22, map_top - 34, "Waiting for live aircraft position...", text_muted);
        return;
    }

    const MapTileViewport viewport{
        map_left, map_top, map_right, map_bottom,
        map.center_latitude_degrees(), map.center_longitude_degrees(), map.range_nm()};
    tiles.draw(map.style(), viewport);
    if (map.poi_enabled()) {
        pois.update(map.center_latitude_degrees(), map.center_longitude_degrees(), map.range_nm());
    }
    if (map.layer_enabled(MapLayer::airspace)) {
        draw_airspace_overlay(airspace, map, viewport,
                              map_left, map_top, map_right, map_bottom);
    }

    std::vector<std::pair<int, int>> occupied_label_positions;
    std::size_t rendered_map_labels{};
    if (navigation_database.points) {
        std::size_t rendered = 0;
        const double latitude_margin = map.range_nm() / 60.0 * 1.5;
        const double longitude_margin = latitude_margin /
            std::max(0.15, std::cos(map.center_latitude_degrees() *
                                    3.14159265358979323846 / 180.0));
        for (const auto& nav : *navigation_database.points) {
            const bool airport = nav.kind == WaypointKind::airport;
            if ((airport && !map.layer_enabled(MapLayer::airports)) ||
                (!airport && !map.layer_enabled(MapLayer::navaids))) continue;
            if (std::abs(nav.latitude_degrees - map.center_latitude_degrees()) > latitude_margin ||
                std::abs(std::remainder(nav.longitude_degrees - map.center_longitude_degrees(),
                                        360.0)) > longitude_margin) continue;
            const auto point = map_screen_point(map, viewport, nav.latitude_degrees,
                                                nav.longitude_degrees);
            if (!point.valid || point.x < map_left + 10 || point.x > map_right - 10 ||
                point.y < map_bottom + 60 || point.y > map_top - 10) continue;
            const int marker_x = static_cast<int>(point.x);
            const int marker_y = static_cast<int>(point.y);
            if (airport) {
                if (!draw_marker_icon(marker_x, marker_y, MarkerIconKind::airport, marker_scale)) {
                    draw_circle_marker(marker_x, marker_y, connected);
                }
                FlightPlanLeg leg;
                leg.identifier = nav.identifier;
                leg.kind = WaypointKind::airport;
                leg.latitude_degrees = nav.latitude_degrees;
                leg.longitude_degrees = nav.longitude_degrees;
                hit_targets.push_back({marker_x, marker_y, std::move(leg), nav.name});
                const std::size_t label_limit = map.range_nm() <= 10.0 ? 36 : 16;
                const bool label_crowded = std::any_of(
                    occupied_label_positions.begin(), occupied_label_positions.end(),
                    [marker_x, marker_y](const auto& placed) {
                        return std::abs(marker_x - placed.first) < 76 &&
                               std::abs(marker_y - placed.second) < 22;
                    });
                if (show_labels && map.range_nm() <= 40.0 &&
                    rendered_map_labels < label_limit && !label_crowded &&
                    marker_x < map_right - 70) {
                    draw_map_label(marker_x + 9, marker_y + 5, nav.identifier, connected);
                    occupied_label_positions.emplace_back(marker_x, marker_y);
                    ++rendered_map_labels;
                }
            } else {
                if (!draw_marker_icon(marker_x, marker_y, marker_icon_for(nav.kind), marker_scale)) {
                    draw_diamond_marker(marker_x, marker_y, text_muted);
                }
            }
            if (++rendered >= 90) break;
        }
    }

    std::vector<std::pair<int, int>> occupied_poi_positions;
    const auto visible_pois = map.range_nm() <= 40.0 ? pois.snapshot() : std::vector<MapPoi>{};
    for (const auto& poi : visible_pois) {
        if (!map.poi_enabled()) continue;
        const auto point = map_screen_point(map, viewport, poi.latitude_degrees,
                                            poi.longitude_degrees);
        if (!point.valid || point.x < map_left + 10 || point.x > map_right - 10 ||
            point.y < map_bottom + 62 || point.y > map_top - 10) continue;
        const int marker_x = static_cast<int>(point.x);
        const int marker_y = static_cast<int>(point.y);
        const bool crowded = std::any_of(
            occupied_poi_positions.begin(), occupied_poi_positions.end(),
            [marker_x, marker_y](const auto& placed) {
                return std::abs(marker_x - placed.first) < 18 &&
                       std::abs(marker_y - placed.second) < 18;
            });
        if (crowded) continue;
        occupied_poi_positions.emplace_back(marker_x, marker_y);
        draw_poi_marker(marker_x, marker_y, poi.category, marker_scale);
        poi_hit_targets.push_back({marker_x, marker_y, poi});
        if (poi_hit_targets.size() >= 80) break;
    }

    std::vector<ScreenPoint> route_points;
    if (show_route) {
        route_points.reserve(flight_plan.legs.size());
        for (const auto& leg : flight_plan.legs) {
            route_points.push_back(map_screen_point(map, viewport, leg.latitude_degrees,
                                                    leg.longitude_degrees));
        }
    }
    for (std::size_t index = 1; index < route_points.size(); ++index) {
        if (!route_points[index - 1].valid || !route_points[index].valid) {
            continue;
        }
        double x_1 = route_points[index - 1].x;
        double y_1 = route_points[index - 1].y;
        double x_2 = route_points[index].x;
        double y_2 = route_points[index].y;
        if (clip_line(x_1, y_1, x_2, y_2, map_left, map_top, map_right, map_bottom)) {
            const float route_width = static_cast<float>(std::clamp(
                route_line_width, minimum_route_line_width, maximum_route_line_width));
            const float active_bonus = flight_plan.legs[index].active ? 1.5F : 0.0F;
            draw_line(x_1, y_1, x_2, y_2,
                      Color{0.08F, 0.02F, 0.07F, 0.96F},
                      route_width + active_bonus + 3.0F);
            draw_line(x_1, y_1, x_2, y_2, route_line,
                      route_width + active_bonus);
        }
    }

    for (std::size_t index = 0; index < route_points.size(); ++index) {
        const auto& point = route_points[index];
        if (!point.valid || point.x < map_left + 7 || point.x > map_right - 7 ||
            point.y < map_bottom + 7 || point.y > map_top - 7) {
            continue;
        }
        const auto& leg = flight_plan.legs[index];
        std::string route_point_name;
        if (leg.kind == WaypointKind::airport && navigation_database.points) {
            const auto named = std::find_if(
                navigation_database.points->begin(), navigation_database.points->end(),
                [&leg](const auto& nav) {
                    return nav.kind == WaypointKind::airport &&
                           nav.identifier == leg.identifier;
                });
            if (named != navigation_database.points->end()) route_point_name = named->name;
        }
        hit_targets.push_back({static_cast<int>(point.x), static_cast<int>(point.y),
                               leg, std::move(route_point_name)});
        const Color waypoint_color = leg.active ? route_line : text_primary;
        const bool airport_visible = leg.kind == WaypointKind::airport &&
                                     map.layer_enabled(MapLayer::airports);
        const bool navaid_visible = (leg.kind == WaypointKind::vor || leg.kind == WaypointKind::ndb ||
                                     leg.kind == WaypointKind::fix) && map.layer_enabled(MapLayer::navaids);
        if (airport_visible || navaid_visible) {
            if (!draw_marker_icon(static_cast<int>(point.x), static_cast<int>(point.y),
                                  marker_icon_for(leg.kind), marker_scale)) {
                if (airport_visible) {
                    draw_circle_marker(static_cast<int>(point.x), static_cast<int>(point.y), waypoint_color);
                } else {
                    draw_diamond_marker(static_cast<int>(point.x), static_cast<int>(point.y), waypoint_color);
                }
            }
        }
        else draw_rectangle(static_cast<int>(point.x) - 3, static_cast<int>(point.y) + 3,
                            static_cast<int>(point.x) + 3, static_cast<int>(point.y) - 3,
                            waypoint_color);
        const bool label = leg.active || index == 0 || index + 1 == route_points.size();
        const int label_x = static_cast<int>(point.x);
        const int label_y = static_cast<int>(point.y);
        const bool label_crowded = std::any_of(
            occupied_label_positions.begin(), occupied_label_positions.end(),
            [label_x, label_y](const auto& placed) {
                return std::abs(label_x - placed.first) < 76 &&
                       std::abs(label_y - placed.second) < 22;
            });
        if (show_labels && label && !label_crowded &&
            point.x < map_right - 80 && point.y < map_top - 18) {
            draw_map_label(label_x + 9, label_y + 5, leg.identifier, waypoint_color);
            occupied_label_positions.emplace_back(label_x, label_y);
        }
        if (map.layer_enabled(MapLayer::weather) && leg.kind == WaypointKind::airport) {
            const bool endpoint = leg.identifier == weather.departure.airport_id ||
                                  leg.identifier == weather.destination.airport_id;
            if (endpoint) {
                const bool report = (leg.identifier == weather.departure.airport_id && !weather.departure.metar.empty()) ||
                                    (leg.identifier == weather.destination.airport_id && !weather.destination.metar.empty());
                const Color weather_color = report ? connected : Color{1.0F, 0.72F, 0.25F, 1.0F};
                draw_tactical_corners(static_cast<int>(point.x) - 11,
                                      static_cast<int>(point.y) + 11,
                                      static_cast<int>(point.x) + 11,
                                      static_cast<int>(point.y) - 11,
                                      weather_color, 5);
                if (point.x < map_right - 35) {
                    draw_map_label(static_cast<int>(point.x) + 14,
                                   static_cast<int>(point.y) - 7, "WX", weather_color);
                }
            }
        }
    }

    if (map.layer_enabled(MapLayer::traffic) && traffic.available && map.range_nm() <= 160.0) {
        std::size_t rendered_traffic{};
        for (const auto& target : traffic.targets) {
            const auto point = map_screen_point(map, viewport, target.latitude_degrees,
                                                target.longitude_degrees);
            if (!point.valid || point.x < map_left + 18 || point.x > map_right - 18 ||
                point.y < map_bottom + 62 || point.y > map_top - 18) continue;
            const int traffic_x = static_cast<int>(point.x);
            const int traffic_y = static_cast<int>(point.y);
            draw_marker_icon(traffic_x, traffic_y, MarkerIconKind::traffic, marker_scale);
            traffic_hit_targets.push_back(
                {traffic_x, traffic_y, traffic_key(target), target.callsign});
            const double radians = target.track_degrees * 3.14159265358979323846 / 180.0;
            draw_line(traffic_x, traffic_y,
                      traffic_x + std::sin(radians) * 18.0,
                      traffic_y + std::cos(radians) * 18.0,
                      Color{1.0F, 0.82F, 0.18F, 1.0F}, 2.0F);
            const bool label_crowded = std::any_of(
                occupied_label_positions.begin(), occupied_label_positions.end(),
                [traffic_x, traffic_y](const auto& placed) {
                    return std::abs(traffic_x - placed.first) < 92 &&
                           std::abs(traffic_y - placed.second) < 24;
                });
            if (show_labels && !label_crowded && traffic_x < map_right - 90) {
                char traffic_label[80]{};
                if (target.on_ground) {
                    std::snprintf(traffic_label, sizeof(traffic_label), "%s  GND",
                                  target.callsign.c_str());
                } else {
                    const int relative_hundreds = static_cast<int>(std::lround(
                        (target.altitude_feet - telemetry.altitude_feet) / 100.0));
                    const char trend = target.vertical_speed_fpm > 300.0 ? '^' :
                                       target.vertical_speed_fpm < -300.0 ? 'v' : '-';
                    std::snprintf(traffic_label, sizeof(traffic_label), "%s  %+d%c",
                                  target.callsign.c_str(), relative_hundreds, trend);
                }
                draw_map_label(traffic_x + 12, traffic_y + 6, traffic_label,
                               Color{1.0F, 0.82F, 0.18F, 1.0F});
                occupied_label_positions.emplace_back(traffic_x, traffic_y);
            }
            if (++rendered_traffic >= 48) break;
        }
    }

    const auto aircraft_point = map_screen_point(
        map, viewport, telemetry.latitude_degrees, telemetry.longitude_degrees);
    if (show_aircraft && aircraft_point.valid && aircraft_point.x >= map_left + 12 &&
        aircraft_point.x <= map_right - 12 && aircraft_point.y >= map_bottom + 62 &&
        aircraft_point.y <= map_top - 12) {
        draw_aircraft_symbol(static_cast<int>(aircraft_point.x),
                             static_cast<int>(aircraft_point.y),
                             telemetry.true_heading_degrees != 0.0
                                 ? telemetry.true_heading_degrees
                                 : telemetry.heading_degrees);
    }
    if (show_route) {
        draw_rectangle(map_left + 10, map_bottom + 54, map_right - 10,
                       map_bottom + 28, status_bar);
        draw_border(map_left + 10, map_bottom + 54, map_right - 10, map_bottom + 28,
                    Color{0.26F, 0.72F, 0.82F, 1.0F}, 1.0F);
        draw_tactical_corners(map_left + 10, map_bottom + 54, map_right - 10,
                              map_bottom + 28, accent, 6);
        draw_shadowed_text(map_left + 22, map_bottom + 36,
                           map_progress_text(progress), text_primary);
    }
    std::string attribution = map.style() == MapStyle::street
        ? "(c) OpenStreetMap contributors"
        : "Map data (c) OpenStreetMap contributors, SRTM  |  Map style (c) OpenTopoMap (CC-BY-SA)";
    if (traffic.source == TrafficSource::online || traffic.source == TrafficSource::blended) {
        attribution += "  |  Traffic (c) adsb.lol (ODbL)";
    }
    draw_rectangle(map_left, map_bottom + 22, map_right, map_bottom, status_bar);
    draw_line(map_left, map_bottom + 22, map_right, map_bottom + 22,
              Color{0.24F, 0.62F, 0.72F, 1.0F});
    draw_shadowed_text(map_left + 8, map_bottom + 6, shortened(attribution, 104), text_muted);

    const int recenter_right = map_right - 10;
    const int recenter_left = recenter_right - 42;
    const int recenter_bottom = map_bottom + 62;
    const int recenter_top = recenter_bottom + 36;
    draw_map_style_button(recenter_left, recenter_top, recenter_right, "HOME",
                          map.following_aircraft());

    const int utility_right = map_right - 10;
    const int utility_left = utility_right - 42;
    int utility_top = map_top - 10;
    draw_map_style_button(utility_left, utility_top, utility_right, "+", false);
    utility_top -= 34;
    draw_map_style_button(utility_left, utility_top, utility_right, "-", false);
    utility_top -= 34;
    draw_map_style_button(utility_left, utility_top, utility_right, "DEP", false);
    utility_top -= 34;
    draw_map_style_button(utility_left, utility_top, utility_right, "ARR", false);

    if (show_aircraft_info) {
        char live_info[96]{};
        std::snprintf(live_info, sizeof(live_info),
                      "ALT %.0f FT  GS %.0f KT  HDG %03.0f",
                      telemetry.altitude_feet, telemetry.ground_speed_knots,
                      std::fmod(telemetry.heading_degrees + 360.0, 360.0));
        const int info_left = map_left + 12;
        const int info_top = map_top - 10;
        const int info_right = std::min(map_right - 62, info_left + 252);
        draw_rectangle(info_left, info_top, info_right, info_top - 24, status_bar);
        draw_border(info_left, info_top, info_right, info_top - 24, accent, 1.0F);
        draw_tactical_corners(info_left, info_top, info_right, info_top - 24,
                              text_primary, 6);
        const std::size_t capacity = static_cast<std::size_t>(
            std::max(8, (info_right - info_left - 20) / 7));
        draw_shadowed_text(info_left + 10, info_top - 17,
                           shortened(live_info, capacity), text_primary);
    }

    const TrafficTarget* selected_traffic = nullptr;
    if (selected_traffic_key) {
        const auto selected = std::ranges::find_if(traffic.targets, [&](const auto& target) {
            return traffic_key(target) == *selected_traffic_key;
        });
        if (selected != traffic.targets.end()) selected_traffic = &*selected;
    }
    if (selected_traffic) {
        const int detail_left = map_left + 12;
        const int detail_top = map_top - (show_aircraft_info ? 42 : 12);
        const int detail_right = std::min(map_right - 62, detail_left + 330);
        const int detail_bottom = detail_top - 104;
        const std::size_t detail_characters = static_cast<std::size_t>(
            std::max(18, (detail_right - detail_left - 24) / 7));
        const Color traffic_color{1.0F, 0.82F, 0.18F, 1.0F};
        draw_rectangle(detail_left - 3, detail_top + 3, detail_right + 3,
                       detail_bottom - 3, Color{0.015F, 0.018F, 0.020F, 1.0F});
        draw_rectangle(detail_left, detail_top, detail_right, detail_bottom,
                       Color{0.075F, 0.095F, 0.110F, 1.0F});
        draw_border(detail_left, detail_top, detail_right, detail_bottom, traffic_color, 2.0F);
        draw_tactical_corners(detail_left, detail_top, detail_right, detail_bottom,
                              text_primary, 8);
        const std::string title = selected_traffic->callsign.empty()
            ? "TRAFFIC" : selected_traffic->callsign;
        const std::string registration = selected_traffic->registration.empty()
            ? "REG NOT PUBLISHED" : selected_traffic->registration;
        const std::string aircraft = !selected_traffic->aircraft_name.empty()
            ? selected_traffic->aircraft_name
            : (!selected_traffic->aircraft_type.empty()
                   ? selected_traffic->aircraft_type : "TYPE NOT PUBLISHED");
        char performance[96]{};
        std::snprintf(performance, sizeof(performance), "ALT %.0f FT   SPEED %.0f KT",
                      selected_traffic->altitude_feet,
                      selected_traffic->ground_speed_knots);
        std::string route = "ROUTE LOOKUP...";
        if (selected_traffic->route_lookup_complete) {
            route = selected_traffic->departure_airport.empty() ||
                    selected_traffic->destination_airport.empty()
                ? (selected_traffic->route_lookup_status.empty()
                       ? "DEP / DEST NOT PUBLISHED"
                       : selected_traffic->route_lookup_status)
                : "ROUTE  " + selected_traffic->departure_airport + " > " +
                      selected_traffic->destination_airport;
        }
        draw_shadowed_text(detail_left + 12, detail_top - 20,
                           shortened(title + "  /  " + registration, detail_characters),
                           traffic_color);
        draw_shadowed_text(detail_left + 12, detail_top - 42,
                           shortened("AIRCRAFT  " + aircraft, detail_characters), text_primary);
        draw_shadowed_text(detail_left + 12, detail_top - 64,
                           shortened(performance, detail_characters), text_primary);
        draw_shadowed_text(detail_left + 12, detail_top - 86,
                           shortened(route, detail_characters), connected);
        draw_text(detail_left + 12, detail_top - 99,
                  shortened("Click aircraft again to close  |  Route data: CC0",
                            detail_characters),
                  text_muted);
    }

    if (hovered_poi || hovered_target) {
        const int tooltip_width = std::min(290, map_right - map_left - 90);
        const int tooltip_left = map_left + 12;
        const int tooltip_top = map_top - (show_aircraft_info ? 42 : 12) -
                                (selected_traffic ? 112 : 0);
        const int tooltip_right = tooltip_left + tooltip_width;
        const int tooltip_bottom = tooltip_top - 56;
        const Color tooltip_accent = hovered_poi
            ? poi_color(hovered_poi->category) : connected;
        draw_rectangle(tooltip_left, tooltip_top, tooltip_right, tooltip_bottom, status_bar);
        draw_border(tooltip_left, tooltip_top, tooltip_right, tooltip_bottom,
                    tooltip_accent, 2.0F);
        draw_tactical_corners(tooltip_left, tooltip_top, tooltip_right, tooltip_bottom,
                              text_primary, 8);
        draw_shadowed_text(tooltip_left + 12, tooltip_top - 23,
                           hovered_poi ? shortened(hovered_poi->name, 34)
                                       : shortened(hovered_target->leg.identifier, 34),
                           text_primary);
        draw_shadowed_text(tooltip_left + 12, tooltip_top - 47,
                           hovered_poi
                               ? shortened(std::string(poi_category_label(hovered_poi->category)) +
                                               "  /  " + hovered_poi->detail, 38)
                               : "Airport  /  click for route options",
                           tooltip_accent);
    }

    if (filters_open) {
        const auto filters = map_filter_geometry(map_left, map_top, map_right, map_bottom);
        draw_rectangle(filters.left - 4, filters.top + 4, filters.right + 4,
                       filters.bottom - 4, Color{0.015F, 0.018F, 0.020F, 1.0F});
        draw_rectangle(filters.left, filters.top, filters.right, filters.bottom,
                       Color{0.105F, 0.115F, 0.120F, 1.0F});
        draw_border(filters.left, filters.top, filters.right, filters.bottom, accent, 2.0F);
        draw_tactical_corners(filters.left, filters.top, filters.right, filters.bottom,
                              text_primary, 10);
        draw_shadowed_text(filters.left + 12, filters.top - 24, "MAP FILTERS", text_primary,
                           xplmFont_Proportional);
        constexpr int step = 25;
        const int effective_scroll = std::clamp(filter_scroll, 0, filters.max_scroll);
        const auto visible_row = [&filters](int row_top) {
            return row_top <= filters.first_row_top + 1 &&
                   row_top - 22 >= filters.bottom + 6;
        };
        const auto draw_row = [&](int index, std::string_view label, bool enabled) {
            const int row_top = filters.first_row_top + effective_scroll * step - index * step;
            if (visible_row(row_top)) {
                draw_filter_row(filters.left + 8, row_top, filters.right - 8, label, enabled);
            }
        };
        draw_row(0, "Aircraft", show_aircraft);
        draw_row(1, "Aircraft information", show_aircraft_info);
        draw_row(2, "Flight plan", show_route);
        draw_row(3, "Map labels", show_labels);
        draw_row(4, "Weather", map.layer_enabled(MapLayer::weather));
        draw_row(5, "Airports", map.layer_enabled(MapLayer::airports));
        draw_row(6, "Navaids", map.layer_enabled(MapLayer::navaids));
        draw_row(7, "Airspace", map.layer_enabled(MapLayer::airspace));
        const std::string traffic_source = traffic.source == TrafficSource::online
            ? (traffic.online_degraded ? "DEGRADED" : "ONLINE") :
            traffic.source == TrafficSource::blended
            ? (traffic.online_degraded ? "DEGRADED+TCAS" : "ONLINE+TCAS") :
            traffic.source == TrafficSource::simulator
            ? (traffic.status.find("retry") != std::string::npos ? "TCAS / RETRY" : "TCAS")
            : "WAIT";
        const std::string traffic_label = "Traffic  " +
            std::to_string(traffic.targets.size()) + " / " + traffic_source;
        draw_row(8, traffic_label, map.layer_enabled(MapLayer::traffic));
        draw_row(9, "Points of interest", map.poi_enabled());
        const int size_top = filters.first_row_top + effective_scroll * step - 10 * step;
        if (visible_row(size_top)) {
            draw_rectangle(filters.left + 8, size_top, filters.right - 8, size_top - 24, card);
            draw_text(filters.left + 16, size_top - 17,
                      "ICON SIZE  " + std::to_string(marker_scale) + "%", text_primary);
            draw_button(filters.right - 68, size_top - 1,
                        filters.right - 42, size_top - 23, "-");
            draw_button(filters.right - 38, size_top - 1,
                        filters.right - 12, size_top - 23, "+");
        }
        const int range_top = filters.first_row_top + effective_scroll * step - 11 * step;
        if (visible_row(range_top)) {
            draw_rectangle(filters.left + 8, range_top, filters.right - 8,
                           range_top - 24, card);
            draw_text(filters.left + 16, range_top - 17,
                      "ONLINE RANGE  " + std::to_string(traffic.online_range_nm) + " NM",
                      text_primary);
            draw_button(filters.right - 68, range_top - 1,
                        filters.right - 42, range_top - 23, "-");
            draw_button(filters.right - 38, range_top - 1,
                        filters.right - 12, range_top - 23, "+");
        }
        const int provider_top = filters.first_row_top + effective_scroll * step - 12 * step;
        if (visible_row(provider_top)) {
            draw_rectangle(filters.left + 8, provider_top, filters.right - 8,
                           provider_top - 24,
                           traffic.online_degraded
                               ? Color{0.30F, 0.19F, 0.035F, 1.0F}
                               : Color{0.070F, 0.080F, 0.085F, 1.0F});
            draw_text(filters.left + 16, provider_top - 17,
                      shortened("SOURCE  " + traffic.status, 27),
                      traffic.online_degraded ? hover_accent : text_muted);
        }
        if (filters.max_scroll > 0) {
            const int track_top = filters.top - 38;
            const int track_bottom = filters.bottom + 8;
            const int thumb_height = std::max(
                22, (track_top - track_bottom) * filters.visible_slots /
                        map_filter_content_slots);
            const int travel = std::max(0, track_top - track_bottom - thumb_height);
            const int thumb_top = track_top -
                (filters.max_scroll == 0 ? 0 : travel * effective_scroll / filters.max_scroll);
            draw_rectangle(filters.right - 4, track_top,
                           filters.right - 1, track_bottom, Color{0.05F, 0.06F, 0.065F, 1.0F});
            draw_rectangle(filters.right - 5, thumb_top,
                           filters.right, thumb_top - thumb_height, accent);
        }
    }

}

std::string runway_detail(const AirportRunway& runway) {
    char value[96]{};
    std::snprintf(value, sizeof(value), "%s  |  %.0f x %.0f ft  |  %s",
                  runway.identifiers.c_str(), runway.length_feet, runway.width_feet,
                  runway.surface.c_str());
    return value;
}

std::string frequency_detail(const AirportFrequency& frequency) {
    char value[96]{};
    std::snprintf(value, sizeof(value), "%s  %.3f  %s", frequency.type.c_str(),
                  frequency.megahertz, frequency.name.c_str());
    return value;
}

std::string procedure_list(std::string_view label, const std::vector<std::string>& values,
                           std::size_t total_count) {
    std::string result(label);
    result += " (" + std::to_string(total_count) + "): ";
    if (values.empty()) return result + "None in installed navdata";
    for (const auto& value : values) {
        if (result.size() + value.size() + 2 > 92) {
            result += "...";
            break;
        }
        if (result.back() != ' ') result += ", ";
        result += value;
    }
    return result;
}

void draw_airports_page(std::string_view query, const AirportInfoSnapshot& airport,
                        int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right, 420);
    draw_text(left, top, "Airport Information", text_primary, xplmFont_Proportional);
    draw_text(left, top - 27, "Runways, frequencies, and procedures from installed X-Plane data", text_muted);
    draw_rectangle(left, top - 45, card_right - 86, top - 80, card);
    draw_border(left, top - 45, card_right - 86, top - 80, map_grid, 1.0F);
    const std::string entry = query.empty() ? "Type airport identifier..." : std::string(query) + "_";
    draw_text(left + 12, top - 68, entry, query.empty() ? text_muted : text_primary);
    draw_button(card_right - 78, top - 45, card_right, top - 80, "Search", true);

    if (airport.state == AirportLookupState::idle) {
        draw_card(left, top - 98, card_right, top - 184,
                  "Search an airport", "Examples: KSEA, KPDX, EGLL, YSSY");
        return;
    }
    if (airport.state == AirportLookupState::loading) {
        draw_card(left, top - 98, card_right, top - 184,
                  "Searching " + airport.identifier, airport.message);
        return;
    }
    if (airport.state != AirportLookupState::ready) {
        draw_card(left, top - 98, card_right, top - 184,
                  airport.identifier.empty() ? "Airport unavailable" : airport.identifier,
                  airport.message);
        return;
    }

    char airport_summary[128]{};
    std::snprintf(airport_summary, sizeof(airport_summary), "%s  |  Elevation %d ft  |  %zu runways",
                  airport.name.c_str(), airport.elevation_feet, airport.runways.size());
    draw_text(left, top - 105, airport.identifier, accent, xplmFont_Proportional);
    draw_text(left + 70, top - 105, airport_summary, text_primary);

    const int gap = 12;
    const int middle = (left + card_right) / 2;
    const int runway_right = middle - gap / 2;
    const int frequency_left = middle + gap / 2;
    draw_text(left, top - 137, "RUNWAYS", accent);
    draw_text(frequency_left, top - 137, "FREQUENCIES", accent);
    int row_y = top - 163;
    for (std::size_t index = 0; index < std::min<std::size_t>(4, airport.runways.size()); ++index) {
        draw_text(left, row_y, runway_detail(airport.runways[index]), text_primary);
        row_y -= 25;
    }
    if (airport.runways.empty()) draw_text(left, row_y, "No land runways listed", text_muted);
    row_y = top - 163;
    for (std::size_t index = 0; index < std::min<std::size_t>(5, airport.frequencies.size()); ++index) {
        draw_text(frequency_left, row_y, frequency_detail(airport.frequencies[index]), text_primary);
        row_y -= 25;
    }
    if (airport.frequencies.empty()) draw_text(frequency_left, row_y, "No COM frequencies listed", text_muted);
    draw_line(left, top - 267, card_right, top - 267, map_grid);
    draw_text(left, top - 294, "PROCEDURES", accent);
    draw_text(left, top - 321, procedure_list("SIDs", airport.procedures.departures,
                                              airport.procedures.departure_count), text_primary);
    draw_text(left, top - 348, procedure_list("STARs", airport.procedures.arrivals,
                                              airport.procedures.arrival_count), text_primary);
    draw_text(left, top - 375, procedure_list("Approaches", airport.procedures.approaches,
                                              airport.procedures.approach_count), text_primary);
    draw_text(left, bottom + 42, airport.message, text_muted);
    static_cast<void>(runway_right);
}

constexpr std::array briefing_tabs{
    std::pair{BriefingTab::summary, std::string_view{"Summary"}},
    std::pair{BriefingTab::library, std::string_view{"Library"}},
    std::pair{BriefingTab::checklist, std::string_view{"Checklist"}},
    std::pair{BriefingTab::notes, std::string_view{"Notes"}},
};

std::pair<std::string, std::string> briefing_airports(const FlightPlanSnapshot& flight_plan) {
    std::string departure;
    std::string destination;
    for (const auto& leg : flight_plan.legs) {
        if (leg.kind != WaypointKind::airport) continue;
        if (departure.empty()) departure = leg.identifier;
        destination = leg.identifier;
    }
    return {departure, destination};
}

std::string shortened(std::string_view value, std::size_t maximum) {
    if (value.size() <= maximum) return std::string(value);
    if (maximum <= 3) return std::string(value.substr(0, maximum));
    return std::string(value.substr(0, maximum - 3)) + "...";
}

std::vector<std::string> wrapped_text(std::string_view value, std::size_t width,
                                      std::size_t maximum_lines) {
    std::vector<std::string> lines;
    std::string current;
    auto flush = [&] {
        lines.push_back(current);
        current.clear();
    };
    std::size_t position = 0;
    while (position < value.size() && lines.size() < maximum_lines) {
        if (value[position] == '\n') { flush(); ++position; continue; }
        const auto end = value.find_first_of(" \n", position);
        const auto length = (end == std::string_view::npos ? value.size() : end) - position;
        std::string word(value.substr(position, length));
        if (!current.empty() && current.size() + 1 + word.size() > width) flush();
        if (word.size() > width) word.resize(width);
        if (!current.empty()) current += ' ';
        current += word;
        position = end == std::string_view::npos ? value.size() : end;
        while (position < value.size() && value[position] == ' ') ++position;
    }
    if (!current.empty() && lines.size() < maximum_lines) flush();
    return lines;
}

void draw_briefing_tabs(const BriefingModel& briefing, int left, int top) {
    int tab_left = left;
    for (const auto& [tab, label] : briefing_tabs) {
        draw_button(tab_left, top - 45, tab_left + 92, top - 78, label,
                    briefing.active_tab() == tab);
        tab_left += 98;
    }
}

void draw_briefing_summary(const TelemetrySnapshot& telemetry,
                           const FlightPlanSnapshot& flight_plan,
                           const RouteProgressSnapshot& progress,
                           const WeatherSnapshot& weather,
                           const PlanningSnapshot& planning,
                           int left, int top, int right) {
    const int card_right = responsive_card_right(left, right, 420);
    const int gap = 12;
    const int middle = (left + card_right) / 2;
    const int left_right = middle - gap / 2;
    const int right_left = middle + gap / 2;
    std::string route = "No active route";
    if (!flight_plan.legs.empty()) {
        route = flight_plan.legs.front().identifier + "  >  " +
                (progress.destination.available ? progress.destination.identifier
                                                 : flight_plan.legs.back().identifier);
    }
    draw_card(left, top - 100, left_right, top - 186, "AIRCRAFT",
              telemetry.available ? shortened(telemetry.aircraft_name, 34) : "Waiting for aircraft");
    draw_card(right_left, top - 100, card_right, top - 186, "ROUTE", route);
    draw_card(left, top - 198, card_right, top - 284, "DEPARTURE WEATHER",
              weather.departure.metar.empty() ? "No departure METAR" : shortened(weather.departure.metar, 88));
    draw_card(left, top - 296, card_right, top - 382, "DESTINATION WEATHER",
              weather.destination.metar.empty() ? "No destination METAR" : shortened(weather.destination.metar, 88));
    std::string readiness = "Waiting for weight and fuel plan";
    Color readiness_color = text_muted;
    if (planning.fuel_plan_available) {
        readiness = planning.dispatch_ready ? "Weight and fuel plan checks pass"
                                             : "Review gross weight or reserve fuel";
        readiness_color = planning.dispatch_ready ? connected : Color{1.0F, 0.42F, 0.30F, 1.0F};
    }
    draw_text(left + 18, top - 418, readiness, readiness_color);
}

bool library_entry_matches(const LibraryEntry& entry, std::string_view airport) {
    if (airport.empty()) return false;
    if (entry.name.size() <= airport.size() || entry.name.substr(0, airport.size()) != airport) return false;
    return entry.name[airport.size()] == '/' || entry.name[airport.size()] == '\\';
}

std::vector<std::size_t> filtered_library_indices(const BriefingModel& briefing) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < briefing.library().size(); ++index) {
        if (library_entry_matches(briefing.library()[index], briefing.library_airport())) indices.push_back(index);
    }
    return indices;
}

std::size_t library_visible_start(const BriefingModel& briefing,
                                  const std::vector<std::size_t>& indices,
                                  std::size_t visible) {
    if (indices.size() <= visible || briefing.selected_entry_index() < 0) return 0;
    const auto selected = std::find(indices.begin(), indices.end(),
                                    static_cast<std::size_t>(briefing.selected_entry_index()));
    if (selected == indices.end()) return 0;
    const auto position = static_cast<std::size_t>(std::distance(indices.begin(), selected));
    if (position < visible) return 0;
    return std::min(indices.size() - visible, position - visible + 1);
}

std::size_t library_visible_count(int row_start, int bottom, std::size_t count) {
    const int available_height = std::max(34, row_start - (bottom + 52));
    return std::min(count, static_cast<std::size_t>(
        std::max(1, available_height / 38)));
}

void select_library_airport(BriefingModel& briefing, std::string airport) {
    briefing.select_library_airport(std::move(airport));
    const auto indices = filtered_library_indices(briefing);
    if (!indices.empty()) briefing.select_entry(static_cast<int>(indices.front()));
}

bool select_library_offset(BriefingModel& briefing, int offset) {
    const auto indices = filtered_library_indices(briefing);
    if (indices.empty()) return false;
    const auto selected = std::find(indices.begin(), indices.end(),
                                    static_cast<std::size_t>(std::max(0, briefing.selected_entry_index())));
    const int position = selected == indices.end()
        ? 0 : static_cast<int>(std::distance(indices.begin(), selected));
    const int next = position + offset;
    return next >= 0 && next < static_cast<int>(indices.size()) &&
           briefing.select_entry(static_cast<int>(indices[static_cast<std::size_t>(next)]));
}

void draw_briefing_library(const BriefingModel& briefing, const FlightPlanSnapshot& flight_plan,
                           int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right, 420);
    const auto [departure, destination] = briefing_airports(flight_plan);
    const bool compact = card_right - left < 500;
    if (compact) {
        const int width = (card_right - left - 12) / 3;
        draw_button(left, top - 98, left + width, top - 131,
                    departure.empty() ? "DEP --" : "DEP " + departure,
                    !departure.empty() && briefing.library_airport() == departure);
        draw_button(left + width + 6, top - 98, left + width * 2 + 6, top - 131,
                    destination.empty() ? "DEST --" : "DEST " + destination,
                    !destination.empty() && briefing.library_airport() == destination);
        draw_button(left + width * 2 + 12, top - 98, card_right, top - 131, "Refresh");
        draw_button(left, top - 137, left + width, top - 170, "Open",
                    briefing.selected_entry() != nullptr);
        draw_button(left + width + 6, top - 137, left + width * 2 + 6, top - 170, "Up");
        draw_button(left + width * 2 + 12, top - 137, card_right, top - 170, "Down");
    } else {
        draw_button(left, top - 98, left + 90, top - 131,
                    departure.empty() ? "DEP --" : "DEP " + departure,
                    !departure.empty() && briefing.library_airport() == departure);
        draw_button(left + 96, top - 98, left + 186, top - 131,
                    destination.empty() ? "DEST --" : "DEST " + destination,
                    !destination.empty() && briefing.library_airport() == destination);
        draw_button(left + 192, top - 98, left + 270, top - 131, "Refresh");
        draw_button(left + 276, top - 98, left + 342, top - 131, "Open",
                    briefing.selected_entry() != nullptr);
        draw_button(left + 348, top - 98, left + 398, top - 131, "Up");
        draw_button(left + 404, top - 98, left + 462, top - 131, "Down");
    }
    const std::string library_status = briefing.library_airport().empty()
        ? std::string(briefing.library_message())
        : std::string(briefing.library_airport()) + " briefing and charts  /  " +
              std::string(briefing.library_message());
    const int status_offset = compact ? 185 : 146;
    draw_text(left, top - status_offset, shortened(library_status, 72), text_muted);
    const int list_right = std::min(left + 230, card_right - 220);
    const int row_start = top - (compact ? 197 : 158);
    constexpr int row_height = 34;
    const auto indices = filtered_library_indices(briefing);
    const std::size_t visible = library_visible_count(row_start, bottom, indices.size());
    const std::size_t start = library_visible_start(briefing, indices, visible);
    for (std::size_t offset = 0; offset < visible; ++offset) {
        const std::size_t index = indices[start + offset];
        const int row_top = row_start - static_cast<int>(offset) * (row_height + 4);
        const bool selected = static_cast<int>(index) == briefing.selected_entry_index();
        draw_rectangle(left, row_top, list_right - 8, row_top - row_height,
                       selected ? active_navigation : card);
        const auto& entry = briefing.library()[index];
        draw_text(left + 8, row_top - 22,
                  std::string(entry.category == LibraryCategory::chart ? "CHART  " : "BRIEF  ") +
                      shortened(entry.name, 23),
                  selected ? text_primary : text_muted);
    }
    if (indices.size() > visible && visible > 0) {
        const int track_top = row_start;
        const int track_bottom = row_start - static_cast<int>(visible) * 38 + 4;
        const int track_height = std::max(12, track_top - track_bottom);
        const int thumb_height = std::max(
            22, track_height * static_cast<int>(visible) / static_cast<int>(indices.size()));
        const int maximum_start = static_cast<int>(indices.size() - visible);
        const int travel = std::max(0, track_height - thumb_height);
        const int thumb_top = track_top - (maximum_start > 0
            ? travel * static_cast<int>(start) / maximum_start : 0);
        draw_rectangle(list_right - 5, track_top, list_right, track_bottom, status_bar);
        draw_rectangle(list_right - 6, thumb_top, list_right + 1,
                       thumb_top - thumb_height, accent);
    }
    if (indices.empty()) {
        draw_text(left, row_start - 22,
                  briefing.library_airport().empty()
                      ? "Select Departure or Destination to view its briefing and charts."
                      : "This airport archive is still loading or has no files.", text_muted);
    }
    const int preview_left = list_right + 14;
    draw_rectangle(preview_left, row_start, card_right, bottom + 46, card);
    const auto* selected = briefing.selected_entry();
    if (!selected) {
        draw_text(preview_left + 14, row_start - 28, "No document selected", text_muted);
        return;
    }
    draw_text(preview_left + 14, row_start - 28, shortened(selected->name, 38), accent);
    if (selected->text_content.empty()) {
        draw_text(preview_left + 14, row_start - 58, "Select Open to view this PDF", text_primary);
        draw_text(preview_left + 14, row_start - 84, "inside OpenEFB's document viewer.", text_muted);
        return;
    }
    const auto lines = wrapped_text(selected->text_content, 42, 11);
    int line_y = row_start - 58;
    for (const auto& line : lines) {
        draw_text(preview_left + 14, line_y, line, text_primary);
        line_y -= 24;
    }
}

void draw_briefing_checklist(const BriefingModel& briefing, int left, int top, int right) {
    const int card_right = responsive_card_right(left, right, 420);
    const int phase_width = std::max(88, (card_right - left - 12) / 3);
    draw_button(left, top - 96, left + phase_width, top - 130, "Preflight",
                briefing.checklist_phase() == ChecklistPhase::preflight);
    draw_button(left + phase_width + 6, top - 96, left + phase_width * 2 + 6,
                top - 130, "Takeoff/Cruise",
                briefing.checklist_phase() == ChecklistPhase::takeoff_cruise);
    draw_button(left + phase_width * 2 + 12, top - 96, card_right, top - 130,
                "Descent/Landing",
                briefing.checklist_phase() == ChecklistPhase::descent_landing);
    char progress[64]{};
    std::snprintf(progress, sizeof(progress), "%zu of %zu complete",
                  briefing.completed_checklist_items(), briefing.checklist().size());
    draw_text(left, top - 153, shortened(std::string(briefing.checklist_aircraft()) +
              "  |  offline general checklist  |  " + progress, 82), text_muted);
    draw_button(card_right - 78, top - 139, card_right, top - 172, "Reset");
    int row_top = top - 188;
    for (const auto& item : briefing.checklist()) {
        draw_rectangle(left, row_top, card_right, row_top - 40, card);
        draw_border(left + 12, row_top - 10, left + 32, row_top - 30,
                    item.checked ? connected : map_grid, 2.0F);
        if (item.checked) {
            draw_line(left + 16, row_top - 20, left + 21, row_top - 26, connected, 2.0F);
            draw_line(left + 21, row_top - 26, left + 29, row_top - 14, connected, 2.0F);
        }
        draw_text(left + 46, row_top - 26, item.label, item.checked ? connected : text_primary);
        row_top -= 46;
    }
}

void draw_briefing_notes(const BriefingModel& briefing, int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right, 420);
    draw_text(left, top - 112, "Click the note area and type. Notes save with the EFB window.", text_muted);
    draw_button(card_right - 72, top - 98, card_right, top - 131, "Clear");
    draw_rectangle(left, top - 146, card_right, bottom + 46, card);
    draw_border(left, top - 146, card_right, bottom + 46, map_grid, 1.0F);
    std::string display(briefing.notes());
    display += "_";
    const auto lines = wrapped_text(display, 78, 12);
    int line_y = top - 174;
    for (const auto& line : lines) {
        draw_text(left + 14, line_y, line, text_primary);
        line_y -= 24;
    }
}

void draw_briefing_page(const BriefingModel& briefing, const TelemetrySnapshot& telemetry,
                        const FlightPlanSnapshot& flight_plan, const RouteProgressSnapshot& progress,
                        const WeatherSnapshot& weather, const PlanningSnapshot& planning,
                        int left, int top, int right, int bottom) {
    draw_text(left, top, "Flight Briefing", text_primary, xplmFont_Proportional);
    draw_text(left, top - 28, "Route review, local charts and documents, checklist, and notes", text_muted);
    draw_briefing_tabs(briefing, left, top);
    switch (briefing.active_tab()) {
    case BriefingTab::summary:
        draw_briefing_summary(telemetry, flight_plan, progress, weather, planning, left, top, right);
        break;
    case BriefingTab::library:
        draw_briefing_library(briefing, flight_plan, left, top, right, bottom);
        break;
    case BriefingTab::checklist:
        draw_briefing_checklist(briefing, left, top, right);
        break;
    case BriefingTab::notes:
        draw_briefing_notes(briefing, left, top, right, bottom);
        break;
    }
}

std::string log_time(int seconds) {
    const int hours = seconds / 3600;
    const int minutes = (seconds % 3600) / 60;
    char value[32]{};
    std::snprintf(value, sizeof(value), "%02d:%02d", hours, minutes);
    return value;
}

void draw_logbook_page(const FlightLogSnapshot& log, int selected_entry,
                       int left, int top, int right) {
    const int card_right = responsive_card_right(left, right, 420);
    draw_text(left, top, "Flight Logbook", text_primary, xplmFont_Proportional);
    draw_text(left, top - 28, "Persistent takeoff, landing, route-track, altitude, and landing-rate records", text_muted);
    draw_card(left, top - 62, card_right, top - 158, log.airborne ? "CURRENT FLIGHT - AIRBORNE" : "CURRENT FLIGHT - ON GROUND",
              log.departure + "  to  " + log.destination);
    char details[180]{};
    std::snprintf(details, sizeof(details), "%s  |  %s  |  %.1f NM  |  max %.0f ft",
                  log.aircraft_name.c_str(), log_time(log.airborne_seconds).c_str(),
                  log.distance_nm, log.maximum_altitude_feet);
    draw_text(left + 16, top - 132, details, log.airborne ? connected : text_primary);
    draw_text(left, top - 188, "RECENT FLIGHTS", accent, xplmFont_Proportional);
    if (log.entries.empty()) {
        draw_card(left, top - 210, card_right, top - 292, "No completed flights yet",
                  "OpenEFB starts a record after takeoff and saves it when you land.");
        return;
    }
    int row_top = top - 214;
    for (std::size_t index = 0; index < log.entries.size() && index < 6; ++index) {
        const auto& entry = log.entries[index];
        draw_rectangle(left, row_top, card_right, row_top - 42,
                       index % 2 == 0 ? card : Color{0.145F, 0.155F, 0.160F, 1.0F});
        char row[220]{};
        std::snprintf(row, sizeof(row), "%s  |  %s  %s-%s  |  %s  |  %.1f NM  |  landing %.0f fpm",
                      entry.completed_utc.c_str(), entry.aircraft_name.c_str(),
                      entry.departure.c_str(), entry.destination.c_str(),
                      log_time(entry.airborne_seconds).c_str(), entry.distance_nm,
                      entry.landing_vertical_speed_fpm);
        draw_text(left + 14, row_top - 27, shortened(row, 96), text_primary);
        row_top -= 46;
    }
    if (selected_entry >= 0 && selected_entry < static_cast<int>(log.entries.size())) {
        const auto& entry = log.entries[static_cast<std::size_t>(selected_entry)];
        const int detail_left = left + 24;
        const int detail_right = card_right - 24;
        const int detail_top = top - 178;
        const int detail_bottom = detail_top - 300;
        draw_rectangle(detail_left - 5, detail_top + 5, detail_right + 5,
                       detail_bottom - 5, status_bar);
        draw_rectangle(detail_left, detail_top, detail_right, detail_bottom,
                       Color{0.065F, 0.085F, 0.105F, 1.0F});
        draw_border(detail_left, detail_top, detail_right, detail_bottom, accent, 2.0F);
        draw_text(detail_left + 18, detail_top - 28, "FLIGHT DETAILS", accent,
                  xplmFont_Proportional);
        draw_text(detail_left + 18, detail_top - 57,
                  shortened(entry.aircraft_name, 56), text_primary);
        draw_text(detail_left + 18, detail_top - 84,
                  entry.departure + "  >  " + entry.destination + "  |  " + entry.completed_utc,
                  text_primary);
        draw_text(detail_left + 18, detail_top - 121,
                  "AIRBORNE  " + log_time(entry.airborne_seconds) +
                  "    DISTANCE  " + std::to_string(static_cast<int>(std::lround(entry.distance_nm))) + " NM",
                  text_muted);
        draw_text(detail_left + 18, detail_top - 150,
                  "MAX ALT  " + std::to_string(static_cast<int>(std::lround(entry.maximum_altitude_feet))) +
                  " FT    MAX GS  " + std::to_string(static_cast<int>(std::lround(entry.maximum_ground_speed_knots))) + " KT",
                  text_primary);
        draw_text(detail_left + 18, detail_top - 179,
                  "MAX CLIMB  " + std::to_string(static_cast<int>(std::lround(entry.maximum_climb_rate_fpm))) +
                  " FPM    MAX DESCENT  " + std::to_string(static_cast<int>(std::lround(entry.maximum_descent_rate_fpm))) + " FPM",
                  text_primary);
        draw_text(detail_left + 18, detail_top - 208,
                  "LANDING RATE  " + std::to_string(static_cast<int>(std::lround(entry.landing_vertical_speed_fpm))) +
                  " FPM    TRACK POINTS  " + std::to_string(entry.track.size()),
                  connected);
        draw_text(detail_left + 18, detail_bottom + 26,
                  "Click this flight again to close details", text_muted);
    }
}

void draw_page_content(EfbPage page, const TelemetrySnapshot& telemetry,
                       const FlightPlanSnapshot& flight_plan,
                       const FlightPlanEditor& flight_plan_editor,
                       std::string_view route_file_message,
                       std::string_view airport_query,
                       const AirportInfoSnapshot& airport_info,
                       const PlanningSnapshot& planning,
                       const BriefingModel& briefing,
                       const RouteProgressSnapshot& route_progress,
                       const WeatherSnapshot& weather,
                       const FlightLogSnapshot& flight_log,
                       const AirspaceSnapshot& airspace,
                       const NavigationDatabaseSnapshot& navigation_database,
                       const TrafficSnapshot& traffic,
                       const MovingMapModel& moving_map,
                       XPlaneMapTiles& map_tiles, XPlaneMapPois& map_pois,
                       std::vector<MapHitTarget>& map_hit_targets,
                       std::vector<PoiHitTarget>& poi_hit_targets,
                       std::vector<TrafficHitTarget>& traffic_hit_targets,
                       const std::optional<MapPoi>& hovered_map_poi,
                       const std::optional<MapHitTarget>& hovered_map_target,
                       const std::optional<std::string>& selected_traffic_key,
                       bool map_filters_open, int map_filter_scroll,
                       bool show_map_aircraft,
                       bool show_map_aircraft_info, bool show_map_route,
                       bool show_map_labels, int map_marker_scale,
                       bool flight_plan_expanded,
                       int selected_log_entry,
                       bool show_weather_forecast,
                       const DisplayPreferences& display_preferences,
                       const MobileServerStatus& mobile_status,
                       int left, int top, int right, int bottom) {
    const int card_right = responsive_card_right(left, right);
    switch (page) {
    case EfbPage::home:
        draw_home_map(telemetry, flight_plan, route_progress, weather, airspace,
                      navigation_database, traffic, moving_map, map_tiles, map_pois, map_hit_targets,
                      poi_hit_targets, traffic_hit_targets, hovered_map_poi, hovered_map_target,
                      selected_traffic_key,
                      map_filters_open, map_filter_scroll,
                      show_map_aircraft, show_map_aircraft_info,
                      show_map_route, show_map_labels, map_marker_scale,
                      display_preferences.route_line_width,
                      left, top, right, bottom);
        break;
    case EfbPage::flight_plan:
        draw_flight_plan_page(flight_plan, flight_plan_editor, route_file_message,
                              flight_plan_expanded,
                              left, top, right, bottom);
        break;
    case EfbPage::airports:
        draw_airports_page(airport_query, airport_info, left, top, right, bottom);
        break;
    case EfbPage::progress:
        draw_progress_page(route_progress, left, top, right);
        break;
    case EfbPage::weather:
        draw_weather_page(weather, show_weather_forecast, left, top, right);
        break;
    case EfbPage::planning:
        draw_planning_page(planning, left, top, right);
        break;
    case EfbPage::briefing:
        draw_briefing_page(briefing, telemetry, flight_plan, route_progress, weather, planning,
                           left, top, right, bottom);
        break;
    case EfbPage::logbook:
        draw_logbook_page(flight_log, selected_log_entry, left, top, right);
        break;
    case EfbPage::settings:
        draw_text(left, top, "Settings", text_primary, xplmFont_Proportional);
        draw_text(left, top - 28, "Saved simulator, display, and accessibility preferences.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Inject online traffic into X-Plane TCAS",
                  shortened(traffic.injection_status + "  |  online " +
                                std::to_string(traffic.online_range_nm) + " NM", 56));
        draw_button(card_right - 122, top - 91, card_right - 18, top - 124,
                    display_preferences.inject_traffic ? "Disable" : "Enable", true);
        draw_card(left, top - 178, card_right, top - 274, "Moving exterior 3D traffic",
                  shortened(traffic.visual_traffic_status, 62));
        draw_button(card_right - 122, top - 207, card_right - 18, top - 240,
                    display_preferences.show_3d_traffic ? "Disable" : "Enable", true);
        draw_card(left, top - 294, card_right, top - 390, "Map aircraft information strip",
                  display_preferences.show_map_aircraft_info
                      ? "Visible - altitude, ground speed, and magnetic heading"
                      : "Hidden - more map area remains unobstructed");
        draw_button(card_right - 122, top - 323, card_right - 18, top - 356,
                    display_preferences.show_map_aircraft_info ? "Hide" : "Show", true);
        draw_card(left, top - 410, card_right, top - 506, "Magenta flight-plan route",
                  display_preferences.show_map_route ? "Visible on the moving map"
                                                     : "Hidden on the moving map");
        draw_button(card_right - 122, top - 439, card_right - 18, top - 472,
                    display_preferences.show_map_route ? "Hide" : "Show", true);
        draw_card(left, top - 526, card_right, top - 622, "Magenta route thickness",
                  std::to_string(display_preferences.route_line_width) +
                      " px - drag from the thinnest to the boldest setting");
        {
            const int slider_left = left + 18;
            const int slider_right = card_right - 18;
            const int slider_y = top - 604;
            const int knob_x = route_slider_position(
                display_preferences.route_line_width, slider_left, slider_right);
            draw_line(slider_left, slider_y, slider_right, slider_y, map_grid, 6.0F);
            draw_line(slider_left, slider_y, knob_x, slider_y, route_line, 6.0F);
            draw_rectangle(knob_x - 6, slider_y + 9, knob_x + 6, slider_y - 9,
                           active_navigation);
        }
        draw_card(left, top - 642, card_right, top - 738, "Map labels",
                  display_preferences.show_map_labels ? "Airport, waypoint, POI, and traffic labels visible"
                                                      : "Labels hidden for a cleaner map");
        draw_button(card_right - 122, top - 671, card_right - 18, top - 704,
                    display_preferences.show_map_labels ? "Hide" : "Show", true);
        draw_card(left, top - 758, card_right, top - 854, "Map marker size",
                  std::to_string(display_preferences.map_marker_scale) +
                      "% - cycles through 75, 100, 125, and 150 percent");
        draw_button(card_right - 122, top - 787, card_right - 18, top - 820,
                    "Next size", true);
        draw_card(left, top - 874, card_right, top - 970, "High contrast text",
                  display_preferences.high_contrast ? "Enabled - maximum text visibility" :
                                                       "Disabled - standard cockpit palette");
        draw_button(card_right - 122, top - 903, card_right - 18, top - 936,
                    display_preferences.high_contrast ? "Turn off" : "Turn on", true);
        draw_card(left, top - 990, card_right, top - 1086, "Comfort-size window",
                  display_preferences.comfort_size ? "Enabled - 1000 x 700 minimum" :
                                                     "Disabled - 900 x 640 minimum");
        draw_button(card_right - 122, top - 1019, card_right - 18, top - 1052,
                    display_preferences.comfort_size ? "Compact" : "Enlarge", true);
        draw_text(left, top - 1118, "All display settings save automatically.", text_muted);
        break;
    case EfbPage::about:
        draw_text(left, top, "About OpenEFB", text_primary, xplmFont_Proportional);
        draw_text(left, top - 28, "An open-source electronic flight bag for X-Plane 12.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Version", "OPEN EFB / 1.1 MOBILE 2.0 RC");
        draw_card(left, top - 178, card_right, top - 274,
                  "Release", "Mobile companion development preview");
        draw_card(left, top - 294, card_right, top - 390,
                  "Project", "Built in the open for the flight-sim community");
        draw_card(left, top - 410, card_right, top - 506,
                  "Mobile companion",
                  mobile_status.running ? shortened(mobile_status.url, 58) : mobile_status.message);
        draw_card(left, top - 526, card_right, top - 622,
                  "Mobile pairing code",
                  mobile_status.running ? mobile_status.pairing_code + "  |  same Wi-Fi  |  read only"
                                        : "Start OpenEFB to enable phone pairing");
        break;
    }
}

} // namespace

std::unique_ptr<WindowSurface> XPlaneWindow::create(UiModel& ui_model, TelemetryModel& telemetry_model,
                                                    FlightPlanModel& flight_plan_model,
                                                    FlightPlanEditor& flight_plan_editor,
                                                    XPlaneFlightPlan& xplane_flight_plan,
                                                    AirportInfoModel& airport_info_model,
                                                    XPlaneAirportData& xplane_airport_data,
                                                    AirspaceModel& airspace_model,
                                                    FuelModel& fuel_model,
                                                    PlanningModel& planning_model,
                                                    BriefingModel& briefing_model,
                                                    XPlaneBriefingLibrary& briefing_library,
                                                    MovingMapModel& moving_map_model,
                                                    NavigationDatabaseModel& navigation_database_model,
                                                    RouteProgressModel& route_progress_model,
                                                    WeatherModel& weather_model,
                                                    FlightLogModel& flight_log_model,
                                                    TrafficModel& traffic_model,
                                                    XPlaneMobileServer& mobile_server,
                                                    XPlanePreferences& preferences) {
    auto window = std::unique_ptr<XPlaneWindow>(
        new XPlaneWindow(ui_model, telemetry_model, flight_plan_model, flight_plan_editor,
                         xplane_flight_plan, airport_info_model, xplane_airport_data,
                         airspace_model,
                         fuel_model, planning_model, briefing_model, briefing_library,
                         moving_map_model, navigation_database_model,
                         route_progress_model,
                         weather_model, flight_log_model, traffic_model, mobile_server, preferences));
    if (!window->window_id_) {
        return nullptr;
    }
    return window;
}

XPlaneWindow::XPlaneWindow(UiModel& ui_model, TelemetryModel& telemetry_model,
                           FlightPlanModel& flight_plan_model,
                           FlightPlanEditor& flight_plan_editor,
                           XPlaneFlightPlan& xplane_flight_plan,
                           AirportInfoModel& airport_info_model,
                           XPlaneAirportData& xplane_airport_data,
                           AirspaceModel& airspace_model,
                           FuelModel& fuel_model,
                           PlanningModel& planning_model,
                           BriefingModel& briefing_model,
                           XPlaneBriefingLibrary& briefing_library,
                           MovingMapModel& moving_map_model,
                           NavigationDatabaseModel& navigation_database_model,
                           RouteProgressModel& route_progress_model,
                           WeatherModel& weather_model,
                           FlightLogModel& flight_log_model,
                           TrafficModel& traffic_model,
                           XPlaneMobileServer& mobile_server,
                           XPlanePreferences& preferences)
    : ui_model_(ui_model), telemetry_model_(telemetry_model),
      flight_plan_model_(flight_plan_model), flight_plan_editor_(flight_plan_editor),
      xplane_flight_plan_(xplane_flight_plan), airport_info_model_(airport_info_model),
      xplane_airport_data_(xplane_airport_data), airspace_model_(airspace_model), fuel_model_(fuel_model),
      planning_model_(planning_model),
      briefing_model_(briefing_model), briefing_library_(briefing_library),
      moving_map_model_(moving_map_model),
      navigation_database_model_(navigation_database_model),
      route_progress_model_(route_progress_model),
      weather_model_(weather_model),
      flight_log_model_(flight_log_model),
      traffic_model_(traffic_model),
      mobile_server_(mobile_server),
      preferences_(preferences), map_tiles_(preferences.map_cache_directory()) {
    display_preferences_ = preferences_.load_display_preferences();
    traffic_model_.set_injection_requested(display_preferences_.inject_traffic);
    traffic_model_.set_online_range_nm(display_preferences_.traffic_range_nm);
    traffic_model_.set_visual_traffic_requested(display_preferences_.show_3d_traffic);
    show_map_aircraft_info_ = display_preferences_.show_map_aircraft_info;
    show_map_route_ = display_preferences_.show_map_route;
    show_map_labels_ = display_preferences_.show_map_labels;
    map_marker_scale_ = display_preferences_.map_marker_scale;
    high_contrast_mode = display_preferences_.high_contrast;
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
    geometry.enforce_minimum(minimum_width, minimum_height);
    if (display_preferences_.comfort_size) geometry.enforce_minimum(1000, 700);

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
        XPLMSetWindowResizingLimits(window_id_, minimum_width, minimum_height, 1600, 1200);
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
    text_clip_left = left;
    text_clip_top = top;
    text_clip_right = right;
    text_clip_bottom = bottom;
    const bool compact = compact_layout(left, right);

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    draw_rectangle(left, top, right, bottom, canvas);
    draw_rectangle(left, top, right, top - status_bar_height, status_bar);
    if (!compact) {
        draw_rectangle(left, top - status_bar_height, left + sidebar_width, bottom, sidebar);
    }

    draw_text(left + (compact ? 12 : 20), top - 35, "OpenEFB", accent, xplmFont_Proportional);
    if (compact) {
        draw_text(left + 86, top - 35, shortened(ui_model_.active_page_title(), 8), text_primary);
        draw_button(right - 146, top - 10, right - 116, top - 44, "-");
        draw_button(right - 112, top - 10, right - 82, top - 44, "+");
        draw_button(right - 78, top - 10, right - 8, top - 44,
                    navigation_open_ ? "CLOSE" : "MENU", true);
    } else {
        draw_text(left + sidebar_width + 24, top - 35, ui_model_.active_page_title(), text_primary);
    }
    const auto& telemetry = telemetry_model_.snapshot();
    briefing_model_.configure_checklist_for_aircraft(telemetry.aircraft_name);
    if (telemetry.available) {
        moving_map_model_.update_aircraft_position(telemetry.latitude_degrees,
                                                   telemetry.longitude_degrees);
    }
    const auto map_source = map_tiles_.source();
    const std::string_view map_connection = map_source == MapTileSource::online
        ? "MAP ONLINE"
        : map_source == MapTileSource::cache ? "MAP CACHED" : "MAP LOADING";
    if (!compact) {
        draw_text(right - 318, top - 35, map_connection,
                  map_source == MapTileSource::online ? connected : text_muted);
        draw_text(right - 206, top - 35, telemetry.available ? "LIVE DATA" : "WAITING",
                  telemetry.available ? connected : text_muted);
        draw_text(right - 92, top - 35, utc_time(), text_muted);
    }

    const auto& items = navigation_items();
    if (!compact) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            const int local_top = navigation_top + static_cast<int>(index) * (navigation_item_height + navigation_item_gap);
            const int item_top = top - local_top;
            const bool selected = items[index].page == ui_model_.active_page();
            const bool hovered = hover_cursor_active && point_in_rectangle(
                hover_cursor_x, hover_cursor_y, left + 12, item_top,
                left + sidebar_width - 12, item_top - navigation_item_height);
            if (selected || hovered) {
                draw_rectangle(left + 12, item_top, left + sidebar_width - 12,
                                item_top - navigation_item_height,
                               selected ? active_navigation : card);
            }
            if (selected || hovered) {
                draw_rectangle(left + 12, item_top, left + 16,
                               item_top - navigation_item_height,
                               hovered ? hover_accent : connected);
            }
            draw_shadowed_text(left + 28, item_top - 28, items[index].label,
                               selected || hovered ? text_primary : text_muted);
        }
    }

    const int content_left = content_left_for(left, right);
    const int content_top = top - status_bar_height - 38;
    if (page_scroll_page_ != ui_model_.active_page()) {
        page_scroll_page_ = ui_model_.active_page();
        page_scroll_pixels_ = 0;
    }
    int scroll_limit = page_scroll_limit(ui_model_.active_page(), content_top, bottom);
    if (ui_model_.active_page() == EfbPage::flight_plan) {
        const int viewport_height = std::max(120, content_top - bottom - 28);
        const int route_height = 126 +
            static_cast<int>(complete_flight_plan_legs(
                flight_plan_model_.snapshot()).size()) * 54;
        scroll_limit = std::max(scroll_limit, route_height - viewport_height);
    }
    page_scroll_pixels_ = std::clamp(page_scroll_pixels_, 0, scroll_limit);
    const int scrolled_content_top = content_top + page_scroll_pixels_;
    draw_rectangle(content_left - 18, content_top + 22, right - 10, bottom + 10, sidebar);
    draw_rectangle(content_left - 14, content_top + 18, right - 14, bottom + 14,
                   Color{0.095F, 0.125F, 0.150F, 1.0F});
    const int saved_clip_left = text_clip_left;
    const int saved_clip_top = text_clip_top;
    const int saved_clip_right = text_clip_right;
    const int saved_clip_bottom = text_clip_bottom;
    text_clip_left = content_left - 10;
    text_clip_top = content_top + 18;
    text_clip_right = right - 14;
    text_clip_bottom = bottom + 14;
    draw_page_content(ui_model_.active_page(), telemetry, flight_plan_model_.snapshot(),
                      flight_plan_editor_,
                      route_file_message_,
                      airport_query_, airport_info_model_.snapshot(),
                      planning_model_.snapshot(), briefing_model_,
                      route_progress_model_.snapshot(),
                      weather_model_.snapshot(), flight_log_model_.snapshot(), airspace_model_.snapshot(),
                      navigation_database_model_.snapshot(), traffic_model_.snapshot(),
                      moving_map_model_, map_tiles_, map_pois_,
                      map_hit_targets_, poi_hit_targets_, traffic_hit_targets_,
                      hovered_map_poi_, hovered_map_target_, selected_traffic_key_,
                      map_filters_open_, map_filter_scroll_,
                      show_map_aircraft_, show_map_aircraft_info_,
                      show_map_route_, show_map_labels_, map_marker_scale_,
                      flight_plan_expanded_,
                      selected_log_entry_,
                      show_weather_forecast_,
                      display_preferences_,
                      mobile_server_.status(),
                      content_left, scrolled_content_top, right, bottom);
    text_clip_left = saved_clip_left;
    text_clip_top = saved_clip_top;
    text_clip_right = saved_clip_right;
    text_clip_bottom = saved_clip_bottom;
    if (scroll_limit > 0) {
        const int rail_right = right - 17;
        const int rail_left = rail_right - 5;
        const int rail_top = content_top + 8;
        const int rail_bottom = bottom + 22;
        draw_rectangle(rail_left, rail_top, rail_right, rail_bottom, status_bar);
        const int travel = std::max(8, rail_top - rail_bottom - 34);
        const int thumb_top = rail_top - static_cast<int>(
            static_cast<double>(page_scroll_pixels_) / scroll_limit * travel);
        draw_rectangle(rail_left, thumb_top, rail_right, thumb_top - 34, accent);
    }
    if (compact && navigation_open_) {
        const int menu_left = left + 8;
        const int menu_right = right - 8;
        const int menu_top = top - status_bar_height - 4;
        const int column_width = (menu_right - menu_left - 6) / 2;
        const int row_height = 38;
        draw_rectangle(menu_left, menu_top, menu_right, menu_top - 5 * (row_height + 4) - 8,
                       status_bar);
        for (std::size_t index = 0; index < items.size(); ++index) {
            const int column = static_cast<int>(index / 5);
            const int row = static_cast<int>(index % 5);
            const int item_left = menu_left + 6 + column * column_width;
            const int item_right = item_left + column_width - 6;
            const int item_top = menu_top - 6 - row * (row_height + 4);
            draw_rectangle(item_left, item_top, item_right, item_top - row_height,
                           items[index].page == ui_model_.active_page() ? active_navigation : card);
            draw_text(item_left + 10, item_top - 24, items[index].label, text_primary);
        }
    }
    if (ui_model_.active_page() == EfbPage::home && !map_action_message_.empty()) {
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int toast_left = panel.left + 14;
        const int toast_bottom = panel.bottom + 64;
        const int toast_right = std::min(panel.right - 66, toast_left + 430);
        draw_rectangle(toast_left, toast_bottom + 34, toast_right, toast_bottom, status_bar);
        draw_border(toast_left, toast_bottom + 34, toast_right, toast_bottom, connected, 1.0F);
        draw_text(toast_left + 12, toast_bottom + 12, shortened(map_action_message_, 56), connected);
    }
    if (pending_map_navigation_) {
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int map_left = panel.left + 3;
        const int map_right = panel.right - 3;
        const int map_top = panel.tile_top;
        const int map_bottom = panel.bottom + 3;
        const MapTileViewport viewport{
            map_left, map_top, map_right, map_bottom,
            moving_map_model_.center_latitude_degrees(),
            moving_map_model_.center_longitude_degrees(),
            moving_map_model_.range_nm()};
        // A distinct modal layer separates map actions from the base map and
        // page shell. The scrim is intentionally bounded to the map viewport.
        draw_rectangle(map_left, map_top, map_right, map_bottom,
                       Color{0.0F, 0.0F, 0.0F, 0.52F});
        const auto selected_point = map_screen_point(
            moving_map_model_, viewport, pending_map_navigation_->leg.latitude_degrees,
            pending_map_navigation_->leg.longitude_degrees);
        if (selected_point.valid && selected_point.x >= map_left + 12 &&
            selected_point.x <= map_right - 12 && selected_point.y >= map_bottom + 12 &&
            selected_point.y <= map_top - 12) {
            const int selected_x = static_cast<int>(selected_point.x);
            const int selected_y = static_cast<int>(selected_point.y);
            draw_tactical_corners(selected_x - 15, selected_y + 15,
                                  selected_x + 15, selected_y - 15, text_primary, 8);
            draw_tactical_corners(selected_x - 11, selected_y + 11,
                                  selected_x + 11, selected_y - 11, accent, 6);
            draw_line(selected_x - 7, selected_y, selected_x + 7, selected_y, accent, 2.0F);
            draw_line(selected_x, selected_y - 7, selected_x, selected_y + 7, accent, 2.0F);
            if (selected_x < map_right - 110) {
                draw_map_label(selected_x + 18, selected_y + 4, "SELECTED", accent);
            }
        }
        const int dialog_right = panel.right - 18;
        const int dialog_left = std::max(panel.left + 18, dialog_right - 372);
        const int dialog_top = panel.tile_top - 16;
        const int dialog_bottom = dialog_top - 164;
        constexpr Color dialog_surface{0.105F, 0.115F, 0.120F, 1.0F};
        constexpr Color dialog_header{0.020F, 0.330F, 0.075F, 1.0F};
        constexpr Color dialog_text{1.0F, 1.0F, 1.0F, 1.0F};
        constexpr Color dialog_muted{0.76F, 0.88F, 0.93F, 1.0F};
        draw_rectangle(dialog_left - 4, dialog_top + 4, dialog_right + 4,
                       dialog_bottom - 4, sidebar);
        draw_rectangle(dialog_left, dialog_top, dialog_right, dialog_bottom, dialog_surface);
        draw_rectangle(dialog_left, dialog_top, dialog_right, dialog_top - 27, dialog_header);
        draw_border(dialog_left, dialog_top, dialog_right, dialog_bottom, accent, 2.0F);
        draw_tactical_corners(dialog_left, dialog_top, dialog_right, dialog_bottom,
                              Color{0.02F, 0.32F, 0.43F, 1.0F}, 10);
        draw_shadowed_text(dialog_left + 12, dialog_top - 19, "MAP ACTIONS", dialog_text);
        draw_shadowed_text(dialog_left + 14, dialog_top - 50,
                           shortened(pending_map_navigation_->name, 38), dialog_text,
                           xplmFont_Proportional);
        draw_shadowed_text(dialog_left + 14, dialog_top - 74,
                           shortened(pending_map_navigation_->detail, 46), dialog_muted);
        char coordinates[64]{};
        std::snprintf(coordinates, sizeof(coordinates), "%.4f, %.4f",
                      pending_map_navigation_->leg.latitude_degrees,
                      pending_map_navigation_->leg.longitude_degrees);
        draw_shadowed_text(dialog_left + 14, dialog_top - 96, coordinates, dialog_muted);
        draw_button(dialog_left + 14, dialog_bottom + 40,
                    dialog_left + 80, dialog_bottom + 10, "Cancel");
        draw_button(dialog_left + 88, dialog_bottom + 40,
                    dialog_left + 166, dialog_bottom + 10, "Center");
        if (pending_map_navigation_->leg.kind == WaypointKind::airport) {
            draw_button(dialog_left + 174, dialog_bottom + 40,
                        dialog_left + 252, dialog_bottom + 10, "Details");
        }
        if (pending_map_navigation_->existing_route_index < 0) {
            draw_button(dialog_right - 108, dialog_bottom + 40,
                        dialog_right - 14, dialog_bottom + 10, "Add FMS", true);
        } else if (pending_map_navigation_->removable_from_route) {
            draw_button(dialog_right - 108, dialog_bottom + 40,
                        dialog_right - 14, dialog_bottom + 10, "Remove", true);
        } else {
            draw_shadowed_text(dialog_right - 112, dialog_bottom + 19,
                               "ROUTE END", hover_accent);
        }
    }
    if (approach_panel_open_ && ui_model_.active_page() == EfbPage::flight_plan) {
        draw_approach_panel(airport_info_model_.snapshot(), flight_plan_model_.snapshot(),
                            approach_destination_endpoint_, selected_approach_identifier_,
                            selected_approach_transition_, approach_transition_picker_open_,
                            excluded_approach_fixes_, approach_scroll_, approach_fix_scroll_,
                            approach_message_,
                            content_left, content_top, right, bottom);
    }
    if (pdf_viewer_.visible()) {
        const int viewer_left = viewer_left_for(left, right);
        const int viewer_right = right - 18;
        const int viewer_top = top - status_bar_height - 14;
        const int viewer_bottom = bottom + 18;
        draw_rectangle(viewer_left, viewer_top, viewer_right, viewer_bottom, status_bar);
        draw_border(viewer_left, viewer_top, viewer_right, viewer_bottom, accent, 2.0F);
        draw_text(viewer_left + 14, viewer_top - 27, shortened(pdf_viewer_.title(), 38), text_primary);
        draw_text(viewer_right - 154, viewer_top - 27, "MOUSE WHEEL TO SCROLL", accent);
        // Documents are presented as a paper page, independent of the dark EFB theme.
        const int document_left = viewer_left + 10;
        const int document_top = viewer_top - 42;
        const int document_right = viewer_right - 10;
        const int document_bottom = viewer_bottom + 54;
        draw_rectangle(document_left, document_top, document_right, document_bottom,
                       Color{1.0F, 1.0F, 1.0F, 1.0F});
        PdfAircraftOverlay chart_aircraft;
        chart_aircraft.available = telemetry.available;
        chart_aircraft.latitude_degrees = telemetry.latitude_degrees;
        chart_aircraft.longitude_degrees = telemetry.longitude_degrees;
        chart_aircraft.heading_degrees = telemetry.true_heading_degrees != 0.0
            ? telemetry.true_heading_degrees : telemetry.heading_degrees;
        chart_aircraft.altitude_feet = telemetry.altitude_feet;
        const auto& approach = flight_plan_model_.snapshot();
        if (approach.active_approach_leg_index >= 0 &&
            approach.active_approach_leg_index <
                static_cast<int>(approach.approach_legs.size())) {
            chart_aircraft.approach_altitude_feet = approach.approach_legs[
                static_cast<std::size_t>(approach.active_approach_leg_index)].altitude_feet;
        }
        pdf_viewer_.draw(document_left, document_top, document_right, document_bottom,
                         chart_aircraft);
        const std::string pdf_status = pdf_viewer_.status();
        if (pdf_status != "PDF page ready") {
            draw_text(document_left + 18, document_top - 28, pdf_status,
                      Color{0.12F, 0.16F, 0.20F, 1.0F});
        }
        draw_button(viewer_left + 12, viewer_bottom + 44, viewer_left + 90, viewer_bottom + 12, "Previous");
        draw_button(viewer_left + 98, viewer_bottom + 44, viewer_left + 162, viewer_bottom + 12, "Next");
        const std::string pages = pdf_viewer_.page_count() > 0
            ? "Page " + std::to_string(pdf_viewer_.page_number()) + " / " +
                  std::to_string(pdf_viewer_.page_count())
            : "Loading page";
        draw_text(viewer_left + 184, viewer_bottom + 23, pages, text_primary);
        draw_button(viewer_right - 76, viewer_bottom + 44, viewer_right - 12, viewer_bottom + 12, "Close");
    }
}

void XPlaneWindow::save_geometry() const {
    preferences_.save_briefing_notes(briefing_model_.notes());
    if (!window_id_) {
        return;
    }
    WindowGeometry geometry;
    XPLMGetWindowGeometry(window_id_, &geometry.left, &geometry.top, &geometry.right, &geometry.bottom);
    preferences_.save_geometry(geometry);
}

void XPlaneWindow::resolve_editor_waypoint(EditorPlacement placement) {
    if (!flight_plan_editor_.active() || flight_plan_editor_.input().empty()) {
        flight_plan_editor_.set_message("Type a waypoint identifier before selecting Add");
        return;
    }
    double latitude = 0.0;
    double longitude = 0.0;
    const int selected = flight_plan_editor_.selected_index();
    if (selected >= 0 && selected < static_cast<int>(flight_plan_editor_.legs().size())) {
        latitude = flight_plan_editor_.legs()[selected].latitude_degrees;
        longitude = flight_plan_editor_.legs()[selected].longitude_degrees;
    } else if (telemetry_model_.snapshot().available) {
        latitude = telemetry_model_.snapshot().latitude_degrees;
        longitude = telemetry_model_.snapshot().longitude_degrees;
    }
    const std::string identifier(flight_plan_editor_.input());
    auto waypoint = xplane_flight_plan_.find_waypoint(identifier, latitude, longitude);
    if (!waypoint) {
        flight_plan_editor_.set_message("Waypoint not found: " + identifier);
        return;
    }
    if (placement != EditorPlacement::enroute && waypoint->kind != WaypointKind::airport) {
        flight_plan_editor_.set_message(
            std::string(placement == EditorPlacement::departure ? "Departure" : "Destination") +
            " must be an airport identifier");
        return;
    }
    bool changed = false;
    std::string action;
    switch (placement) {
    case EditorPlacement::departure:
        changed = flight_plan_editor_.set_departure(std::move(*waypoint));
        action = "Departure set to ";
        break;
    case EditorPlacement::destination:
        changed = flight_plan_editor_.set_destination(std::move(*waypoint));
        action = "Destination set to ";
        break;
    case EditorPlacement::enroute:
        changed = flight_plan_editor_.insert_after_selection(std::move(*waypoint));
        action = "Added enroute waypoint ";
        break;
    }
    if (!changed) {
        flight_plan_editor_.set_message("The draft already contains X-Plane's maximum 100 legs");
        return;
    }
    flight_plan_editor_.set_message(action + identifier);
}

void XPlaneWindow::apply_editor_route() {
    if (!flight_plan_editor_.active()) {
        return;
    }
    if (!flight_plan_editor_.dirty()) {
        flight_plan_editor_.set_message("No draft changes to apply");
        return;
    }
    if (!flight_plan_editor_.source_unchanged(flight_plan_model_.snapshot())) {
        flight_plan_editor_.set_message(
            "X-Plane's route changed while editing - return to Flight Plan and reopen Builder");
        return;
    }
    const auto result = xplane_flight_plan_.apply_route(flight_plan_editor_.legs());
    if (!result.success) {
        flight_plan_editor_.set_message(result.message);
        return;
    }
    flight_plan_editor_.mark_applied(flight_plan_model_.snapshot());
}

void XPlaneWindow::import_latest_route() {
    const auto result = xplane_flight_plan_.import_latest(preferences_.flight_plan_directory());
    route_file_message_ = result.message;
}

void XPlaneWindow::export_current_route() {
    const auto result = xplane_flight_plan_.export_current(preferences_.flight_plan_directory());
    route_file_message_ = result.message;
}

void XPlaneWindow::toggle_high_contrast() {
    display_preferences_.high_contrast = !display_preferences_.high_contrast;
    high_contrast_mode = display_preferences_.high_contrast;
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::toggle_comfort_size() {
    display_preferences_.comfort_size = !display_preferences_.comfort_size;
    preferences_.save_display_preferences(display_preferences_);
    if (!window_id_ || !display_preferences_.comfort_size) return;
    int left{}, top{}, right{}, bottom{};
    XPLMGetWindowGeometry(window_id_, &left, &top, &right, &bottom);
    XPLMSetWindowGeometry(window_id_, left, top, std::max(right, left + 1000),
                          std::min(bottom, top - 700));
}

void XPlaneWindow::toggle_traffic_injection() {
    display_preferences_.inject_traffic = !display_preferences_.inject_traffic;
    traffic_model_.set_injection_requested(display_preferences_.inject_traffic);
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::toggle_3d_traffic() {
    display_preferences_.show_3d_traffic = !display_preferences_.show_3d_traffic;
    traffic_model_.set_visual_traffic_requested(display_preferences_.show_3d_traffic);
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::toggle_map_aircraft_info() {
    show_map_aircraft_info_ = !show_map_aircraft_info_;
    display_preferences_.show_map_aircraft_info = show_map_aircraft_info_;
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::toggle_map_route() {
    show_map_route_ = !show_map_route_;
    display_preferences_.show_map_route = show_map_route_;
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::toggle_map_labels() {
    show_map_labels_ = !show_map_labels_;
    display_preferences_.show_map_labels = show_map_labels_;
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::cycle_marker_scale() {
    map_marker_scale_ = map_marker_scale_ >= 150 ? 75 : map_marker_scale_ + 25;
    display_preferences_.map_marker_scale = map_marker_scale_;
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::adjust_traffic_range(int direction) {
    if (direction == 0) return;
    display_preferences_.traffic_range_nm = std::clamp(
        display_preferences_.traffic_range_nm + (direction > 0 ? 25 : -25), 25, 200);
    traffic_model_.set_online_range_nm(display_preferences_.traffic_range_nm);
    preferences_.save_display_preferences(display_preferences_);
}

void XPlaneWindow::resize_interface(double factor) {
    if (!window_id_ || factor <= 0.0) return;
    int left{}, top{}, right{}, bottom{};
    XPLMGetWindowGeometry(window_id_, &left, &top, &right, &bottom);
    const int width = std::clamp(static_cast<int>(std::lround((right - left) * factor)),
                                 minimum_width, 1600);
    const int height = std::clamp(static_cast<int>(std::lround((top - bottom) * factor)),
                                  minimum_height, 1200);
    XPLMSetWindowGeometry(window_id_, left, top, left + width, top - height);
}

void XPlaneWindow::handle_editor_key(char key, char virtual_key) {
    if (!flight_plan_editor_.active()) {
        return;
    }
    switch (static_cast<unsigned char>(virtual_key)) {
    case XPLM_VK_ESCAPE:
        flight_plan_editor_.cancel();
        XPLMTakeKeyboardFocus(nullptr);
        return;
    case XPLM_VK_BACK:
        flight_plan_editor_.backspace_input();
        return;
    case XPLM_VK_RETURN:
        resolve_editor_waypoint(EditorPlacement::enroute);
        return;
    case XPLM_VK_UP:
        flight_plan_editor_.select(flight_plan_editor_.selected_index() - 1);
        return;
    case XPLM_VK_DOWN:
        flight_plan_editor_.select(flight_plan_editor_.selected_index() + 1);
        return;
    case XPLM_VK_DELETE:
        flight_plan_editor_.remove_selected();
        return;
    default:
        flight_plan_editor_.append_input(key);
        return;
    }
}

void XPlaneWindow::search_airport() {
    if (!airport_query_.empty()) {
        xplane_airport_data_.search(airport_query_);
    }
}

void XPlaneWindow::open_approach_panel(bool destination_endpoint) {
    approach_panel_open_ = true;
    select_approach_endpoint(destination_endpoint);
}

void XPlaneWindow::select_approach_endpoint(bool destination_endpoint) {
    approach_destination_endpoint_ = destination_endpoint;
    selected_approach_identifier_.clear();
    selected_approach_transition_ = 0;
    approach_transition_picker_open_ = false;
    excluded_approach_fixes_.clear();
    approach_scroll_ = 0;
    approach_fix_scroll_ = 0;
    const auto [departure, destination] = briefing_airports(flight_plan_model_.snapshot());
    const std::string airport = destination_endpoint ? destination : departure;
    if (airport.empty()) {
        approach_message_ = destination_endpoint
            ? "Set a destination airport in the active route first"
            : "Set a departure airport in the active route first";
        return;
    }
    approach_message_ = "Loading " + airport + " approaches from installed X-Plane data";
    if (!xplane_airport_data_.search(airport))
        approach_message_ = "Airport procedure service is not available";
}

void XPlaneWindow::apply_selected_approach() {
    const auto airport = airport_info_model_.snapshot();
    const auto* approach = selected_approach(airport, selected_approach_identifier_);
    if (!approach) {
        approach_message_ = "Select an approach before applying";
        return;
    }
    std::string transition;
    if (selected_approach_transition_ > 0 &&
        selected_approach_transition_ <= static_cast<int>(approach->transitions.size())) {
        transition = approach->transitions[
            static_cast<std::size_t>(selected_approach_transition_ - 1)].identifier;
    }
    const auto result = xplane_flight_plan_.apply_approach(
        *approach, std::move(transition), airport.identifier,
        approach_destination_endpoint_, excluded_approach_fixes_);
    approach_message_ = result.message;
    if (result.success && flight_plan_editor_.active()) {
        flight_plan_editor_.mark_applied(flight_plan_model_.snapshot());
        flight_plan_editor_.set_message(result.message + " - edit any leg, then Apply again");
        approach_panel_open_ = false;
        XPLMTakeKeyboardFocus(window_id_);
    }
}

void XPlaneWindow::open_selected_approach_plate() {
    const auto airport = airport_info_model_.snapshot();
    const auto* approach = selected_approach(airport, selected_approach_identifier_);
    if (!approach) {
        approach_message_ = "Select an approach before opening its plate";
        return;
    }
    auto normalized = [](std::string_view value) {
        std::string result;
        for (const unsigned char character : value) {
            if (std::isalnum(character))
                result.push_back(static_cast<char>(std::toupper(character)));
        }
        return result;
    };
    const std::string runway = normalized(approach->runway);
    std::string type;
    if (!approach->identifier.empty()) {
        switch (approach->identifier.front()) {
        case 'I': type = "ILS"; break;
        case 'L': type = "LOC"; break;
        case 'B': type = "LOCBC"; break;
        case 'R': case 'P': type = "RNAV"; break;
        case 'N': type = "NDB"; break;
        case 'D': type = "VORDME"; break;
        case 'S': case 'V': type = "VOR"; break;
        default: break;
        }
    }
    int best_index = -1;
    int best_score = 0;
    for (std::size_t index = 0; index < briefing_model_.library().size(); ++index) {
        const auto& entry = briefing_model_.library()[index];
        if (entry.category != LibraryCategory::chart ||
            !library_entry_matches(entry, airport.identifier)) continue;
        const std::string name = normalized(entry.name);
        int score = 1;
        if (!runway.empty() && name.find(runway) != std::string::npos) score += 8;
        if (!type.empty() && name.find(type) != std::string::npos) score += 6;
        if (name.find(normalized(approach->identifier)) != std::string::npos) score += 4;
        if (score > best_score) {
            best_score = score;
            best_index = static_cast<int>(index);
        }
    }
    const int required_score = runway.empty() ? 6 : 9;
    if (best_index < 0 || best_score < required_score) {
        briefing_library_.refresh();
        approach_message_ = "Matching plate is not downloaded yet; chart refresh started";
        return;
    }
    briefing_model_.select_library_airport(airport.identifier);
    briefing_model_.select_entry(best_index);
    open_briefing_entry();
    approach_message_ = "Opened " + approach->display_name + " plate";
}

void XPlaneWindow::handle_airport_key(char key, char virtual_key) {
    switch (static_cast<unsigned char>(virtual_key)) {
    case XPLM_VK_BACK:
        if (!airport_query_.empty()) airport_query_.pop_back();
        return;
    case XPLM_VK_RETURN:
        search_airport();
        return;
    default:
        break;
    }
    const auto character = static_cast<unsigned char>(key);
    if (airport_query_.size() < 7 && std::isalnum(character)) {
        airport_query_.push_back(static_cast<char>(std::toupper(character)));
    }
}

void XPlaneWindow::handle_briefing_key(char key, char virtual_key) {
    if (briefing_model_.active_tab() != BriefingTab::notes) return;
    switch (static_cast<unsigned char>(virtual_key)) {
    case XPLM_VK_ESCAPE:
        XPLMTakeKeyboardFocus(nullptr);
        return;
    case XPLM_VK_BACK:
        briefing_model_.backspace_note();
        return;
    case XPLM_VK_RETURN:
        briefing_model_.append_note('\n');
        return;
    default:
        briefing_model_.append_note(key);
        return;
    }
}

void XPlaneWindow::open_briefing_entry() {
    const auto* entry = briefing_model_.selected_entry();
    if (!entry) return;
    std::string extension = std::filesystem::path(entry->path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (extension == ".pdf") {
        pdf_viewer_.open(entry->path);
    }
    // TXT and Markdown entries are already rendered in the in-app preview.
}

void XPlaneWindow::draw(XPLMWindowID window_id, void* refcon) {
    if (auto* window = static_cast<XPlaneWindow*>(refcon)) {
        window->render(window_id);
    }
}

int XPlaneWindow::handle_mouse(XPLMWindowID window_id, int x, int y, XPLMMouseStatus status, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window) {
        return 1;
    }

    int left{};
    int top{};
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
    if (status != xplm_MouseDown) {
        if (window->route_width_slider_dragging_) {
            if (window->ui_model_.active_page() == EfbPage::settings &&
                (status == xplm_MouseDrag || status == xplm_MouseUp)) {
                const int content_left = content_left_for(left, right);
                const int card_right = responsive_card_right(content_left, right);
                window->display_preferences_.route_line_width = route_width_from_slider(
                    x, content_left + 18, card_right - 18);
                if (status == xplm_MouseUp) {
                    window->route_width_slider_dragging_ = false;
                    window->preferences_.save_display_preferences(window->display_preferences_);
                }
                return 1;
            }
            window->route_width_slider_dragging_ = false;
        }
        if (window->ui_model_.active_page() == EfbPage::home && window->map_dragging_) {
            const int content_left = content_left_for(left, right);
            const int content_top = top - status_bar_height - 38;
            const auto panel = home_map_geometry(content_left, content_top, right, bottom);
            const int map_left = panel.left + 3;
            const int map_right = panel.right - 3;
            const int map_top = panel.tile_top;
            const int map_bottom = panel.bottom + 3;
            const int center_x = (map_left + map_right) / 2;
            const int center_y = (map_top + map_bottom) / 2;
            const MapTileViewport drag_viewport{
                map_left, map_top, map_right, map_bottom,
                window->map_drag_start_latitude_, window->map_drag_start_longitude_,
                window->moving_map_model_.range_nm()};
            const int delta_x = x - window->map_drag_start_x_;
            const int delta_y = y - window->map_drag_start_y_;
            if (std::abs(delta_x) > 3 || std::abs(delta_y) > 3) {
                window->map_drag_moved_ = true;
            }
            if (status == xplm_MouseDrag && window->map_drag_moved_) {
                const auto coordinate = unproject_map_coordinate(
                    window->moving_map_model_.style(), drag_viewport,
                    center_x - delta_x, center_y - delta_y);
                if (coordinate.valid) {
                    window->moving_map_model_.pan_to(coordinate.latitude_degrees,
                                                     coordinate.longitude_degrees);
                }
                return 1;
            }
            if (status == xplm_MouseUp) {
                if (!window->map_drag_moved_ && x >= map_left && x <= map_right &&
                    y >= map_bottom && y <= map_top) {
                    const MapTileViewport click_viewport{
                        map_left, map_top, map_right, map_bottom,
                        window->moving_map_model_.center_latitude_degrees(),
                        window->moving_map_model_.center_longitude_degrees(),
                        window->moving_map_model_.range_nm()};
                    const auto coordinate = unproject_map_coordinate(
                        window->moving_map_model_.style(), click_viewport, x, y);
                    if (coordinate.valid) {
                        FlightPlanLeg point;
                        point.identifier = "MAP POINT";
                        point.kind = WaypointKind::coordinate;
                        point.latitude_degrees = coordinate.latitude_degrees;
                        point.longitude_degrees = coordinate.longitude_degrees;
                        window->pending_map_navigation_ = map_navigation_for(
                            window->flight_plan_model_.snapshot(), std::move(point),
                            "Map point", "Custom map coordinate");
                    }
                }
                window->map_dragging_ = false;
                window->map_drag_moved_ = false;
                return 1;
            }
        }
        return 1;
    }
    if (compact_layout(left, right)) {
        if (y <= top - 10 && y >= top - 44) {
            if (x >= right - 146 && x <= right - 116) {
                window->resize_interface(0.87);
                return 1;
            }
            if (x >= right - 112 && x <= right - 82) {
                window->resize_interface(1.15);
                return 1;
            }
        }
        if (x >= right - 78 && x <= right - 8 && y <= top - 10 && y >= top - 44) {
            window->navigation_open_ = !window->navigation_open_;
            return 1;
        }
        if (window->navigation_open_) {
            const auto& items = navigation_items();
            const int menu_left = left + 8;
            const int menu_right = right - 8;
            const int menu_top = top - status_bar_height - 4;
            const int column_width = (menu_right - menu_left - 6) / 2;
            constexpr int row_height = 38;
            for (std::size_t index = 0; index < items.size(); ++index) {
                const int column = static_cast<int>(index / 5);
                const int row = static_cast<int>(index % 5);
                const int item_left = menu_left + 6 + column * column_width;
                const int item_right = item_left + column_width - 6;
                const int item_top = menu_top - 6 - row * (row_height + 4);
                if (point_in_rectangle(x, y, item_left, item_top,
                                       item_right, item_top - row_height)) {
                    window->ui_model_.select_page(items[index].page);
                    window->navigation_open_ = false;
                    return 1;
                }
            }
            window->navigation_open_ = false;
            return 1;
        }
    }
    if (window->pdf_viewer_.visible()) {
        const int viewer_left = viewer_left_for(left, right);
        const int viewer_right = right - 18;
        const int viewer_bottom = bottom + 18;
        if (y <= viewer_bottom + 44 && y >= viewer_bottom + 12) {
            if (x >= viewer_left + 12 && x <= viewer_left + 90) {
                window->pdf_viewer_.previous_page();
                return 1;
            }
            if (x >= viewer_left + 98 && x <= viewer_left + 162) {
                window->pdf_viewer_.next_page();
                return 1;
            }
            if (x >= viewer_right - 76 && x <= viewer_right - 12) {
                window->pdf_viewer_.close();
                return 1;
            }
        }
        return 1;
    }
    if (window->approach_panel_open_ &&
        window->ui_model_.active_page() == EfbPage::flight_plan) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        const int panel_right = responsive_card_right(content_left, right, 420);
        const int panel_bottom = bottom + 22;
        const int endpoint_width = std::max(100, (panel_right - content_left - 38) / 2);
        if (y <= content_top - 42 && y >= content_top - 76) {
            if (x >= content_left + 14 && x <= content_left + 14 + endpoint_width) {
                window->select_approach_endpoint(false);
                return 1;
            }
            if (x >= content_left + 24 + endpoint_width && x <= panel_right - 14) {
                window->select_approach_endpoint(true);
                return 1;
            }
        }
        const int actions_top = panel_bottom + 52;
        const int actions_bottom = panel_bottom + 18;
        const int available_width = panel_right - content_left - 38;
        const int button_width = std::max(72, (available_width - 12) / 3);
        if (y <= actions_top && y >= actions_bottom) {
            if (x >= content_left + 14 && x <= content_left + 14 + button_width) {
                window->approach_panel_open_ = false;
                if (window->flight_plan_editor_.active()) XPLMTakeKeyboardFocus(window_id);
                return 1;
            }
            if (x >= content_left + 20 + button_width &&
                x <= content_left + 20 + button_width * 2) {
                window->open_selected_approach_plate();
                return 1;
            }
            if (x >= content_left + 26 + button_width * 2 && x <= panel_right - 14) {
                window->apply_selected_approach();
                return 1;
            }
        }
        const auto airport = window->airport_info_model_.snapshot();
        const int list_top = content_top - 113;
        const int list_bottom = actions_top + 12;
        const bool compact_panel = panel_right - content_left < 520;
        const int list_right = compact_panel ? panel_right - 14
            : content_left + (panel_right - content_left) * 47 / 100;
        const auto* selected = selected_approach(airport, window->selected_approach_identifier_);
        if (window->approach_transition_picker_open_ && selected) {
            int transition_top = list_top - 34;
            if (x >= content_left + 14 && x <= list_right &&
                y <= transition_top && y >= transition_top - 34) {
                window->selected_approach_transition_ = 0;
                window->approach_transition_picker_open_ = false;
                window->approach_scroll_ = 0;
                window->approach_fix_scroll_ = 0;
                window->excluded_approach_fixes_.clear();
                window->approach_message_ = "Vectors/common approach legs selected";
                return 1;
            }
            transition_top -= 40;
            const std::size_t transition_visible = static_cast<std::size_t>(
                std::max(1, (transition_top - list_bottom) / 40));
            const std::size_t transition_start = std::min<std::size_t>(
                std::max(0, window->approach_scroll_),
                selected->transitions.size() > transition_visible
                    ? selected->transitions.size() - transition_visible : 0);
            for (std::size_t offset = 0; offset < transition_visible &&
                 transition_start + offset < selected->transitions.size(); ++offset) {
                const std::size_t index = transition_start + offset;
                if (x >= content_left + 14 && x <= list_right &&
                    y <= transition_top && y >= transition_top - 34) {
                    window->selected_approach_transition_ = static_cast<int>(index + 1);
                    window->approach_transition_picker_open_ = false;
                    window->approach_scroll_ = 0;
                    window->approach_fix_scroll_ = 0;
                    window->excluded_approach_fixes_.clear();
                    window->approach_message_ = "Transition selected: " +
                        selected->transitions[index].identifier;
                    return 1;
                }
                transition_top -= 40;
            }
            return 1;
        }
        const std::size_t visible = std::min(
            approach_visible_count(list_top, list_bottom),
            airport.procedures.approach_details.size());
        const std::size_t maximum_start = airport.procedures.approach_details.size() > visible
            ? airport.procedures.approach_details.size() - visible : 0;
        const std::size_t start = std::min<std::size_t>(
            std::max(0, window->approach_scroll_), maximum_start);
        int row_top = list_top;
        for (std::size_t offset = 0; offset < visible; ++offset) {
            if (x >= content_left + 14 && x <= list_right &&
                y <= row_top && y >= row_top - 36) {
                window->selected_approach_identifier_ =
                    airport.procedures.approach_details[start + offset].identifier;
                const auto now = std::chrono::steady_clock::now();
                const bool double_click = window->last_clicked_approach_identifier_ ==
                        window->selected_approach_identifier_ &&
                    now - window->last_approach_click_ <= std::chrono::milliseconds(450);
                window->last_clicked_approach_identifier_ = window->selected_approach_identifier_;
                window->last_approach_click_ = now;
                window->selected_approach_transition_ = 0;
                window->excluded_approach_fixes_.clear();
                window->approach_fix_scroll_ = 0;
                window->approach_transition_picker_open_ = double_click;
                if (double_click) window->approach_scroll_ = 0;
                window->approach_message_ = double_click
                    ? "Select an available transition"
                    : "Double-click this approach to choose its transition";
                return 1;
            }
            row_top -= 42;
        }
        if (!compact_panel && selected) {
            const int detail_left = list_right + 14;
            const auto fixes = approach_fix_names(*selected, window->selected_approach_transition_);
            int fix_y = list_top - 134;
            const std::size_t fix_visible = static_cast<std::size_t>(std::max(
                1, (fix_y - (list_bottom + 38)) / 30 + 1));
            const std::size_t fix_start = std::min<std::size_t>(
                std::max(0, window->approach_fix_scroll_),
                fixes.size() > fix_visible ? fixes.size() - fix_visible : 0);
            for (std::size_t offset = 0; offset < fix_visible &&
                 fix_start + offset < fixes.size(); ++offset) {
                const auto& fix = fixes[fix_start + offset];
                if (fix_y - 18 < list_bottom + 38) break;
                if (x >= detail_left + 12 && x <= panel_right - 26 &&
                    y <= fix_y + 8 && y >= fix_y - 18) {
                    auto& excluded = window->excluded_approach_fixes_;
                    const auto found = std::find(excluded.begin(), excluded.end(), fix.key);
                    if (found == excluded.end()) {
                        excluded.push_back(fix.key);
                        window->approach_message_ = "Removed " + fix.label + " from the pending procedure";
                    } else {
                        excluded.erase(found);
                        window->approach_message_ = "Restored " + fix.label + " to the pending procedure";
                    }
                    return 1;
                }
                fix_y -= 30;
            }
        }
        return 1;
    }
    if (window->pending_map_navigation_) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int dialog_right = panel.right - 18;
        const int dialog_left = std::max(panel.left + 18, dialog_right - 372);
        const int dialog_top = panel.tile_top - 16;
        const int dialog_bottom = dialog_top - 164;
        if (y <= dialog_bottom + 40 && y >= dialog_bottom + 10) {
            if (x >= dialog_left + 14 && x <= dialog_left + 80) {
                window->pending_map_navigation_.reset();
                return 1;
            }
            if (x >= dialog_left + 88 && x <= dialog_left + 166) {
                window->moving_map_model_.pan_to(
                    window->pending_map_navigation_->leg.latitude_degrees,
                    window->pending_map_navigation_->leg.longitude_degrees);
                window->map_action_message_ = "Map centered on selected point";
                window->pending_map_navigation_.reset();
                return 1;
            }
            if (window->pending_map_navigation_->leg.kind == WaypointKind::airport &&
                x >= dialog_left + 174 && x <= dialog_left + 252) {
                window->airport_query_ = window->pending_map_navigation_->leg.identifier;
                window->xplane_airport_data_.search(window->airport_query_);
                window->ui_model_.select_page(EfbPage::airports);
                window->pending_map_navigation_.reset();
                return 1;
            }
            if (x >= dialog_right - 108 && x <= dialog_right - 14 &&
                (window->pending_map_navigation_->existing_route_index < 0 ||
                 window->pending_map_navigation_->removable_from_route)) {
                const auto result = window->pending_map_navigation_->removable_from_route
                    ? window->xplane_flight_plan_.remove_route_leg(
                          static_cast<std::size_t>(
                              window->pending_map_navigation_->existing_route_index),
                          window->pending_map_navigation_->name)
                    : window->xplane_flight_plan_.insert_after_active(
                          window->pending_map_navigation_->leg,
                          window->pending_map_navigation_->name);
                window->map_action_message_ = result.message;
                window->pending_map_navigation_.reset();
                return 1;
            }
        }
        return 1;
    }
    if (window->ui_model_.active_page() == EfbPage::logbook) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        const int card_right = responsive_card_right(content_left, right, 420);
        int row_top = content_top - 214;
        const auto& entries = window->flight_log_model_.snapshot().entries;
        for (std::size_t index = 0; index < entries.size() && index < 6; ++index) {
            if (x >= content_left && x <= card_right && y <= row_top && y >= row_top - 42) {
                const int selected = static_cast<int>(index);
                window->selected_log_entry_ = window->selected_log_entry_ == selected ? -1 : selected;
                return 1;
            }
            row_top -= 46;
        }
    }
    if (window->ui_model_.active_page() == EfbPage::briefing) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        int tab_left = content_left;
        if (y <= content_top - 45 && y >= content_top - 78) {
            for (const auto& [tab, label] : briefing_tabs) {
                static_cast<void>(label);
                if (x >= tab_left && x <= tab_left + 92) {
                    window->briefing_model_.select_tab(tab);
                    if (tab == BriefingTab::library &&
                        window->briefing_model_.library_airport().empty()) {
                        const auto [departure, destination] =
                            briefing_airports(window->flight_plan_model_.snapshot());
                        static_cast<void>(destination);
                        if (!departure.empty()) select_library_airport(window->briefing_model_, departure);
                    }
                    XPLMTakeKeyboardFocus(tab == BriefingTab::notes ? window_id : nullptr);
                    return 1;
                }
                tab_left += 98;
            }
        }
        const int card_right = responsive_card_right(content_left, right, 420);
        if (window->briefing_model_.active_tab() == BriefingTab::library) {
            const auto [departure, destination] = briefing_airports(window->flight_plan_model_.snapshot());
            const bool compact_library = card_right - content_left < 500;
            if (compact_library) {
                const int width = (card_right - content_left - 12) / 3;
                if (y <= content_top - 98 && y >= content_top - 131) {
                    if (x >= content_left && x <= content_left + width && !departure.empty())
                        select_library_airport(window->briefing_model_, departure);
                    else if (x >= content_left + width + 6 &&
                             x <= content_left + width * 2 + 6 && !destination.empty())
                        select_library_airport(window->briefing_model_, destination);
                    else if (x >= content_left + width * 2 + 12 && x <= card_right)
                        window->briefing_library_.refresh();
                    return 1;
                }
                if (y <= content_top - 137 && y >= content_top - 170) {
                    if (x >= content_left && x <= content_left + width)
                        window->open_briefing_entry();
                    else if (x >= content_left + width + 6 &&
                             x <= content_left + width * 2 + 6)
                        static_cast<void>(select_library_offset(window->briefing_model_, -1));
                    else if (x >= content_left + width * 2 + 12 && x <= card_right)
                        static_cast<void>(select_library_offset(window->briefing_model_, 1));
                    return 1;
                }
            } else if (y <= content_top - 98 && y >= content_top - 131) {
                if (x >= content_left && x <= content_left + 90 && !departure.empty()) {
                    select_library_airport(window->briefing_model_, departure);
                    return 1;
                }
                if (x >= content_left + 96 && x <= content_left + 186 && !destination.empty()) {
                    select_library_airport(window->briefing_model_, destination);
                    return 1;
                }
                if (x >= content_left + 192 && x <= content_left + 270) {
                    window->briefing_library_.refresh();
                    return 1;
                }
                if (x >= content_left + 276 && x <= content_left + 342) {
                    window->open_briefing_entry();
                    return 1;
                }
                if (x >= content_left + 348 && x <= content_left + 398) {
                    static_cast<void>(select_library_offset(window->briefing_model_, -1));
                    return 1;
                }
                if (x >= content_left + 404 && x <= content_left + 462) {
                    static_cast<void>(select_library_offset(window->briefing_model_, 1));
                    return 1;
                }
            }
            const int list_right = std::min(content_left + 230, card_right - 220);
            constexpr int row_height = 34;
            const int row_start = content_top - (compact_library ? 197 : 158);
            const auto indices = filtered_library_indices(window->briefing_model_);
            const std::size_t visible = library_visible_count(
                row_start, bottom, indices.size());
            const std::size_t start = library_visible_start(window->briefing_model_, indices, visible);
            for (std::size_t offset = 0; offset < visible; ++offset) {
                const int row_top = row_start - static_cast<int>(offset) * (row_height + 4);
                if (x >= content_left && x <= list_right && y <= row_top && y >= row_top - row_height) {
                    window->briefing_model_.select_entry(static_cast<int>(indices[start + offset]));
                    return 1;
                }
            }
        } else if (window->briefing_model_.active_tab() == BriefingTab::checklist) {
            const int phase_width = std::max(88, (card_right - content_left - 12) / 3);
            if (y <= content_top - 96 && y >= content_top - 130) {
                if (x >= content_left && x <= content_left + phase_width)
                    window->briefing_model_.select_checklist_phase(ChecklistPhase::preflight);
                else if (x >= content_left + phase_width + 6 &&
                         x <= content_left + phase_width * 2 + 6)
                    window->briefing_model_.select_checklist_phase(ChecklistPhase::takeoff_cruise);
                else if (x >= content_left + phase_width * 2 + 12 && x <= card_right)
                    window->briefing_model_.select_checklist_phase(ChecklistPhase::descent_landing);
                return 1;
            }
            if (x >= card_right - 78 && x <= card_right &&
                y <= content_top - 139 && y >= content_top - 172) {
                window->briefing_model_.reset_checklist();
                return 1;
            }
            int row_top = content_top - 188;
            for (std::size_t index = 0; index < window->briefing_model_.checklist().size(); ++index) {
                if (x >= content_left && x <= card_right && y <= row_top && y >= row_top - 40) {
                    window->briefing_model_.toggle_checklist_item(index);
                    return 1;
                }
                row_top -= 46;
            }
        } else if (window->briefing_model_.active_tab() == BriefingTab::notes) {
            if (x >= card_right - 72 && x <= card_right &&
                y <= content_top - 98 && y >= content_top - 131) {
                window->briefing_model_.clear_notes();
                return 1;
            }
            if (x >= content_left && x <= card_right &&
                y <= content_top - 146 && y >= bottom + 46) {
                XPLMTakeKeyboardFocus(window_id);
                return 1;
            }
        }
    }
    if (window->ui_model_.active_page() == EfbPage::planning) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        if (y <= content_top - 293 && y >= content_top - 326) {
            if (x >= content_left && x <= content_left + 58) {
                window->planning_model_.adjust_reserve_minutes(-5);
                return 1;
            }
            if (x >= content_left + 190 && x <= content_left + 248) {
                window->planning_model_.adjust_reserve_minutes(5);
                return 1;
            }
        }
    }
    if (window->ui_model_.active_page() == EfbPage::weather) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        const int card_right = responsive_card_right(content_left, right);
        if (y <= content_top + 8 && y >= content_top - 23) {
            if (x >= card_right - 190 && x <= card_right - 98) {
                window->show_weather_forecast_ = false;
                return 1;
            }
            if (x >= card_right - 92 && x <= card_right) {
                window->show_weather_forecast_ = true;
                return 1;
            }
        }
    }
    if (window->ui_model_.active_page() == EfbPage::airports) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        const int card_right = responsive_card_right(content_left, right, 420);
        if (x >= content_left && x <= card_right &&
            y <= content_top - 45 && y >= content_top - 80) {
            XPLMTakeKeyboardFocus(window_id);
            if (x >= card_right - 78) window->search_airport();
            return 1;
        }
    }
    if (window->ui_model_.active_page() == EfbPage::flight_plan) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        const int card_right = responsive_card_right(content_left, right, 420);
        if (!window->flight_plan_editor_.active()) {
            const int toolbar_left = std::max(content_left + 112, card_right - 306);
            const int toolbar_width = std::max(60, (card_right - toolbar_left - 12) / 3);
            if (y <= content_top + 8 && y >= content_top - 23) {
                if (x >= toolbar_left && x <= toolbar_left + toolbar_width) {
                    window->import_latest_route();
                    return 1;
                }
                if (x >= toolbar_left + toolbar_width + 6 &&
                    x <= toolbar_left + toolbar_width * 2 + 6) {
                    window->export_current_route();
                    return 1;
                }
            }
            if (x >= toolbar_left + toolbar_width * 2 + 12 && x <= card_right &&
                y <= content_top + 8 && y >= content_top - 23 &&
                window->flight_plan_editor_.begin(window->flight_plan_model_.snapshot())) {
                XPLMTakeKeyboardFocus(window_id);
                return 1;
            }
        } else {
            if (x >= card_right - 108 && x <= card_right &&
                y <= content_top + 8 && y >= content_top - 25) {
                window->open_approach_panel(true);
                XPLMTakeKeyboardFocus(nullptr);
                return 1;
            }
            XPLMTakeKeyboardFocus(window_id);
            if (y <= content_top - 87 && y >= content_top - 122) {
                if (x >= card_right - 226 && x <= card_right - 156) {
                    window->resolve_editor_waypoint(EditorPlacement::departure);
                    return 1;
                }
                if (x >= card_right - 148 && x <= card_right - 78) {
                    window->resolve_editor_waypoint(EditorPlacement::destination);
                    return 1;
                }
                if (x >= card_right - 70 && x <= card_right) {
                    window->resolve_editor_waypoint(EditorPlacement::enroute);
                    return 1;
                }
            }
            if (y <= content_top - 134 && y >= content_top - 165) {
                if (x >= content_left && x <= content_left + 56) {
                    window->flight_plan_editor_.move_selected_up();
                    return 1;
                }
                if (x >= content_left + 64 && x <= content_left + 132) {
                    window->flight_plan_editor_.move_selected_down();
                    return 1;
                }
                if (x >= content_left + 140 && x <= content_left + 222) {
                    window->flight_plan_editor_.remove_selected();
                    return 1;
                }
                if (x >= card_right - 158 && x <= card_right - 84) {
                    window->flight_plan_editor_.cancel();
                    XPLMTakeKeyboardFocus(nullptr);
                    return 1;
                }
                if (x >= card_right - 76 && x <= card_right) {
                    window->apply_editor_route();
                    return 1;
                }
            }
            constexpr int row_height = 42;
            constexpr int row_gap = 5;
            const int row_start = content_top - 180;
            const int available_height = std::max(row_height, row_start - (bottom + 48));
            const std::size_t visible_count = std::min<std::size_t>(
                window->flight_plan_editor_.legs().size(),
                static_cast<std::size_t>(std::max(1, available_height / (row_height + row_gap))));
            const std::size_t start = editor_visible_start(window->flight_plan_editor_, visible_count);
            int row_top = row_start;
            for (std::size_t offset = 0; offset < visible_count; ++offset) {
                if (x >= content_left && x <= card_right &&
                    y <= row_top && y >= row_top - row_height) {
                    window->flight_plan_editor_.select(static_cast<int>(start + offset));
                    return 1;
                }
                row_top -= row_height + row_gap;
            }
        }
    }
    if (window->ui_model_.active_page() == EfbPage::settings) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38 + window->page_scroll_pixels_;
        const int card_right = responsive_card_right(content_left, right);
        if (x >= content_left + 12 && x <= card_right - 12 &&
            y <= content_top - 590 && y >= content_top - 618) {
            window->route_width_slider_dragging_ = true;
            window->display_preferences_.route_line_width = route_width_from_slider(
                x, content_left + 18, card_right - 18);
            window->preferences_.save_display_preferences(window->display_preferences_);
            return 1;
        }
        if (x >= card_right - 122 && x <= card_right - 18) {
            if (y <= content_top - 91 && y >= content_top - 124) {
                window->toggle_traffic_injection();
                return 1;
            }
            if (y <= content_top - 207 && y >= content_top - 240) {
                window->toggle_3d_traffic();
                return 1;
            }
            if (y <= content_top - 323 && y >= content_top - 356) {
                window->toggle_map_aircraft_info();
                return 1;
            }
            if (y <= content_top - 439 && y >= content_top - 472) {
                window->toggle_map_route();
                return 1;
            }
            if (y <= content_top - 671 && y >= content_top - 704) {
                window->toggle_map_labels();
                return 1;
            }
            if (y <= content_top - 787 && y >= content_top - 820) {
                window->cycle_marker_scale();
                return 1;
            }
            if (y <= content_top - 903 && y >= content_top - 936) {
                window->toggle_high_contrast();
                return 1;
            }
            if (y <= content_top - 1019 && y >= content_top - 1052) {
                window->toggle_comfort_size();
                return 1;
            }
        }
    }
    if (window->ui_model_.active_page() == EfbPage::home) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int topo_right = panel.right - 10;
        const int topo_left = topo_right - 64;
        const int street_right = topo_left - 6;
        const int street_left = street_right - 70;
        if (y <= panel.top - 7 && y >= panel.top - 32) {
            const int filter_left = filter_button_left(panel);
            if (x >= filter_left && x <= filter_left + filter_button_width) {
                window->map_filters_open_ = !window->map_filters_open_;
                return 1;
            }
            if (x >= street_left && x <= street_right) {
                window->moving_map_model_.select_style(MapStyle::street);
                return 1;
            }
            if (x >= topo_left && x <= topo_right) {
                window->moving_map_model_.select_style(MapStyle::topographic);
                return 1;
            }
        }
        const int map_left = panel.left + 3;
        const int map_right = panel.right - 3;
        const int map_top = panel.tile_top;
        const int map_bottom = panel.bottom + 3;
        if (window->map_filters_open_) {
            const auto filters = map_filter_geometry(map_left, map_top, map_right, map_bottom);
            window->map_filter_scroll_ =
                std::clamp(window->map_filter_scroll_, 0, filters.max_scroll);
            if (point_in_rectangle(x, y, filters.left, filters.top,
                                   filters.right, filters.bottom)) {
                constexpr int step = 25;
                for (int row = 0; row < 10; ++row) {
                    const int row_top = filters.first_row_top +
                        window->map_filter_scroll_ * step - row * step;
                    if (row_top > filters.first_row_top + 1 ||
                        row_top - 22 < filters.bottom + 6) continue;
                    if (!point_in_rectangle(x, y, filters.left + 8, row_top,
                                            filters.right - 8, row_top - 22)) continue;
                    switch (row) {
                    case 0: window->show_map_aircraft_ = !window->show_map_aircraft_; break;
                    case 1: window->toggle_map_aircraft_info(); break;
                    case 2: window->toggle_map_route(); break;
                    case 3: window->toggle_map_labels(); break;
                    case 4: window->moving_map_model_.toggle_layer(MapLayer::weather); break;
                    case 5: window->moving_map_model_.toggle_layer(MapLayer::airports); break;
                    case 6: window->moving_map_model_.toggle_layer(MapLayer::navaids); break;
                    case 7: window->moving_map_model_.toggle_layer(MapLayer::airspace); break;
                    case 8: window->moving_map_model_.toggle_layer(MapLayer::traffic); break;
                    case 9: window->moving_map_model_.toggle_pois(); break;
                    default: break;
                    }
                    return 1;
                }
                const int scale_top = filters.first_row_top +
                    window->map_filter_scroll_ * step - 10 * step;
                if (scale_top <= filters.first_row_top + 1 &&
                    scale_top - 22 >= filters.bottom + 6 &&
                    y <= scale_top && y >= scale_top - 24) {
                    if (x >= filters.right - 68 && x <= filters.right - 42) {
                        window->map_marker_scale_ = std::max(75, window->map_marker_scale_ - 25);
                        window->display_preferences_.map_marker_scale = window->map_marker_scale_;
                        window->preferences_.save_display_preferences(window->display_preferences_);
                        return 1;
                    }
                    if (x >= filters.right - 38 && x <= filters.right - 12) {
                        window->map_marker_scale_ = std::min(150, window->map_marker_scale_ + 25);
                        window->display_preferences_.map_marker_scale = window->map_marker_scale_;
                        window->preferences_.save_display_preferences(window->display_preferences_);
                        return 1;
                    }
                }
                const int range_top = filters.first_row_top +
                    window->map_filter_scroll_ * step - 11 * step;
                if (range_top <= filters.first_row_top + 1 &&
                    range_top - 22 >= filters.bottom + 6 &&
                    y <= range_top && y >= range_top - 24) {
                    if (x >= filters.right - 68 && x <= filters.right - 42) {
                        window->adjust_traffic_range(-1);
                        return 1;
                    }
                    if (x >= filters.right - 38 && x <= filters.right - 12) {
                        window->adjust_traffic_range(1);
                        return 1;
                    }
                }
                return 1;
            }
            window->map_filters_open_ = false;
            return 1;
        }
        const int utility_right = map_right - 10;
        const int utility_left = utility_right - 42;
        int utility_top = map_top - 10;
        if (x >= utility_left && x <= utility_right) {
            if (y <= utility_top && y >= utility_top - 30) {
                window->moving_map_model_.zoom_in();
                return 1;
            }
            utility_top -= 34;
            if (y <= utility_top && y >= utility_top - 30) {
                window->moving_map_model_.zoom_out();
                return 1;
            }
            utility_top -= 34;
            if (y <= utility_top && y >= utility_top - 30) {
                const auto& legs = window->flight_plan_model_.snapshot().legs;
                if (!legs.empty()) {
                    window->moving_map_model_.pan_to(legs.front().latitude_degrees,
                                                     legs.front().longitude_degrees);
                    window->map_action_message_ = "Centered on departure";
                } else {
                    window->map_action_message_ = "No departure is loaded";
                }
                return 1;
            }
            utility_top -= 34;
            if (y <= utility_top && y >= utility_top - 30) {
                const auto& legs = window->flight_plan_model_.snapshot().legs;
                if (!legs.empty()) {
                    window->moving_map_model_.pan_to(legs.back().latitude_degrees,
                                                     legs.back().longitude_degrees);
                    window->map_action_message_ = "Centered on arrival";
                } else {
                    window->map_action_message_ = "No arrival is loaded";
                }
                return 1;
            }
        }
        if (x >= map_right - 52 && x <= map_right - 10 &&
            y >= map_bottom + 62 && y <= map_bottom + 98) {
            window->moving_map_model_.recenter_on_aircraft();
            window->map_action_message_.clear();
            return 1;
        }
        const int marker_hit_radius = std::max(10, window->map_marker_scale_ * 10 / 100);
        for (const auto& target : window->traffic_hit_targets_) {
            if (std::abs(x - target.x) <= marker_hit_radius + 3 &&
                std::abs(y - target.y) <= marker_hit_radius + 3) {
                if (window->selected_traffic_key_ &&
                    *window->selected_traffic_key_ == target.key) {
                    window->selected_traffic_key_.reset();
                } else {
                    window->selected_traffic_key_ = target.key;
                    window->traffic_model_.request_route_lookup(target.callsign);
                }
                window->pending_map_navigation_.reset();
                window->map_action_message_.clear();
                return 1;
            }
        }
        for (const auto& target : window->poi_hit_targets_) {
            if (std::abs(x - target.x) <= marker_hit_radius &&
                std::abs(y - target.y) <= marker_hit_radius) {
                FlightPlanLeg leg;
                leg.identifier = "POI";
                leg.kind = WaypointKind::coordinate;
                leg.latitude_degrees = target.poi.latitude_degrees;
                leg.longitude_degrees = target.poi.longitude_degrees;
                window->pending_map_navigation_ = map_navigation_for(
                    window->flight_plan_model_.snapshot(), std::move(leg), target.poi.name,
                    std::string(poi_category_label(target.poi.category)) + "  /  " +
                        target.poi.detail);
                window->map_action_message_.clear();
                return 1;
            }
        }
        for (const auto& target : window->map_hit_targets_) {
            if (std::abs(x - target.x) <= marker_hit_radius &&
                std::abs(y - target.y) <= marker_hit_radius) {
                const std::string title = target.leg.kind == WaypointKind::airport &&
                        !target.name.empty()
                    ? target.leg.identifier + " / " + target.name
                    : target.leg.identifier;
                const std::string detail = target.leg.kind == WaypointKind::airport
                    ? "Airport waypoint" : "Flight-plan waypoint";
                window->pending_map_navigation_ = map_navigation_for(
                    window->flight_plan_model_.snapshot(), target.leg, title, detail);
                window->map_action_message_.clear();
                return 1;
            }
        }
        const auto& telemetry = window->telemetry_model_.snapshot();
        if (telemetry.available && x >= map_left && x <= map_right &&
            y >= map_bottom && y <= map_top) {
            window->map_dragging_ = true;
            window->map_drag_moved_ = false;
            window->map_drag_start_x_ = x;
            window->map_drag_start_y_ = y;
            window->map_drag_start_latitude_ =
                window->moving_map_model_.center_latitude_degrees();
            window->map_drag_start_longitude_ =
                window->moving_map_model_.center_longitude_degrees();
            window->map_action_message_.clear();
            return 1;
        }
    }
    if (!compact_layout(left, right)) {
        window->ui_model_.select_at(x - left, top - y);
    }
    return 1;
}

void XPlaneWindow::handle_key(XPLMWindowID, char key, XPLMKeyFlags flags, char virtual_key,
                              void* refcon, int losing_focus) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window || losing_focus || (flags & xplm_DownFlag) == 0) return;
    if (window->approach_panel_open_) {
        if (static_cast<unsigned char>(virtual_key) == XPLM_VK_ESCAPE) {
            window->approach_panel_open_ = false;
            if (window->flight_plan_editor_.active()) XPLMTakeKeyboardFocus(window->window_id_);
        }
        return;
    }
    if (window->ui_model_.active_page() == EfbPage::flight_plan) {
        window->handle_editor_key(key, virtual_key);
    } else if (window->ui_model_.active_page() == EfbPage::airports) {
        window->handle_airport_key(key, virtual_key);
    } else if (window->ui_model_.active_page() == EfbPage::briefing) {
        window->handle_briefing_key(key, virtual_key);
    }
}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID window_id, int x, int y, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    hover_cursor_active = window != nullptr;
    hover_cursor_x = x;
    hover_cursor_y = y;
    if (!window) return xplm_CursorDefault;
    if (window->ui_model_.active_page() != EfbPage::home) {
        window->hovered_map_poi_.reset();
        window->hovered_map_target_.reset();
        return xplm_CursorDefault;
    }
    const int marker_hit_radius = std::max(10, window->map_marker_scale_ * 10 / 100);
    for (const auto& target : window->traffic_hit_targets_) {
        if (std::abs(x - target.x) <= marker_hit_radius + 3 &&
            std::abs(y - target.y) <= marker_hit_radius + 3) {
            window->hovered_map_poi_.reset();
            window->hovered_map_target_.reset();
            return xplm_CursorArrow;
        }
    }
    for (const auto& target : window->poi_hit_targets_) {
        if (std::abs(x - target.x) <= marker_hit_radius &&
            std::abs(y - target.y) <= marker_hit_radius) {
            window->hovered_map_poi_ = target.poi;
            window->hovered_map_target_.reset();
            return xplm_CursorArrow;
        }
    }
    window->hovered_map_poi_.reset();
    for (const auto& target : window->map_hit_targets_) {
        if (std::abs(x - target.x) <= marker_hit_radius &&
            std::abs(y - target.y) <= marker_hit_radius) {
            window->hovered_map_target_ = target;
            return xplm_CursorArrow;
        }
    }
    window->hovered_map_target_.reset();
    int left{}, top{}, right{}, bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
    const int content_left = content_left_for(left, right);
    const int content_top = top - status_bar_height - 38;
    const auto panel = home_map_geometry(content_left, content_top, right, bottom);
    if (x >= panel.left && x <= panel.right && y <= panel.tile_top && y >= panel.bottom) {
        return xplm_CursorArrow;
    }
    return xplm_CursorDefault;
}

int XPlaneWindow::handle_wheel(XPLMWindowID window_id, int x, int y, int, int clicks, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window) {
        return 0;
    }
    int left{};
    int top{};
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
    if (window->pdf_viewer_.visible()) {
        const int viewer_left = viewer_left_for(left, right);
        const int viewer_right = right - 18;
        const int viewer_top = top - status_bar_height - 14;
        const int viewer_bottom = bottom + 18;
        const int document_left = viewer_left + 10;
        const int document_top = viewer_top - 42;
        const int document_right = viewer_right - 10;
        const int document_bottom = viewer_bottom + 54;
        if (x >= document_left && x <= document_right &&
            y <= document_top && y >= document_bottom) {
            window->pdf_viewer_.scroll(clicks);
        }
        return 1;
    }
    if (window->approach_panel_open_ &&
        window->ui_model_.active_page() == EfbPage::flight_plan) {
        const auto airport = window->airport_info_model_.snapshot();
        const int direction = clicks < 0 ? 1 : -1;
        const auto* selected = selected_approach(airport, window->selected_approach_identifier_);
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        const int panel_right = responsive_card_right(content_left, right, 420);
        const int list_right = content_left + (panel_right - content_left) * 45 / 100;
        if (!window->approach_transition_picker_open_ && selected && x > list_right) {
            const int panel_bottom = bottom + 22;
            const int list_top = content_top - 113;
            const int list_bottom = panel_bottom + 64;
            const int fix_y = list_top - 134;
            const int visible = std::max(1, (fix_y - (list_bottom + 38)) / 30 + 1);
            const int maximum = std::max(0, static_cast<int>(
                approach_fix_names(*selected, window->selected_approach_transition_).size()) - visible);
            window->approach_fix_scroll_ = std::clamp(
                window->approach_fix_scroll_ + direction * std::abs(clicks), 0, maximum);
            return 1;
        }
        const int maximum = window->approach_transition_picker_open_ && selected
            ? std::max(0, static_cast<int>(selected->transitions.size()) - 2)
            : std::max(0, static_cast<int>(airport.procedures.approach_details.size()) - 2);
        window->approach_scroll_ = std::clamp(
            window->approach_scroll_ + direction * std::abs(clicks), 0, maximum);
        return 1;
    }
    if (window->ui_model_.active_page() == EfbPage::briefing &&
        window->briefing_model_.active_tab() == BriefingTab::library) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        if (x < content_left || x > right - 28 || y > content_top - 140 || y < bottom + 40) return 0;
        const int direction = clicks < 0 ? 1 : -1;
        for (int count = std::abs(clicks); count > 0; --count) {
            if (!select_library_offset(window->briefing_model_, direction)) break;
        }
        return 1;
    }
    if (window->ui_model_.active_page() == EfbPage::flight_plan &&
        window->flight_plan_editor_.active()) {
        const int content_left = content_left_for(left, right);
        const int content_top = top - status_bar_height - 38;
        if (x < content_left || x > right - 28 || y > content_top || y < bottom + 40) {
            return 0;
        }
        int selected = window->flight_plan_editor_.selected_index();
        const int direction = clicks < 0 ? 1 : -1;
        for (int count = std::abs(clicks); count > 0; --count) {
            if (!window->flight_plan_editor_.select(selected + direction)) {
                break;
            }
            selected += direction;
        }
        return 1;
    }
    if (window->ui_model_.active_page() == EfbPage::flight_plan &&
        window->flight_plan_expanded_) {
        const int content_top = top - status_bar_height - 38;
        const int viewport_height = std::max(120, content_top - bottom - 28);
        const int route_height = 126 +
            static_cast<int>(window->flight_plan_model_.snapshot().legs.size()) * 54;
        const int limit = std::max(0, route_height - viewport_height);
        const int direction = clicks < 0 ? 1 : -1;
        window->page_scroll_pixels_ = std::clamp(
            window->page_scroll_pixels_ + direction * std::abs(clicks) * 38, 0, limit);
        return 1;
    }
    if (window->ui_model_.active_page() != EfbPage::home) {
        const int content_top = top - status_bar_height - 38;
        const int limit = page_scroll_limit(window->ui_model_.active_page(), content_top, bottom);
        if (limit <= 0) return 0;
        const int direction = clicks < 0 ? 1 : -1;
        window->page_scroll_pixels_ = std::clamp(
            window->page_scroll_pixels_ + direction * std::abs(clicks) * 38, 0, limit);
        return 1;
    }
    const int content_left = content_left_for(left, right);
    const int content_top = top - status_bar_height - 38;
    const auto panel = home_map_geometry(content_left, content_top, right, bottom);
    if (x < panel.left || x > panel.right || y > panel.tile_top || y < panel.bottom) {
        return 0;
    }
    const int map_left = panel.left + 3;
    const int map_right = panel.right - 3;
    const int map_top = panel.tile_top;
    const int map_bottom = panel.bottom + 3;
    if (window->map_filters_open_) {
        const auto filters = map_filter_geometry(map_left, map_top, map_right, map_bottom);
        if (point_in_rectangle(x, y, filters.left, filters.top,
                               filters.right, filters.bottom)) {
            const int direction = clicks < 0 ? 1 : -1;
            window->map_filter_scroll_ = std::clamp(
                window->map_filter_scroll_ + direction * std::abs(clicks),
                0, filters.max_scroll);
            return 1;
        }
    }
    const int center_x = (map_left + map_right) / 2;
    const int center_y = (map_top + map_bottom) / 2;
    const MapTileViewport old_viewport{
        map_left, map_top, map_right, map_bottom,
        window->moving_map_model_.center_latitude_degrees(),
        window->moving_map_model_.center_longitude_degrees(),
        window->moving_map_model_.range_nm()};
    const auto anchor = unproject_map_coordinate(
        window->moving_map_model_.style(), old_viewport, x, y);
    if (window->moving_map_model_.apply_wheel(clicks) && anchor.valid &&
        (std::abs(x - center_x) > 4 || std::abs(y - center_y) > 4)) {
        const MapTileViewport anchor_viewport{
            map_left, map_top, map_right, map_bottom,
            anchor.latitude_degrees, anchor.longitude_degrees,
            window->moving_map_model_.range_nm()};
        const auto new_center = unproject_map_coordinate(
            window->moving_map_model_.style(), anchor_viewport,
            2 * center_x - x, 2 * center_y - y);
        if (new_center.valid) {
            window->moving_map_model_.pan_to(new_center.latitude_degrees,
                                             new_center.longitude_degrees);
        }
    }
    return 1;
}

} // namespace openefb::xplane
