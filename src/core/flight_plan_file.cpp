#include "openefb/core/flight_plan_file.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace openefb {
namespace {

WaypointKind kind_from_code(int code) {
    switch (code) {
    case 1: return WaypointKind::airport;
    case 2: return WaypointKind::ndb;
    case 3: return WaypointKind::vor;
    case 11: return WaypointKind::fix;
    case 28: return WaypointKind::coordinate;
    default: return WaypointKind::other;
    }
}

int code_from_kind(WaypointKind kind) {
    switch (kind) {
    case WaypointKind::airport: return 1;
    case WaypointKind::ndb: return 2;
    case WaypointKind::vor: return 3;
    case WaypointKind::fix: return 11;
    case WaypointKind::coordinate: return 28;
    default: return 28;
    }
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

} // namespace

FlightPlanFileResult parse_xplane_fms(std::istream& input) {
    std::string line;
    int expected = -1;
    std::vector<FlightPlanLeg> legs;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string keyword;
        fields >> keyword;
        if (keyword == "NUMENR") {
            fields >> expected;
            if (expected < 0 || expected > 100) {
                return {false, {}, "Flight plan has an invalid waypoint count"};
            }
            legs.reserve(static_cast<std::size_t>(expected));
            continue;
        }
        if (expected < 0 || static_cast<int>(legs.size()) >= expected || keyword.empty() ||
            !std::isdigit(static_cast<unsigned char>(keyword.front()))) {
            continue;
        }
        int code{};
        try { code = std::stoi(keyword); } catch (...) { continue; }
        FlightPlanLeg leg;
        std::string role;
        if (!(fields >> leg.identifier >> role >> leg.altitude_feet >>
              leg.latitude_degrees >> leg.longitude_degrees)) {
            return {false, {}, "Flight plan contains an unreadable waypoint"};
        }
        leg.identifier = uppercase(std::move(leg.identifier));
        leg.kind = kind_from_code(code);
        leg.index = static_cast<int>(legs.size());
        if (leg.identifier.empty()) {
            return {false, {}, "Flight plan contains an unnamed waypoint"};
        }
        legs.push_back(std::move(leg));
    }
    if (expected < 0) return {false, {}, "This is not an X-Plane 11/12 FMS plan"};
    if (static_cast<int>(legs.size()) != expected)
        return {false, {}, "Flight plan waypoint count does not match its contents"};
    if (legs.empty()) return {false, {}, "Flight plan contains no waypoints"};
    return {true, std::move(legs), "Flight plan imported"};
}

bool write_xplane_fms(std::ostream& output, const std::vector<FlightPlanLeg>& legs) {
    if (legs.empty() || legs.size() > 100) return false;
    output << "I\n1100 Version\nCYCLE 0000\n"
           << "ADEP " << legs.front().identifier << '\n'
           << "ADES " << legs.back().identifier << '\n'
           << "NUMENR " << legs.size() << '\n';
    output << std::fixed << std::setprecision(6);
    for (std::size_t index = 0; index < legs.size(); ++index) {
        const auto& leg = legs[index];
        const char* role = index == 0 ? "ADEP" : index + 1 == legs.size() ? "ADES" : "DRCT";
        output << code_from_kind(leg.kind) << ' ' << leg.identifier << ' ' << role << ' '
               << leg.altitude_feet << ' ' << leg.latitude_degrees << ' '
               << leg.longitude_degrees << '\n';
    }
    return static_cast<bool>(output);
}

std::string xplane_fms_filename(const std::vector<FlightPlanLeg>& legs) {
    if (legs.empty()) return "OpenEFB.fms";
    const auto safe_identifier = [](std::string_view identifier) {
        std::string safe;
        safe.reserve(identifier.size());
        for (const unsigned char character : identifier) {
            if (std::isalnum(character)) safe.push_back(static_cast<char>(std::toupper(character)));
            else if (character == '-' || character == '_') safe.push_back(static_cast<char>(character));
        }
        return safe.empty() ? std::string("UNKNOWN") : safe;
    };
    return safe_identifier(legs.front().identifier) + "-" +
           safe_identifier(legs.back().identifier) + ".fms";
}

} // namespace openefb
