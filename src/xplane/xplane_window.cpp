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

void draw_flight_plan_page(const FlightPlanSnapshot& flight_plan,
                           int left, int top, int right) {
    const int card_right = std::max(left + 260, right - 28);
    draw_text(left, top, "Flight Plan", text_primary, xplmFont_Basic);

    if (!flight_plan.available) {
        draw_text(left, top - 28, "Waiting for X-Plane FMS data...", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Route status", "Flight-plan service is starting");
        return;
    }
    if (flight_plan.legs.empty()) {
        draw_text(left, top - 28, "No active route is loaded.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "X-Plane FMS", "Load or enter a flight plan to see it here");
        return;
    }

    const auto& first = flight_plan.legs.front();
    const auto& last = flight_plan.legs.back();
    char summary[128]{};
    std::snprintf(summary, sizeof(summary), "%s to %s  |  %zu legs  |  Active %d",
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

void draw_home_map(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                   const RouteProgressSnapshot& progress, const FuelSnapshot& fuel,
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
        draw_rectangle(static_cast<int>(point.x) - 3, static_cast<int>(point.y) + 3,
                       static_cast<int>(point.x) + 3, static_cast<int>(point.y) - 3,
                       waypoint_color);
        const bool label = leg.active || index == 0 || index + 1 == route_points.size();
        if (label && point.x < map_right - 80 && point.y < map_top - 18) {
            draw_text(static_cast<int>(point.x) + 8, static_cast<int>(point.y) + 5,
                      leg.identifier, waypoint_color);
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

void draw_page_content(EfbPage page, const TelemetrySnapshot& telemetry,
                       const FlightPlanSnapshot& flight_plan,
                       const FuelSnapshot& fuel,
                       const RouteProgressSnapshot& route_progress,
                       const WeatherSnapshot& weather,
                       const MovingMapModel& moving_map,
                       XPlaneMapTiles& map_tiles,
                       int left, int top, int right, int bottom) {
    const int card_right = std::max(left + 260, right - 28);
    switch (page) {
    case EfbPage::home:
        draw_home_map(telemetry, flight_plan, route_progress, fuel, moving_map, map_tiles,
                      left, top, right, bottom);
        break;
    case EfbPage::flight_plan:
        draw_flight_plan_page(flight_plan, left, top, right);
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
                  "Version", "0.8.0 - M8 moving map");
        draw_card(left, top - 178, card_right, top - 274,
                  "Project", "Built in the open for the flight-sim community");
        break;
    }

    draw_text(left, bottom + 22, "OPEN EFB  /  M8", text_muted);
}

} // namespace

std::unique_ptr<WindowSurface> XPlaneWindow::create(UiModel& ui_model, TelemetryModel& telemetry_model,
                                                    FlightPlanModel& flight_plan_model,
                                                    FuelModel& fuel_model,
                                                    MovingMapModel& moving_map_model,
                                                    RouteProgressModel& route_progress_model,
                                                    WeatherModel& weather_model,
                                                    XPlanePreferences& preferences) {
    auto window = std::unique_ptr<XPlaneWindow>(
        new XPlaneWindow(ui_model, telemetry_model, flight_plan_model, fuel_model, moving_map_model,
                         route_progress_model,
                         weather_model, preferences));
    if (!window->window_id_) {
        return nullptr;
    }
    return window;
}

XPlaneWindow::XPlaneWindow(UiModel& ui_model, TelemetryModel& telemetry_model,
                           FlightPlanModel& flight_plan_model,
                           FuelModel& fuel_model,
                           MovingMapModel& moving_map_model,
                           RouteProgressModel& route_progress_model,
                           WeatherModel& weather_model,
                           XPlanePreferences& preferences)
    : ui_model_(ui_model), telemetry_model_(telemetry_model),
      flight_plan_model_(flight_plan_model), fuel_model_(fuel_model),
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
                      fuel_model_.snapshot(), route_progress_model_.snapshot(),
                      weather_model_.snapshot(), moving_map_model_,
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
    if (window->ui_model_.active_page() == EfbPage::home) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
        const auto panel = home_map_geometry(content_left, content_top, right, bottom);
        const int topo_right = panel.right - 10;
        const int topo_left = topo_right - 64;
        const int street_right = topo_left - 6;
        const int street_left = street_right - 70;
        if (y <= panel.top - 7 && y >= panel.top - 32) {
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

void XPlaneWindow::handle_key(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID, int, int, void*) {
    return xplm_CursorDefault;
}

int XPlaneWindow::handle_wheel(XPLMWindowID window_id, int x, int y, int, int clicks, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window || window->ui_model_.active_page() != EfbPage::home) {
        return 0;
    }
    int left{};
    int top{};
    int right{};
    int bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
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
