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

constexpr int initial_width = 1000;
constexpr int initial_height = 700;
constexpr int minimum_width = 900;
constexpr int minimum_height = 640;

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
bool high_contrast_mode = false;

std::string shortened(std::string_view value, std::size_t maximum);

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
    if (high_contrast_mode) {
        color.red = std::max(color.red, 0.88F);
        color.green = std::max(color.green, 0.88F);
        color.blue = std::max(color.blue, 0.88F);
    }
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
                           std::string_view route_file_message,
                           int left, int top, int right, int bottom) {
    const int card_right = std::max(left + 260, right - 28);
    if (editor.active()) {
        draw_flight_plan_editor(editor, left, top, right, bottom);
        return;
    }
    draw_text(left, top, "Flight Plan", text_primary, xplmFont_Basic);
    draw_button(card_right - 338, top + 8, card_right - 224, top - 23, "Import latest");
    draw_button(card_right - 216, top + 8, card_right - 90, top - 23, "Export .fms");
    draw_button(card_right - 82, top + 8, card_right, top - 23, "Edit", true);

    if (!route_file_message.empty()) {
        draw_text(left, top - 28, route_file_message, accent);
    }

    if (!flight_plan.available) {
        if (route_file_message.empty()) draw_text(left, top - 28, "Waiting for X-Plane FMS data...", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Route status", "Flight-plan service is starting");
        return;
    }
    if (flight_plan.legs.empty()) {
        if (route_file_message.empty()) draw_text(left, top - 28, "No active route is loaded.", text_muted);
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
    draw_text(left, top - (route_file_message.empty() ? 28 : 50), summary, text_muted);

    const int active = std::clamp(flight_plan.active_leg_index, 0,
                                  static_cast<int>(flight_plan.legs.size()) - 1);
    const std::size_t start = active > 1 ? static_cast<std::size_t>(active - 1) : 0U;
    const std::size_t visible_count = std::min<std::size_t>(5, flight_plan.legs.size() - start);
    constexpr int row_height = 48;
    constexpr int row_gap = 6;
    int row_top = top - (route_file_message.empty() ? 58 : 78);
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
    const char* source = weather.source == WeatherSource::online ? "ONLINE"
        : weather.source == WeatherSource::simulator ? "X-PLANE"
        : weather.source == WeatherSource::cache ? "SAVED CACHE" : "NO SOURCE";
    draw_text(right - 108, top - 29, source,
              weather.source == WeatherSource::online ? connected : text_muted);

    if (weather.airport_id.empty()) {
        draw_text(left + 18, top - 60, "Airport not found in the active route", text_muted);
        return;
    }
    if (weather.metar.empty()) {
        draw_text(left + 18, top - 60, "No METAR is available from the internet, X-Plane, or cache", text_muted);
        draw_text(left + 18, top - 84, "Check the route and network connection", text_muted);
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
    draw_text(left, top - 28, "Internet first, with X-Plane weather and saved-cache fallback", text_muted);
    if (!weather.online_status.empty()) draw_text(left + 360, top - 28, weather.online_status, text_muted);
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

std::string format_fuel_flow(const FuelSnapshot& fuel) {
    if (!fuel.endurance_available) {
        return "Waiting for measurable engine fuel flow";
    }
    char value[96]{};
    std::snprintf(value, sizeof(value), "%.1f GPH  |  US gallons, 6.0 lb/gal basis",
                  fuel.fuel_flow_us_gallons_per_hour);
    return value;
}

std::string format_weight(double kilograms) {
    char value[64]{};
    std::snprintf(value, sizeof(value), "%.0f kg  /  %.0f lb", kilograms, kilograms * 2.2046226218);
    return value;
}

void draw_planning_page(const PlanningSnapshot& planning, int left, int top, int right) {
    const int card_right = std::max(left + 260, right - 28);
    draw_text(left, top, "Aircraft Planning", text_primary, xplmFont_Basic);
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
    std::snprintf(gross, sizeof(gross), "%.0f / %.0f kg  |  %.0f%%  |  %s",
                  planning.loading.gross_weight_kg, planning.loading.maximum_gross_weight_kg,
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
                      "Burn %.1f GPH  |  Trip %.1f kg  |  Reserve %.1f kg",
                      planning.fuel_flow_us_gallons_per_hour,
                      planning.trip_fuel_kg, planning.reserve_fuel_kg);
        draw_text(left, top - 356, plan,
                  planning.fuel_margin_kg >= 0.0 ? text_primary : Color{1.0F, 0.38F, 0.30F, 1.0F});
        char landing[150]{};
        std::snprintf(landing, sizeof(landing), "Landing %.0f kg  |  Fuel margin %.1f kg  |  %s",
                      planning.predicted_landing_weight_kg, planning.fuel_margin_kg,
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

ScreenPoint map_screen_point(const MovingMapModel& map,
                             double latitude, double longitude, int center_x, int center_y,
                             double pixels_per_nm) {
    const auto offset = map.project(map.center_latitude_degrees(), map.center_longitude_degrees(),
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
    int width;
};

constexpr std::array map_layer_buttons{
    MapLayerButton{MapLayer::weather, "WX", 38},
    MapLayerButton{MapLayer::airports, "APT", 38},
    MapLayerButton{MapLayer::navaids, "NAV", 38},
    MapLayerButton{MapLayer::airspace, "AIR", 38},
    MapLayerButton{MapLayer::food, "FOOD", 48},
    MapLayerButton{MapLayer::golf, "GOLF", 48},
    MapLayerButton{MapLayer::attractions, "SIGHTS", 56},
};

int map_layer_buttons_left(const HomeMapGeometry& panel) {
    const int topo_left = panel.right - 10 - 64;
    const int street_left = topo_left - 6 - 70;
    int width{};
    for (const auto& button : map_layer_buttons) width += button.width + 4;
    return street_left - 4 - width;
}

Color airspace_color(std::string_view class_code) {
    if (class_code == "B" || class_code == "C") return {0.30F, 0.65F, 1.0F, 0.92F};
    if (class_code == "D") return {0.70F, 0.45F, 1.0F, 0.92F};
    if (class_code == "P" || class_code == "R" || class_code == "Q")
        return {1.0F, 0.36F, 0.24F, 0.95F};
    return {1.0F, 0.72F, 0.25F, 0.85F};
}

void draw_airspace_overlay(const AirspaceSnapshot& airspace, const MovingMapModel& map,
                           int center_x, int center_y,
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
            auto point_1 = map_screen_point(map, first.latitude_degrees,
                                            first.longitude_degrees, center_x, center_y, pixels_per_nm);
            auto point_2 = map_screen_point(map, second.latitude_degrees,
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

Color poi_color(PoiCategory category) {
    switch (category) {
    case PoiCategory::food: return {1.0F, 0.58F, 0.18F, 1.0F};
    case PoiCategory::golf: return {0.36F, 0.92F, 0.46F, 1.0F};
    case PoiCategory::attraction: return {0.88F, 0.48F, 1.0F, 1.0F};
    }
    return accent;
}

bool poi_layer_enabled(const MovingMapModel& map, PoiCategory category) {
    switch (category) {
    case PoiCategory::food: return map.layer_enabled(MapLayer::food);
    case PoiCategory::golf: return map.layer_enabled(MapLayer::golf);
    case PoiCategory::attraction: return map.layer_enabled(MapLayer::attractions);
    }
    return false;
}

void draw_poi_marker(int x, int y, PoiCategory category) {
    const auto color = poi_color(category);
    draw_rectangle(x - 7, y + 7, x + 7, y - 7, status_bar);
    draw_border(x - 7, y + 7, x + 7, y - 7, color, 2.0F);
    if (category == PoiCategory::food) {
        draw_line(x - 2, y + 4, x - 2, y - 4, color, 2.0F);
        draw_line(x + 3, y + 4, x + 3, y - 4, color, 2.0F);
    } else if (category == PoiCategory::golf) {
        draw_line(x - 2, y + 5, x - 2, y - 5, color, 2.0F);
        glColor4f(color.red, color.green, color.blue, color.alpha);
        glBegin(GL_TRIANGLES);
        glVertex2i(x - 1, y + 5); glVertex2i(x + 5, y + 2); glVertex2i(x - 1, y);
        glEnd();
    } else {
        draw_diamond_marker(x, y, color);
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

void draw_home_map(const TelemetrySnapshot& telemetry, const FlightPlanSnapshot& flight_plan,
                   const RouteProgressSnapshot& progress, const FuelSnapshot& fuel,
                   const WeatherSnapshot& weather, const AirspaceSnapshot& airspace,
                   const NavigationDatabaseSnapshot& navigation_database,
                   const MovingMapModel& map, XPlaneMapTiles& tiles, XPlaneMapPois& pois,
                   std::vector<MapHitTarget>& hit_targets,
                   std::vector<PoiHitTarget>& poi_hit_targets,
                   const std::optional<MapPoi>& hovered_poi,
                   int left, int top, int right, int bottom) {
    hit_targets.clear();
    poi_hit_targets.clear();
    draw_text(left, top, "Live Moving Map", text_primary, xplmFont_Basic);
    draw_text(left + 142, top,
              "OpenStreetMap  /  wheel zoom  /  drag pan  /  click a place to add it",
              text_muted);
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
        draw_map_style_button(layer_left, panel.top - 7, layer_left + button.width, button.label,
                              map.layer_enabled(button.layer));
        layer_left += button.width + 4;
    }

    draw_text(panel.left + 14, panel.top - 23, map_range_label(map.range_nm()), text_muted);
    draw_rectangle(map_left, map_top, map_right, map_bottom, map_background);

    if (!telemetry.available) {
        draw_text(map_left + 22, map_top - 34, "Waiting for live aircraft position...", text_muted);
        return;
    }

    tiles.draw(map.style(), {map_left, map_top, map_right, map_bottom,
                             map.center_latitude_degrees(), map.center_longitude_degrees(),
                             map.range_nm()});
    if (map.layer_enabled(MapLayer::food) || map.layer_enabled(MapLayer::golf) ||
        map.layer_enabled(MapLayer::attractions)) {
        pois.update(map.center_latitude_degrees(), map.center_longitude_degrees(), map.range_nm());
    }
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
        draw_airspace_overlay(airspace, map, center_x, center_y, pixels_per_nm,
                              map_left, map_top, map_right, map_bottom);
    }

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
            const auto point = map_screen_point(map, nav.latitude_degrees,
                                                nav.longitude_degrees, center_x, center_y,
                                                pixels_per_nm);
            if (!point.valid || point.x < map_left + 10 || point.x > map_right - 10 ||
                point.y < map_bottom + 60 || point.y > map_top - 10) continue;
            const int marker_x = static_cast<int>(point.x);
            const int marker_y = static_cast<int>(point.y);
            if (airport) {
                draw_circle_marker(marker_x, marker_y, connected);
                FlightPlanLeg leg;
                leg.identifier = nav.identifier;
                leg.kind = WaypointKind::airport;
                leg.latitude_degrees = nav.latitude_degrees;
                leg.longitude_degrees = nav.longitude_degrees;
                hit_targets.push_back({marker_x, marker_y, std::move(leg)});
                if (map.range_nm() <= 120.0 && marker_x < map_right - 55) {
                    draw_text(marker_x + 8, marker_y + 5, nav.identifier, connected);
                }
            } else {
                draw_diamond_marker(marker_x, marker_y, text_muted);
            }
            if (++rendered >= 90) break;
        }
    }

    std::vector<std::pair<int, int>> occupied_poi_positions;
    const auto visible_pois = map.range_nm() <= 40.0 ? pois.snapshot() : std::vector<MapPoi>{};
    for (const auto& poi : visible_pois) {
        if (!poi_layer_enabled(map, poi.category)) continue;
        const auto point = map_screen_point(map, poi.latitude_degrees, poi.longitude_degrees,
                                            center_x, center_y, pixels_per_nm);
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
        draw_poi_marker(marker_x, marker_y, poi.category);
        poi_hit_targets.push_back({marker_x, marker_y, poi});
        if (poi_hit_targets.size() >= 80) break;
    }

    std::vector<ScreenPoint> route_points;
    route_points.reserve(flight_plan.legs.size());
    for (const auto& leg : flight_plan.legs) {
        route_points.push_back(map_screen_point(map, leg.latitude_degrees,
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

    const auto aircraft_point = map_screen_point(
        map, telemetry.latitude_degrees, telemetry.longitude_degrees,
        center_x, center_y, pixels_per_nm);
    if (aircraft_point.valid && aircraft_point.x >= map_left + 12 &&
        aircraft_point.x <= map_right - 12 && aircraft_point.y >= map_bottom + 62 &&
        aircraft_point.y <= map_top - 12) {
        draw_aircraft_symbol(static_cast<int>(aircraft_point.x),
                             static_cast<int>(aircraft_point.y), telemetry.heading_degrees);
    }
    draw_rectangle(map_left + 10, map_bottom + 54, map_right - 10, map_bottom + 28, status_bar);
    draw_text(map_left + 22, map_bottom + 36, map_progress_text(progress), text_primary);
    const std::string attribution = map.style() == MapStyle::street
        ? "(c) OpenStreetMap contributors"
        : "Map data (c) OpenStreetMap contributors, SRTM  |  Map style (c) OpenTopoMap (CC-BY-SA)";
    draw_rectangle(map_left, map_bottom + 22, map_right, map_bottom, status_bar);
    draw_text(map_left + 8, map_bottom + 6, attribution, text_muted);

    const int recenter_right = map_right - 10;
    const int recenter_left = recenter_right - 42;
    const int recenter_bottom = map_bottom + 62;
    const int recenter_top = recenter_bottom + 36;
    draw_rectangle(recenter_left, recenter_top, recenter_right, recenter_bottom, status_bar);
    draw_border(recenter_left, recenter_top, recenter_right, recenter_bottom,
                map.following_aircraft() ? connected : accent, 2.0F);
    draw_text(recenter_left + 9, recenter_bottom + 12, "HOME",
              map.following_aircraft() ? connected : text_primary);

    if (hovered_poi) {
        const int tooltip_left = map_left + 12;
        const int tooltip_top = map_top - 12;
        const int tooltip_right = std::min(map_right - 12, tooltip_left + 330);
        const int tooltip_bottom = tooltip_top - 62;
        draw_rectangle(tooltip_left, tooltip_top, tooltip_right, tooltip_bottom, status_bar);
        draw_border(tooltip_left, tooltip_top, tooltip_right, tooltip_bottom,
                    poi_color(hovered_poi->category), 2.0F);
        draw_text(tooltip_left + 12, tooltip_top - 23,
                  shortened(hovered_poi->name, 40), text_primary);
        draw_text(tooltip_left + 12, tooltip_top - 47,
                  std::string(poi_category_label(hovered_poi->category)) + "  /  " +
                      hovered_poi->detail,
                  poi_color(hovered_poi->category));
    }

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
    const int card_right = std::max(left + 420, right - 28);
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
    const int card_right = std::max(left + 420, right - 28);
    const auto [departure, destination] = briefing_airports(flight_plan);
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
    const std::string library_status = briefing.library_airport().empty()
        ? std::string(briefing.library_message())
        : std::string(briefing.library_airport()) + " briefing and charts  /  " +
              std::string(briefing.library_message());
    draw_text(left, top - 146, shortened(library_status, 72), text_muted);
    const int list_right = std::min(left + 230, card_right - 220);
    const int row_start = top - 158;
    constexpr int row_height = 34;
    const auto indices = filtered_library_indices(briefing);
    const std::size_t visible = std::min<std::size_t>(indices.size(), 8);
    const std::size_t start = library_visible_start(briefing, indices, visible);
    for (std::size_t offset = 0; offset < visible; ++offset) {
        const std::size_t index = indices[start + offset];
        const int row_top = row_start - static_cast<int>(offset) * (row_height + 4);
        const bool selected = static_cast<int>(index) == briefing.selected_entry_index();
        draw_rectangle(left, row_top, list_right, row_top - row_height,
                       selected ? active_navigation : card);
        const auto& entry = briefing.library()[index];
        draw_text(left + 8, row_top - 22,
                  std::string(entry.category == LibraryCategory::chart ? "CHART  " : "BRIEF  ") +
                      shortened(entry.name, 23),
                  selected ? text_primary : text_muted);
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
    const int card_right = std::max(left + 420, right - 28);
    char progress[64]{};
    std::snprintf(progress, sizeof(progress), "%zu of %zu complete",
                  briefing.completed_checklist_items(), briefing.checklist().size());
    draw_text(left, top - 112, progress, text_muted);
    draw_button(card_right - 78, top - 98, card_right, top - 131, "Reset");
    int row_top = top - 148;
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
    const int card_right = std::max(left + 420, right - 28);
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
    draw_text(left, top, "Flight Briefing", text_primary, xplmFont_Basic);
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

void draw_page_content(EfbPage page, const TelemetrySnapshot& telemetry,
                       const FlightPlanSnapshot& flight_plan,
                       const FlightPlanEditor& flight_plan_editor,
                       std::string_view route_file_message,
                       std::string_view airport_query,
                       const AirportInfoSnapshot& airport_info,
                       const FuelSnapshot& fuel,
                       const PlanningSnapshot& planning,
                       const BriefingModel& briefing,
                       const RouteProgressSnapshot& route_progress,
                       const WeatherSnapshot& weather,
                       const AirspaceSnapshot& airspace,
                       const NavigationDatabaseSnapshot& navigation_database,
                       const MovingMapModel& moving_map,
                       XPlaneMapTiles& map_tiles, XPlaneMapPois& map_pois,
                       std::vector<MapHitTarget>& map_hit_targets,
                       std::vector<PoiHitTarget>& poi_hit_targets,
                       const std::optional<MapPoi>& hovered_map_poi,
                       const DisplayPreferences& display_preferences,
                       int left, int top, int right, int bottom) {
    const int card_right = std::max(left + 260, right - 28);
    switch (page) {
    case EfbPage::home:
        draw_home_map(telemetry, flight_plan, route_progress, fuel, weather, airspace,
                      navigation_database, moving_map, map_tiles, map_pois, map_hit_targets,
                      poi_hit_targets, hovered_map_poi,
                      left, top, right, bottom);
        break;
    case EfbPage::flight_plan:
        draw_flight_plan_page(flight_plan, flight_plan_editor, route_file_message,
                              left, top, right, bottom);
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
    case EfbPage::planning:
        draw_planning_page(planning, left, top, right);
        break;
    case EfbPage::briefing:
        draw_briefing_page(briefing, telemetry, flight_plan, route_progress, weather, planning,
                           left, top, right, bottom);
        break;
    case EfbPage::settings:
        draw_text(left, top, "Settings", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "Saved display and accessibility preferences.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Window geometry", "Position and size save automatically");
        draw_card(left, top - 178, card_right, top - 274, "High contrast text",
                  display_preferences.high_contrast ? "Enabled - maximum text visibility" :
                                                       "Disabled - standard cockpit palette");
        draw_button(card_right - 122, top - 207, card_right - 18, top - 240,
                    display_preferences.high_contrast ? "Turn off" : "Turn on", true);
        draw_card(left, top - 294, card_right, top - 390, "Comfort-size window",
                  display_preferences.comfort_size ? "Enabled - 1000 x 700 minimum" :
                                                     "Disabled - 900 x 640 minimum");
        draw_button(card_right - 122, top - 323, card_right - 18, top - 356,
                    display_preferences.comfort_size ? "Compact" : "Enlarge", true);
        break;
    case EfbPage::about:
        draw_text(left, top, "About OpenEFB", text_primary, xplmFont_Basic);
        draw_text(left, top - 28, "An open-source electronic flight bag for X-Plane 12.", text_muted);
        draw_card(left, top - 62, card_right, top - 158,
                  "Version", "1.0.0 RC6 - interactive places and reliable charts");
        draw_card(left, top - 178, card_right, top - 274,
                  "Project", "Built in the open for the flight-sim community");
        break;
    }

    draw_text(left, bottom + 22, "OPEN EFB  /  1.0 RC6", text_muted);
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
                                                    XPlanePreferences& preferences) {
    auto window = std::unique_ptr<XPlaneWindow>(
        new XPlaneWindow(ui_model, telemetry_model, flight_plan_model, flight_plan_editor,
                         xplane_flight_plan, airport_info_model, xplane_airport_data,
                         airspace_model,
                         fuel_model, planning_model, briefing_model, briefing_library,
                         moving_map_model, navigation_database_model,
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
                           PlanningModel& planning_model,
                           BriefingModel& briefing_model,
                           XPlaneBriefingLibrary& briefing_library,
                           MovingMapModel& moving_map_model,
                           NavigationDatabaseModel& navigation_database_model,
                           RouteProgressModel& route_progress_model,
                           WeatherModel& weather_model,
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
      preferences_(preferences), map_tiles_(preferences.map_cache_directory()) {
    display_preferences_ = preferences_.load_display_preferences();
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

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    draw_rectangle(left, top, right, bottom, canvas);
    draw_rectangle(left, top, right, top - status_bar_height, status_bar);
    draw_rectangle(left, top - status_bar_height, left + sidebar_width, bottom, sidebar);

    draw_text(left + 20, top - 35, "OpenEFB", accent, xplmFont_Basic);
    draw_text(left + sidebar_width + 24, top - 35, ui_model_.active_page_title(), text_primary);
    const auto& telemetry = telemetry_model_.snapshot();
    if (telemetry.available) {
        moving_map_model_.update_aircraft_position(telemetry.latitude_degrees,
                                                   telemetry.longitude_degrees);
    }
    const auto map_source = map_tiles_.source();
    const std::string_view map_connection = map_source == MapTileSource::online
        ? "MAP ONLINE"
        : map_source == MapTileSource::cache ? "MAP CACHED" : "MAP LOADING";
    draw_text(right - 318, top - 35, map_connection,
              map_source == MapTileSource::online ? connected : text_muted);
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
    draw_rectangle(content_left - 18, content_top + 22, right - 10, bottom + 10, sidebar);
    draw_rectangle(content_left - 14, content_top + 18, right - 14, bottom + 14,
                   Color{0.047F, 0.071F, 0.098F, 1.0F});
    draw_border(content_left - 14, content_top + 18, right - 14, bottom + 14,
                Color{0.12F, 0.20F, 0.27F, 1.0F}, 1.0F);
    draw_page_content(ui_model_.active_page(), telemetry, flight_plan_model_.snapshot(),
                      flight_plan_editor_,
                      route_file_message_,
                      airport_query_, airport_info_model_.snapshot(),
                      fuel_model_.snapshot(), planning_model_.snapshot(), briefing_model_,
                      route_progress_model_.snapshot(),
                      weather_model_.snapshot(), airspace_model_.snapshot(),
                      navigation_database_model_.snapshot(), moving_map_model_, map_tiles_, map_pois_,
                      map_hit_targets_, poi_hit_targets_, hovered_map_poi_,
                      display_preferences_,
                      content_left, content_top, right, bottom);
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
        const int center_x = (left + right) / 2;
        const int center_y = (top + bottom) / 2;
        draw_rectangle(center_x - 232, center_y + 92, center_x + 238, center_y - 92, sidebar);
        draw_rectangle(center_x - 226, center_y + 86, center_x + 226, center_y - 86, status_bar);
        draw_border(center_x - 226, center_y + 86, center_x + 226, center_y - 86, accent, 2.0F);
        draw_text(center_x - 198, center_y + 53,
                  "Add " + shortened(pending_map_navigation_->name, 42) + " to the route?",
                  text_primary);
        draw_text(center_x - 198, center_y + 25,
                  shortened(pending_map_navigation_->detail, 56), accent);
        draw_text(center_x - 198, center_y - 3,
                  "It will be inserted after the active FMS waypoint and selected for navigation.",
                  text_muted);
        draw_button(center_x - 198, center_y - 35, center_x - 88, center_y - 68, "Cancel");
        draw_button(center_x + 36, center_y - 35, center_x + 198, center_y - 68,
                    "Add to FMS", true);
    }
    if (pdf_viewer_.visible()) {
        const int viewer_left = left + sidebar_width + 18;
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
        pdf_viewer_.draw(document_left, document_top, document_right, document_bottom);
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
        if (window->ui_model_.active_page() == EfbPage::home && window->map_dragging_) {
            const int content_left = left + sidebar_width + 30;
            const int content_top = top - status_bar_height - 38;
            const auto panel = home_map_geometry(content_left, content_top, right, bottom);
            const int map_left = panel.left + 3;
            const int map_right = panel.right - 3;
            const int map_top = panel.tile_top;
            const int map_bottom = panel.bottom + 3;
            const double radius_pixels = std::max(
                40.0, std::min(map_right - map_left, map_top - map_bottom) * 0.46);
            const double pixels_per_nm = radius_pixels / window->moving_map_model_.range_nm();
            const int delta_x = x - window->map_drag_start_x_;
            const int delta_y = y - window->map_drag_start_y_;
            if (std::abs(delta_x) > 3 || std::abs(delta_y) > 3) {
                window->map_drag_moved_ = true;
            }
            if (status == xplm_MouseDrag && window->map_drag_moved_) {
                const auto coordinate = window->moving_map_model_.unproject(
                    window->map_drag_start_latitude_, window->map_drag_start_longitude_,
                    -delta_x / pixels_per_nm, -delta_y / pixels_per_nm);
                if (coordinate.valid) {
                    window->moving_map_model_.pan_to(coordinate.latitude_degrees,
                                                     coordinate.longitude_degrees);
                }
                return 1;
            }
            if (status == xplm_MouseUp) {
                if (!window->map_drag_moved_ && x >= map_left && x <= map_right &&
                    y >= map_bottom && y <= map_top) {
                    const int center_x = (map_left + map_right) / 2;
                    const int center_y = (map_top + map_bottom) / 2;
                    const auto coordinate = window->moving_map_model_.unproject(
                        window->moving_map_model_.center_latitude_degrees(),
                        window->moving_map_model_.center_longitude_degrees(),
                        (x - center_x) / pixels_per_nm, (y - center_y) / pixels_per_nm);
                    if (coordinate.valid) {
                        FlightPlanLeg point;
                        point.identifier = "MAP POINT";
                        point.kind = WaypointKind::coordinate;
                        point.latitude_degrees = coordinate.latitude_degrees;
                        point.longitude_degrees = coordinate.longitude_degrees;
                        window->pending_map_navigation_ = PendingMapNavigation{
                            std::move(point), "Map point", "Custom map coordinate"};
                    }
                }
                window->map_dragging_ = false;
                window->map_drag_moved_ = false;
                return 1;
            }
        }
        return 1;
    }
    if (window->pdf_viewer_.visible()) {
        const int viewer_left = left + sidebar_width + 18;
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
    if (window->pending_map_navigation_) {
        const int center_x = (left + right) / 2;
        const int center_y = (top + bottom) / 2;
        if (y <= center_y - 35 && y >= center_y - 68) {
            if (x >= center_x - 196 && x <= center_x - 86) {
                window->pending_map_navigation_.reset();
                return 1;
            }
            if (x >= center_x + 36 && x <= center_x + 198) {
                const auto result = window->xplane_flight_plan_.insert_after_active(
                    window->pending_map_navigation_->leg,
                    window->pending_map_navigation_->name);
                window->map_action_message_ = result.message;
                window->pending_map_navigation_.reset();
                return 1;
            }
        }
        return 1;
    }
    if (window->ui_model_.active_page() == EfbPage::briefing) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
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
        const int card_right = std::max(content_left + 420, right - 28);
        if (window->briefing_model_.active_tab() == BriefingTab::library) {
            const auto [departure, destination] = briefing_airports(window->flight_plan_model_.snapshot());
            if (y <= content_top - 98 && y >= content_top - 131) {
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
            const int row_start = content_top - 158;
            const auto indices = filtered_library_indices(window->briefing_model_);
            const std::size_t visible = std::min<std::size_t>(indices.size(), 8);
            const std::size_t start = library_visible_start(window->briefing_model_, indices, visible);
            for (std::size_t offset = 0; offset < visible; ++offset) {
                const int row_top = row_start - static_cast<int>(offset) * (row_height + 4);
                if (x >= content_left && x <= list_right && y <= row_top && y >= row_top - row_height) {
                    window->briefing_model_.select_entry(static_cast<int>(indices[start + offset]));
                    return 1;
                }
            }
        } else if (window->briefing_model_.active_tab() == BriefingTab::checklist) {
            if (x >= card_right - 78 && x <= card_right &&
                y <= content_top - 98 && y >= content_top - 131) {
                window->briefing_model_.reset_checklist();
                return 1;
            }
            int row_top = content_top - 148;
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
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
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
            if (y <= content_top + 8 && y >= content_top - 23) {
                if (x >= card_right - 338 && x <= card_right - 224) {
                    window->import_latest_route();
                    return 1;
                }
                if (x >= card_right - 216 && x <= card_right - 90) {
                    window->export_current_route();
                    return 1;
                }
            }
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
    if (window->ui_model_.active_page() == EfbPage::settings) {
        const int content_left = left + sidebar_width + 30;
        const int content_top = top - status_bar_height - 38;
        const int card_right = std::max(content_left + 260, right - 28);
        if (x >= card_right - 122 && x <= card_right - 18) {
            if (y <= content_top - 207 && y >= content_top - 240) {
                window->toggle_high_contrast();
                return 1;
            }
            if (y <= content_top - 323 && y >= content_top - 356) {
                window->toggle_comfort_size();
                return 1;
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
                if (x >= layer_left && x <= layer_left + button.width) {
                    window->moving_map_model_.toggle_layer(button.layer);
                    return 1;
                }
                layer_left += button.width + 4;
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
        if (x >= map_right - 52 && x <= map_right - 10 &&
            y >= map_bottom + 62 && y <= map_bottom + 98) {
            window->moving_map_model_.recenter_on_aircraft();
            window->map_action_message_ = "Map centered on aircraft";
            return 1;
        }
        for (const auto& target : window->poi_hit_targets_) {
            if (std::abs(x - target.x) <= 10 && std::abs(y - target.y) <= 10) {
                FlightPlanLeg leg;
                leg.identifier = "POI";
                leg.kind = WaypointKind::coordinate;
                leg.latitude_degrees = target.poi.latitude_degrees;
                leg.longitude_degrees = target.poi.longitude_degrees;
                window->pending_map_navigation_ = PendingMapNavigation{
                    std::move(leg), target.poi.name,
                    std::string(poi_category_label(target.poi.category)) + "  /  " +
                        target.poi.detail};
                window->map_action_message_.clear();
                return 1;
            }
        }
        for (const auto& target : window->map_hit_targets_) {
            if (std::abs(x - target.x) <= 11 && std::abs(y - target.y) <= 11) {
                window->pending_map_navigation_ = PendingMapNavigation{
                    target.leg, target.leg.identifier, "Airport waypoint"};
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
    } else if (window->ui_model_.active_page() == EfbPage::briefing) {
        window->handle_briefing_key(key, virtual_key);
    }
}

XPLMCursorStatus XPlaneWindow::handle_cursor(XPLMWindowID window_id, int x, int y, void* refcon) {
    auto* window = static_cast<XPlaneWindow*>(refcon);
    if (!window || window->ui_model_.active_page() != EfbPage::home) {
        if (window) window->hovered_map_poi_.reset();
        return xplm_CursorDefault;
    }
    for (const auto& target : window->poi_hit_targets_) {
        if (std::abs(x - target.x) <= 10 && std::abs(y - target.y) <= 10) {
            window->hovered_map_poi_ = target.poi;
            return xplm_CursorArrow;
        }
    }
    window->hovered_map_poi_.reset();
    int left{}, top{}, right{}, bottom{};
    XPLMGetWindowGeometry(window_id, &left, &top, &right, &bottom);
    const int content_left = left + sidebar_width + 30;
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
        const int viewer_left = left + sidebar_width + 18;
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
    if (window->ui_model_.active_page() == EfbPage::briefing &&
        window->briefing_model_.active_tab() == BriefingTab::library) {
        const int content_left = left + sidebar_width + 30;
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
    const int map_left = panel.left + 3;
    const int map_right = panel.right - 3;
    const int map_top = panel.tile_top;
    const int map_bottom = panel.bottom + 3;
    const int center_x = (map_left + map_right) / 2;
    const int center_y = (map_top + map_bottom) / 2;
    const double radius_pixels = std::max(
        40.0, std::min(map_right - map_left, map_top - map_bottom) * 0.46);
    const double old_pixels_per_nm = radius_pixels / window->moving_map_model_.range_nm();
    const auto anchor = window->moving_map_model_.unproject(
        window->moving_map_model_.center_latitude_degrees(),
        window->moving_map_model_.center_longitude_degrees(),
        (x - center_x) / old_pixels_per_nm, (y - center_y) / old_pixels_per_nm);
    if (window->moving_map_model_.apply_wheel(clicks) && anchor.valid &&
        (std::abs(x - center_x) > 4 || std::abs(y - center_y) > 4)) {
        const double new_pixels_per_nm = radius_pixels / window->moving_map_model_.range_nm();
        const auto new_center = window->moving_map_model_.unproject(
            anchor.latitude_degrees, anchor.longitude_degrees,
            -(x - center_x) / new_pixels_per_nm,
            -(y - center_y) / new_pixels_per_nm);
        if (new_center.valid) {
            window->moving_map_model_.pan_to(new_center.latitude_degrees,
                                             new_center.longitude_degrees);
        }
    }
    return 1;
}

} // namespace openefb::xplane
