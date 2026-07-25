#include "openefb/core/airspace_model.hpp"

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>

namespace openefb {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double earth_radius_nm = 3440.065;
constexpr std::size_t maximum_zones = 20000;
constexpr std::size_t maximum_points_per_zone = 2000;

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

double coordinate_component(const std::string& value) {
    std::stringstream stream(value);
    std::string part;
    double result = 0.0;
    double divisor = 1.0;
    while (std::getline(stream, part, ':')) {
        result += std::stod(part) / divisor;
        divisor *= 60.0;
    }
    return result;
}

bool parse_coordinate(const std::string& text, GeoPoint& point) {
    static const std::regex pattern(
        R"(([0-9]+(?:\.[0-9]+)?(?::[0-9]+(?:\.[0-9]+)?){0,2})\s*([NS])\s*[, ]+\s*([0-9]+(?:\.[0-9]+)?(?::[0-9]+(?:\.[0-9]+)?){0,2})\s*([EW]))",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(text, match, pattern)) return false;
    try {
        point.latitude_degrees = coordinate_component(match[1].str());
        point.longitude_degrees = coordinate_component(match[3].str());
        if (match[2].str()[0] == 'S' || match[2].str()[0] == 's') point.latitude_degrees *= -1.0;
        if (match[4].str()[0] == 'W' || match[4].str()[0] == 'w') point.longitude_degrees *= -1.0;
        return point.latitude_degrees <= 90.0 && point.longitude_degrees <= 180.0;
    } catch (...) {
        return false;
    }
}

double radians(double degrees) { return degrees * pi / 180.0; }
double degrees(double radians_value) { return radians_value * 180.0 / pi; }

GeoPoint destination(GeoPoint center, double bearing_degrees, double distance_nm) {
    const double angular = distance_nm / earth_radius_nm;
    const double lat1 = radians(center.latitude_degrees);
    const double lon1 = radians(center.longitude_degrees);
    const double bearing = radians(bearing_degrees);
    const double lat2 = std::asin(std::sin(lat1) * std::cos(angular) +
                                  std::cos(lat1) * std::sin(angular) * std::cos(bearing));
    const double lon2 = lon1 + std::atan2(std::sin(bearing) * std::sin(angular) * std::cos(lat1),
                                          std::cos(angular) - std::sin(lat1) * std::sin(lat2));
    return {degrees(lat2), std::remainder(degrees(lon2), 360.0)};
}

double distance_nm(GeoPoint from, GeoPoint to) {
    const double dlat = radians(to.latitude_degrees - from.latitude_degrees);
    const double dlon = radians(to.longitude_degrees - from.longitude_degrees);
    const double a = std::pow(std::sin(dlat / 2.0), 2) + std::cos(radians(from.latitude_degrees)) *
        std::cos(radians(to.latitude_degrees)) * std::pow(std::sin(dlon / 2.0), 2);
    return earth_radius_nm * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

double bearing(GeoPoint from, GeoPoint to) {
    const double lat1 = radians(from.latitude_degrees);
    const double lat2 = radians(to.latitude_degrees);
    const double dlon = radians(to.longitude_degrees - from.longitude_degrees);
    return std::fmod(degrees(std::atan2(std::sin(dlon) * std::cos(lat2),
        std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dlon))) + 360.0, 360.0);
}

void append_arc(AirspaceZone& zone, GeoPoint center, double radius, double start,
                double end, bool clockwise) {
    if (!(radius > 0.0) || zone.boundary.size() >= maximum_points_per_zone) return;
    double sweep = clockwise ? std::fmod(end - start + 360.0, 360.0)
                             : -std::fmod(start - end + 360.0, 360.0);
    if (std::abs(sweep) < 0.001) sweep = clockwise ? 360.0 : -360.0;
    const int steps = std::clamp(static_cast<int>(std::ceil(std::abs(sweep) / 7.5)), 2, 96);
    for (int i = 0; i <= steps && zone.boundary.size() < maximum_points_per_zone; ++i) {
        zone.boundary.push_back(destination(center, start + sweep * i / steps, radius));
    }
}

} // namespace

AirspaceModel::AirspaceModel() {
    snapshot_.zones = std::make_shared<const std::vector<AirspaceZone>>();
}

void AirspaceModel::begin_load() { update(AirspaceLoadState::loading, {}, "Loading airspace"); }

void AirspaceModel::update(AirspaceLoadState state,
                           std::shared_ptr<const std::vector<AirspaceZone>> zones,
                           std::string message) {
    std::lock_guard lock(mutex_);
    snapshot_.state = state;
    snapshot_.zones = zones ? std::move(zones) : std::make_shared<const std::vector<AirspaceZone>>();
    snapshot_.message = std::move(message);
    ++snapshot_.revision;
}

AirspaceSnapshot AirspaceModel::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

std::vector<AirspaceZone> parse_openair(std::istream& input) {
    std::vector<AirspaceZone> zones;
    AirspaceZone current;
    GeoPoint center{};
    bool has_center = false;
    bool clockwise = true;
    auto finish = [&] {
        if (current.boundary.size() >= 3 && zones.size() < maximum_zones) zones.push_back(std::move(current));
        current = {};
        has_center = false;
        clockwise = true;
    };
    std::string line;
    while (std::getline(input, line) && zones.size() < maximum_zones) {
        line = trim(line);
        if (line.size() < 2 || line[0] == '*') continue;
        const std::string command = line.substr(0, 2);
        const std::string value = trim(line.substr(2));
        if (command == "AC") { finish(); current.class_code = value; }
        else if (command == "AN") current.name = value;
        else if (command == "AL") current.floor = value;
        else if (command == "AH") current.ceiling = value;
        else if (command == "DP") {
            GeoPoint point;
            if (parse_coordinate(value, point) && current.boundary.size() < maximum_points_per_zone) current.boundary.push_back(point);
        } else if (command == "V " && value.rfind("X=", 0) == 0) {
            has_center = parse_coordinate(value.substr(2), center);
        } else if (command == "V " && value.rfind("D=", 0) == 0) {
            clockwise = value.find('-') == std::string::npos;
        } else if (command == "DC" && has_center) {
            try { append_arc(current, center, std::stod(value), 0.0, 0.0, true); } catch (...) {}
        } else if (command == "DA" && has_center) {
            std::stringstream values(value);
            std::string radius_text, start_text, end_text;
            if (std::getline(values, radius_text, ',') && std::getline(values, start_text, ',') && std::getline(values, end_text)) {
                try { append_arc(current, center, std::stod(radius_text), std::stod(start_text), std::stod(end_text), clockwise); } catch (...) {}
            }
        } else if (command == "DB" && has_center) {
            const auto comma = value.find(',');
            GeoPoint first, second;
            if (comma != std::string::npos && parse_coordinate(value.substr(0, comma), first) && parse_coordinate(value.substr(comma + 1), second)) {
                append_arc(current, center, distance_nm(center, first), bearing(center, first), bearing(center, second), clockwise);
            }
        }
    }
    finish();
    return zones;
}

} // namespace openefb
