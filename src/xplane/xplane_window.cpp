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

constexpr int initial_width = 860;
constexpr int initial_height = 580;
constexpr int minimum_width = 760;
constexpr int minimum_height = 560;

struct Color {
    float red;
    float green;
    float blue;
    float alpha;
};

constexpr Color canvas{0.035F, 0.052F, 0.075F, 1.0F};
constexpr Color status_bar{0.055F, 0.086F, 0.118F, 1.0F};
constexpr Color sidebar{0.025F, 0.039F, 0.058F, 1.0F};
constexpr Color active_navigation{0.045F, 0.310F, 0.410F, 1.0F};
constexpr Color card{0.090F, 0.132F, 0.175F, 1.0F};
constexpr Color accent{0.30F, 0.92F, 1.0F, 1.0F};
constexpr Color text_primary{1.0F, 1.0F, 1.0F, 1.0F};
constexpr Color text_muted{0.82F, 0.87F, 0.92F, 1.0F};
constexpr Color connected{0.42F, 1.0F, 0.62F, 1.0F};
constexpr Color map_background{0.025F, 0.075F, 0.095F, 1.0F};
constexpr Color map_grid{0.16F, 0.31F, 0.35F, 1.0F};
constexpr Color route_line{0.25F, 0.72F, 0.88F, 1.0F};

void draw_rectangle(int left, int top, int right, int bottom, Color color) {
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glBegin(GL_QUADS);
    glVertex2i(left, bottom);
    glVertex2i(right, bottom);
    glVertex2i(right, top);
    glVertex2i(left, top);
    glEnd();
}

void draw_border(int left, int top, int right, int bottom, Color color, float width = 2.0F) {
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

void draw_button(int left, int top, int right, int bottom, std::string_view label,
                 bool emphasized = false) {
    draw_rectangle(left, top, right, bottom, emphasized ? active_navigation : status_bar);
    draw_border(left, top, right, bottom, emphasized ? accent : map_grid, 1.0F);
    draw_text(left + 12, bottom + 9, label, emphasized ? text_primary : text_muted);
}

std::string format_flight_summary(const TelemetrySnapshot& telemetry) {
    if (!telemetry.available) {
        return "Waiting for live simulator data";
    }
    char value[96]{};
    std::snprintf(value, sizeof(value), "Altitude %.0f ft  |  Ground speed %.0f kt",
                  telemetry.altitude_feet, telemetry.ground_speed_knots);
    return value;
}

std::string format_position(const TelemetrySnapshot& telemetry) {
    if (!telemetry.available) {
        return "Position unavailable";
    }
    char value[96]{};
    std::snprintf(value, sizeof(value), "Lat %.4f  |  Lon %.4f",
                  telemetry.latitude_degrees, telemetry.longitude_degrees);
    return value;
}

std::string format_motion(const TelemetrySnapshot& telemetry) {
    if (!telemetry.available) {
        return "Motion data unavailable";
    }
    char value[96]{};
    std::snprintf(value, sizeof(value), "Heading %03d deg  |  V/S %+.0f fpm",
                  static_cast<int>(std::lround(telemetry.heading_degrees)) % 360,
                  telemetry.vertical_speed_fpm);
    return value;
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
    const int card_right = std::max(left + 420, right - 28);
    draw_text(left, top, "Flight Plan Builder", text_primary, xplmFont_Basic);
    draw_text(left, top - 27, editor.message(), text_muted);

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
                top - action_bottom_offset, "Cancel");
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
                           int left, int top, int right, int bottom) {
    const int card_right = std::max(left + 260, right - 28);
    if (editor.active()) {
        draw_flight_plan_editor(editor, left, top, right, bottom);
        return;
    }
    draw_text(left, top, "Flight Plan", text_primary, xplmFont_Basic);
    draw_button(card_right - 82, top + 8, card_right, top - 23, "Edit", true);

    if (!flight_plan.available) {
        draw_text(left, top - 28, "Waiting for X-Plane FMS data...", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Route status", "Flight-plan service is starting");
        return;
    }
    if (flight_plan.legs.empty()) {
        draw_text(left, top - 28, "No active route is loaded.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "X-Plane FMS", "Select Edit to create a route from waypoint identifiers");
        return;
    }

    const auto& first = flight_plan.legs.front();
    const auto& last = flight_plan.legs.back();
    char summary[128]{};
    std::snprintf(summary, sizeof(summary), "Departure %s  |  Destination %s  |  %zu legs  |  Active %d",
                  first.identifier.c_str(), last.identifier.c_str(), flight_plan.legs.size(),
                  flight_plan.active_leg_index >= 0 ? flight_plan.active_leg_index + 1 : 0);
    draw_text(left, top - 28, summary, text_muted);

    const int active = std::clamp(flight_plan.active_leg_index, 0,
                                  static_cast<int>(flight_plan.legs.size()) - 1);
    const std::size_t start = active > 1 ? static_cast<std::size_t>(active - 1) : 0U;
    const std::size_t visible_count = std::min<std::size_t>(5, flight_plan.legs.size() - start);
    constexpr int row_height = 48;
    constexpr int row_gap = 6;
    int row_top = top - 58;
    for (std::size_t offset = 0; offset < visible_count; ++offset) {
        const auto& leg = flight_plan.legs[start + offset];
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

    const std::size_t remaining = flight_plan.legs.size() - start - visible_count;
    if (remaining > 0) {
        draw_text(left + 14, row_top - 12, "+ " + std::to_string(remaining) + " more legs", text_muted);
    }
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
                       const AirportWeather& weather) {
    constexpr int card_height = 132;
    draw_rectangle(left, top, right, top - card_height, card);
    const std::string title = weather.airport_id.empty()
                                  ? std::string(role)
                                  : std::string(role) + "  /  " + weather.airport_id;
    draw_text(left + 18, top - 29, title, text_primary);

    if (weather.airport_id.empty()) {
        draw_text(left + 18, top - 60, "Airport not found in the active route", text_muted);
        return;
    }
    if (weather.metar.empty()) {
        draw_text(left + 18, top - 60, "No downloaded METAR is available", text_muted);
        draw_text(left + 18, top - 84, "Enable X-Plane Real Weather and allow it to update", text_muted);
        return;
    }

    const int available_width = std::max(252, right - left - 36);
    const auto lines = wrap_text(weather.metar, static_cast<std::size_t>(available_width / 7));
    const std::size_t visible_lines = std::min<std::size_t>(3, lines.size());
    for (std::size_t index = 0; index < visible_lines; ++index) {
        draw_text(left + 18, top - 60 - static_cast<int>(index) * 23, lines[index], text_muted);
    }
}

void draw_weather_page(const WeatherSnapshot& weather, int left, int top, int right) {
    const int card_right = std::max(left + 260, right - 28);
    draw_text(left, top, "Route Weather", text_primary, xplmFont_Basic);
    draw_text(left, top - 28, "Latest downloaded METARs from X-Plane Real Weather", text_muted);
    if (!weather.available) {
        draw_card(left, top - 62, card_right, top - 158,
                  "Weather status", "Weather service is starting");
        return;
    }
    draw_weather_card(left, top - 62, card_right, "Departure", weather.departure);
    draw_weather_card(left, top - 212, card_right, "Destination", weather.destination);
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
    const int card_right = std::max(left + 260, right - 28);
    draw_text(left, top, "Route Progress", text_primary, xplmFont_Basic);
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

std::string format_fuel_mass(const FuelSnapshot& fuel) {
    constexpr double kilograms_to_pounds = 2.2046226218;
    char value[96]{};
    std::snprintf(value, sizeof(value), "%.1f kg  |  %.0f lb",
                  fuel.fuel_remaining_kg, fuel.fuel_remaining_kg * kilograms_to_pounds);
    return value;
}

std::string format_fuel_flow(const FuelSnapshot& fuel) {
    if (!fuel.endurance_available) {
        return "Waiting for measurable engine fuel flow";
    }
    char value[96]{};
    std::snprintf(value, sizeof(value), "%.1f GPH  |  US gallons, 6.0 lb/gal basis",
                  fuel.fuel_flow_us_gallons_per_hour);
    return value;
}

std::string format_fuel_estimates(const FuelSnapshot& fuel) {
    if (!fuel.endurance_available) {
        return "Start an engine to calculate endurance";
    }
    const double bounded_hours = std::min(999.0, fuel.endurance_hours);
    const int total_minutes = static_cast<int>(std::lround(bounded_hours * 60.0));
    std::string value = "Endurance " + std::to_string(total_minutes / 60) + " hr " +
                        std::to_string(total_minutes % 60) + " min";
    if (fuel.range_available) {
        char range[48]{};
        std::snprintf(range, sizeof(range), "  |  Range %.0f NM", fuel.estimated_range_nm);
        value += range;
    } else {
        value += "  |  Range unavailable while parked";
    }
    return value;
}

void draw_fuel_page(const FuelSnapshot& fuel, int left, int top, int right) {
    const int card_right = std::max(left + 260, right - 28);
    draw_text(left, top, "Fuel", text_primary, xplmFont_Basic);
    draw_text(left, top - 28, "Live totals and estimates from the active aircraft", text_muted);
    if (!fuel.available) {
        draw_card(left, top - 62, card_right, top - 158,
                  "Fuel status", "Waiting for X-Plane fuel data");
        return;
    }
    draw_card(left, top - 62, card_right, top - 158,
              "Fuel remaining", format_fuel_mass(fuel));
    draw_card(left, top - 178, card_right, top - 274,
              "Current burn", format_fuel_flow(fuel));
    draw_card(left, top - 294, card_right, top - 390,
              "Estimates", format_fuel_estimates(fuel));
}

struct ScreenPoint {
    bool valid{false};
    double x{};
    double y{};
};

ScreenPoint map_screen_point(const MovingMapModel& map, const TelemetrySnapshot& telemetry,
                             double latitude, double longitude, int center_x, int center_y,
                             double pixels_per_nm) {
    const auto offset = map.project(telemetry.latitude_degrees, telemetry.longitude_degrees,
                                    latitude, longitude);
    return {offset.valid, center_x + offset.east_nm * pixels_per_nm,
            center_y + offset.north_nm * pixels_per_nm};
}

void draw_range_ring(int center_x, int center_y, double radius, Color color) {
    constexpr int segments = 64;
    glColor4f(color.red, color.green, color.blue, color.alpha);
    glBegin(GL_LINE_LOOP);
    for (int index = 0; index < segments; ++index) {
        const double angle = static_cast<double>(index) * 2.0 * 3.14159265358979323846 / segments;
        glVertex2d(center_x + std::cos(angle) * radius, center_y + std::sin(angle) * radius);
    }
    glEnd();
}

void draw_aircraft_symbol(int center_x, int center_y, double heading_degrees) {
    const double heading = heading_degrees * 3.14159265358979323846 / 180.0;
    const double forward_x = std::sin(heading);
    const double forward_y = std::cos(heading);
    const double right_x = std::cos(heading);
    const double right_y = -std::sin(heading);
    glColor4f(accent.red, accent.green, accent.blue, accent.alpha);
    glBegin(GL_TRIANGLES);
    glVertex2d(center_x + forward_x * 13.0, center_y + forward_y * 13.0);
    glVertex2d(center_x - forward_x * 8.0 + right_x * 7.0,
               center_y - forward_y * 8.0 + right_y * 7.0);
    glVertex2d(center_x - forward_x * 8.0 - right_x * 7.0,
               center_y - forward_y * 8.0 - right_y * 7.0);
    glEnd();
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
    geometry.top = top - 34;
    geometry.right = std::max(left + 360, right - 28);
    geometry.bottom = std::max(bottom + 164, geometry.top - 330);
    geometry.tile_top = geometry.top - 38;
    return geometry;
}

void draw_map_style_button(int left, int top, int right, std::string_view label, bool selected) {
    draw_rectangle(left, top, right, top - 25, selected ? active_navigation : status_bar);
    draw_border(left, top, right, top - 25, selected ? accent : map_grid, 1.0F);
    draw_text(left + 11, top - 17, label, selected ? text_primary : text_muted);
}

struct MapLayerButton {
    MapLayer layer;
    std::string_view label;
};

constexpr std::array map_layer_buttons{
    MapLayerButton{MapLayer::weather, "WX"},
    MapLayerButton{MapLayer::airports, "APT"},
    MapLayerButton{MapLayer::navaids, "NAV"},
    MapLayerButton{MapLayer::airspace, "AIR"},
};

int map_layer_buttons_left(const HomeMapGeometry& panel) {
    const int topo_left = panel.right - 10 - 64;
    const int street_left = topo_left - 6 - 70;
    return street_left - 4 - static_cast<int>(map_layer_buttons.size()) * 46;
}

Color airspace_color(std::string_view class_code) {
    if (class_code == "B" || class_code == "C") return {0.30F, 0.65F, 1.0F, 0.92F};
    if (class_code == "D") return {0.70F, 0.45F, 1.0F, 0.92F};
    if (class_code == "P" || class_code == "R" || class_code == "Q")
        return {1.0F, 0.36F, 0.24F, 0.95F};
    return {1.0F, 0.72F, 0.25F, 0.85F};
}

void draw_airspace_overlay(const AirspaceSnapshot& airspace, const MovingMapModel& map,
                           const TelemetrySnapshot& telemetry, int center_x, int center_y,
                           double pixels_per_nm, int left, int top, int right, int bottom) {
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
            auto point_1 = map_screen_point(map, telemetry, first.latitude_degrees,
                                            first.longitude_degrees, center_x, center_y, pixels_per_nm);
            auto point_2 = map_screen_point(map, telemetry, second.latitude_degrees,
                                            second.longitude_degrees, center_x, center_y, pixels_per_nm);
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
                draw_text(static_cast<int>(label_point.x) + 3, static_cast<int>(label_point.y) - 3,
                          label, airspace_color(zone.class_code));
            }
            ++rendered_zones;
        }
    }
}

void draw_circle_marker(int x, int y, Color color) {
    draw_range_ring(x, y, 6.0, color);
    draw_rectangle(x - 2, y + 2, x + 2, y - 2, color);
}

void draw_diamond_marker(int x, int y, Color color) {
    draw_line(x, y + 6, x + 6, y, color, 2.0F);
    draw_line(x + 6, y, x, y - 6, color, 2.0F);
    draw_line(x, y - 6, x - 6, y, color, 2.0F);
    draw_line(x - 6, y, x, y + 6, color, 2.0F);
}

void draw_home_map(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                   const RouteProgressSnapshot& progress, const FuelSnapshot& fuel,
                   const WeatherSnapshot& weather, const AirspaceSnapshot& airspace,
                   const MovingMapModel& map, XPlaneMapTiles& tiles,
                   int left, int top, int right, int bottom) {
    draw_text(left, top, "Live Moving Map", text_primary, xplmFont_Basic);
    draw_text(left + 142, top, "OpenStreetMap basemap  /  mouse wheel to zoom", text_muted);
    const auto panel = home_map_geometry(left, top, right, bottom);
    const int map_left = panel.left + 3;
    const int map_right = panel.right - 3;
    const int map_top = panel.tile_top;
    const int map_bottom = panel.bottom + 3;
    draw_rectangle(panel.left, panel.top, panel.right, panel.bottom, card);
    draw_border(panel.left, panel.top, panel.right, panel.bottom, accent, 2.0F);

    const int topo_right = panel.right - 10;
    const int topo_left = topo_right - 64;
    const int street_right = topo_left - 6;
    const int street_left = street_right - 70;
    draw_map_style_button(street_left, panel.top - 7, street_right, "Street",
                          map.style() == MapStyle::street);
    draw_map_style_button(topo_left, panel.top - 7, topo_right, "Topo",
                          map.style() == MapStyle::topographic);

    int layer_left = map_layer_buttons_left(panel);
    for (const auto& button : map_layer_buttons) {
        draw_map_style_button(layer_left, panel.top - 7, layer_left + 42, button.label,
                              map.layer_enabled(button.layer));
        layer_left += 46;
    }

    char range_label[32]{};
    std::snprintf(range_label, sizeof(range_label), "RANGE %.0f NM", map.range_nm());
    draw_text(panel.left + 14, panel.top - 23, range_label, text_muted);
    draw_rectangle(map_left, map_top, map_right, map_bottom, map_background);

    if (!telemetry.available) {
        draw_text(map_left + 22, map_top - 34, "Waiting for live aircraft position...", text_muted);
        return;
    }

    tiles.draw(map.style(), {map_left, map_top, map_right, map_bottom,
                             telemetry.latitude_degrees, telemetry.longitude_degrees,
                             map.range_nm()});

    const int center_x = (map_left + map_right) / 2;
    const int center_y = (map_top + map_bottom) / 2;
    const double radius_pixels = std::max(40.0, std::min(map_right - map_left, map_top - map_bottom) * 0.46);
    const double pixels_per_nm = radius_pixels / map.range_nm();
    draw_line(map_left, center_y, map_right, center_y, map_grid);
    draw_line(center_x, map_bottom, center_x, map_top, map_grid);
    draw_range_ring(center_x, center_y, radius_pixels / 3.0, map_grid);
    draw_range_ring(center_x, center_y, radius_pixels * 2.0 / 3.0, map_grid);
    draw_range_ring(center_x, center_y, radius_pixels, map_grid);

    if (map.layer_enabled(MapLayer::airspace)) {
        draw_airspace_overlay(airspace, map, telemetry, center_x, center_y, pixels_per_nm,
                              map_left, map_top, map_right, map_bottom);
    }

    std::vector<ScreenPoint> route_points;
    route_points.reserve(flight_plan.legs.size());
    for (const auto& leg : flight_plan.legs) {
        route_points.push_back(map_screen_point(map, telemetry, leg.latitude_degrees,
                                                leg.longitude_degrees, center_x, center_y,
                                                pixels_per_nm));
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
            draw_line(x_1, y_1, x_2, y_2,
                      flight_plan.legs[index].active ? accent : route_line,
                      flight_plan.legs[index].active ? 3.0F : 2.0F);
        }
    }

    for (std::size_t index = 0; index < route_points.size(); ++index) {
        const auto& point = route_points[index];
        if (!point.valid || point.x < map_left + 7 || point.x > map_right - 7 ||
            point.y < map_bottom + 7 || point.y > map_top - 7) {
            continue;
        }
        const auto& leg = flight_plan.legs[index];
        const Color waypoint_color = leg.active ? accent : text_primary;
        const bool airport_visible = leg.kind == WaypointKind::airport &&
                                     map.layer_enabled(MapLayer::airports);
        const bool navaid_visible = (leg.kind == WaypointKind::vor || leg.kind == WaypointKind::ndb ||
                                     leg.kind == WaypointKind::fix) && map.layer_enabled(MapLayer::navaids);
        if (airport_visible) draw_circle_marker(static_cast<int>(point.x), static_cast<int>(point.y), waypoint_color);
        else if (navaid_visible) draw_diamond_marker(static_cast<int>(point.x), static_cast<int>(point.y), waypoint_color);
        else draw_rectangle(static_cast<int>(point.x) - 3, static_cast<int>(point.y) + 3,
                            static_cast<int>(point.x) + 3, static_cast<int>(point.y) - 3,
                            waypoint_color);
        const bool label = leg.active || index == 0 || index + 1 == route_points.size();
        if (label && point.x < map_right - 80 && point.y < map_top - 18) {
            draw_text(static_cast<int>(point.x) + 8, static_cast<int>(point.y) + 5,
                      leg.identifier, waypoint_color);
        }
        if (map.layer_enabled(MapLayer::weather) && leg.kind == WaypointKind::airport) {
            const bool endpoint = leg.identifier == weather.departure.airport_id ||
                                  leg.identifier == weather.destination.airport_id;
            if (endpoint) {
                const bool report = (leg.identifier == weather.departure.airport_id && !weather.departure.metar.empty()) ||
                                    (leg.identifier == weather.destination.airport_id && !weather.destination.metar.empty());
                const Color weather_color = report ? connected : Color{1.0F, 0.72F, 0.25F, 1.0F};
                draw_range_ring(static_cast<int>(point.x), static_cast<int>(point.y), 11.0, weather_color);
                if (point.x < map_right - 35) draw_text(static_cast<int>(point.x) + 13,
                                                        static_cast<int>(point.y) - 7, "WX", weather_color);
            }
        }
    }

    draw_aircraft_symbol(center_x, center_y, telemetry.heading_degrees);
    draw_rectangle(map_left + 10, map_bottom + 54, map_right - 10, map_bottom + 28, status_bar);
    draw_text(map_left + 22, map_bottom + 36, map_progress_text(progress), text_primary);
    const std::string attribution = map.style() == MapStyle::street
        ? "(c) OpenStreetMap contributors"
        : "Map data (c) OpenStreetMap contributors, SRTM  |  Map style (c) OpenTopoMap (CC-BY-SA)";
    draw_rectangle(map_left, map_bottom + 22, map_right, map_bottom, status_bar);
    draw_text(map_left + 8, map_bottom + 6, attribution, text_muted);

    const int cards_top = panel.bottom - 16;
    const int cards_bottom = bottom + 38;
    const int gap = 12;
    const int middle = (panel.left + panel.right) / 2;
    draw_rectangle(panel.left, cards_top, middle - gap / 2, cards_bottom, card);
    draw_text(panel.left + 16, cards_top - 26, "AIRCRAFT", accent);
    draw_text(panel.left + 16, cards_top - 53, format_flight_summary(telemetry), text_primary);
    draw_rectangle(middle + gap / 2, cards_top, panel.right, cards_bottom, card);
    draw_text(middle + gap / 2 + 16, cards_top - 26, "FUEL", accent);
    draw_text(middle + gap / 2 + 16, cards_top - 53,
              fuel.available ? format_fuel_flow(fuel) : "Waiting for live fuel data", text_primary);
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
    const int card_right = std::max(left + 420, right - 28);
    draw_text(left, top, "Airport Information", text_primary, xplmFont_Basic);
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
    draw_text(left, top - 105, airport.identifier, accent, xplmFont_Basic);
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

void draw_page_content(EfbPage page, const TelemetrySnapshot& telemetry,
                       const FlightPlanSnapshot& flight_plan,
                       const FlightPlanEditor& flight_plan_editor,
                       std::string_view airport_query,
                       const AirportInfoSnapshot& airport_info,
                       const FuelSnapshot& fuel,
                       const RouteProgressSnapshot& route_progress,
                       const WeatherSnapshot& weather,
                       const AirspaceSnapshot& airspace,
                       const MovingMapModel& moving_map,
                       XPlaneMapTiles& map_tiles,
                       int left, int top, int right, int bottom) {
    const int card_right = std::max(left + 260, right - 28);
    switch (page) {
    case EfbPage::home:
        draw_home_map(telemetry, flight_plan, route_progress, fuel, weather, airspace,
                      moving_map, map_tiles,
                      left, top, right, bottom);
        break;
    case EfbPage::flight_plan:
        draw_flight_plan_page(flight_plan, flight_plan_editor, left, top, right, bottom);
        break;
    case EfbPage::airports:
        draw_airports_page(airport_query, airport_info, left, top, right, bottom);
        break;
    case EfbPage::progress:
        draw_progress_page(route_progress, left, top, right);
        break;
    case EfbPage::weather:
        draw_weather_page(weather, left, top, right);
        break;
    case EfbPage::fuel:
        draw_fuel_page(fuel, left, top, right);
        break;
    case EfbPage::aircraft:
        draw_text(left, top, "Aircraft", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, telemetry.available ? telemetry.aircraft_name : "Waiting for active aircraft",
                  text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Position", format_position(telemetry));
        draw_card(left, top - 178, card_right, top - 274,
                  "Altitude and speed", format_flight_summary(telemetry));
        draw_card(left, top - 294, card_right, top - 390,
                  "Motion", format_motion(telemetry));
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
                  "Version", "0.11.0 - M11 operational map overlays");
        draw_card(left, top - 178, card_right, top - 274,
                  "Project", "Built in the open for the flight-sim community");
        break;
    }

    draw_text(left, bottom + 22, "OPEN EFB  /  M11", text_muted);
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
                                                    MovingMapModel& moving_map_model,
                                                    RouteProgressModel& route_progress_model,
                                                    WeatherModel& weather_model,
                                                    XPlanePreferences& preferences) {
    auto window = std::unique_ptr<XPlaneWindow>(
        new XPlaneWindow(ui_model, telemetry_model, flight_plan_model, flight_plan_editor,
                         xplane_flight_plan, airport_info_model, xplane_airport_data,
                         airspace_model,
                         fuel_model, moving_map_model,
                         route_progress_model,
                         weather_model, preferences));
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
                           MovingMapModel& moving_map_model,
                           RouteProgressModel& route_progress_model,
                           WeatherModel& weather_model,
                           XPlanePreferences& preferences)
    : ui_model_(ui_model), telemetry_model_(telemetry_model),
      flight_plan_model_(flight_plan_model), flight_plan_editor_(flight_plan_editor),
      xplane_flight_plan_(xplane_flight_plan), airport_info_model_(airport_info_model),
      xplane_airport_data_(xplane_airport_data), airspace_model_(airspace_model), fuel_model_(fuel_model),
      moving_map_model_(moving_map_model),
      route_progress_model_(route_progress_model),
      weather_model_(weather_model),
      preferences_(preferences), map_tiles_(preferences.map_cache_directory()) {
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

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    draw_rectangle(left, top, right, bottom, canvas);
    draw_rectangle(left, top, right, top - status_bar_height, status_bar);
    draw_rectangle(left, top - status_bar_height, left + sidebar_width, bottom, sidebar);

    draw_text(left + 20, top - 35, "OpenEFB", accent, xplmFont_Basic);
    draw_text(left + sidebar_width + 24, top - 35, ui_model_.active_page_title(), text_primary);
    const auto& telemetry = telemetry_model_.snapshot();
    draw_text(right - 206, top - 35, telemetry.available ? "LIVE DATA" : "WAITING",
              telemetry.available ? connected : text_muted);
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
    draw_page_content(ui_model_.active_page(), telemetry, flight_plan_model_.snapshot(),
                      flight_plan_editor_,
                      airport_query_, airport_info_model_.snapshot(),
                      fuel_model_.snapshot(), route_progress_model_.snapshot(),
                      weather_model_.snapshot(), airspace_model_.snapshot(), moving_map_model_,
                      map_tiles_,
                      content_left, content_top, right, bottom);
}

void XPlaneWindow::save_geometry() const {
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
            "X-Plane's route changed while editing - Cancel and reopen the builder");
        return;
    }
    const auto result = xplane_flight_plan_.apply_route(flight_plan_editor_.legs());
    if (!result.success) {
        flight_plan_editor_.set_message(result.message);
        return;
    }
    flight_plan_editor_.mark_applied(flight_plan_model_.snapshot());
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
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
    if (window->ui_model_.active_page() == EfbPage::airports) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
        const int card_right = std::max(content_left + 420, right - 28);
        if (x >= content_left && x <= card_right &&
            y <= content_top - 45 && y >= content_top - 80) {
            XPLMTakeKeyboardFocus(window_id);
            if (x >= card_right - 78) window->search_airport();
            return 1;
        }
    }
    if (window->ui_model_.active_page() == EfbPage::flight_plan) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
        const int card_right = std::max(content_left + 420, right - 28);
        if (!window->flight_plan_editor_.active()) {
            if (x >= card_right - 82 && x <= card_right &&
                y <= content_top + 8 && y >= content_top - 23 &&
                window->flight_plan_editor_.begin(window->flight_plan_model_.snapshot())) {
                XPLMTakeKeyboardFocus(window_id);
                return 1;
            }
        } else {
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
    if (window->ui_model_.active_page() == EfbPage::home) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int topo_right = panel.right - 10;
        const int topo_left = topo_right - 64;
        const int street_right = topo_left - 6;
        const int street_left = street_right - 70;
        if (y <= panel.top - 7 && y >= panel.top - 32) {
            int layer_left = map_layer_buttons_left(panel);
            for (const auto& button : map_layer_buttons) {
                if (x >= layer_left && x <= layer_left + 42) {
                    window->moving_map_model_.toggle_layer(button.layer);
                    return 1;
                }
                layer_left += 46;
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
    }
    window->ui_model_.select_at(x - left, top - y);
    return 1;
}

void XPlaneWindow::handle_key(XPLMWindowID, char key, XPLMKeyFlags flags, char virtual_key,
                              void* refcon, int losing_focus) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window || losing_focus || (flags & xplm_DownFlag) == 0) return;
    if (window->ui_model_.active_page() == EfbPage::flight_plan) {
        window->handle_editor_key(key, virtual_key);
    } else if (window->ui_model_.active_page() == EfbPage::airports) {
        window->handle_airport_key(key, virtual_key);
    }
}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID, int, int, void*) {
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
    if (window->ui_model_.active_page() == EfbPage::flight_plan &&
        window->flight_plan_editor_.active()) {
        const int content_left = left + sidebar_width + 30;
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
    if (window->ui_model_.active_page() != EfbPage::home) {
        return 0;
    }
    const int content_left = left + sidebar_width + 30;
    const int content_top = top - status_bar_height - 38;
    const auto panel = home_map_geometry(content_left, content_top, right, bottom);
    if (x < panel.left || x > panel.right || y > panel.tile_top || y < panel.bottom) {
        return 0;
    }
    window->moving_map_model_.apply_wheel(clicks);
    return 1;
}

} // namespace openefb::xplane
