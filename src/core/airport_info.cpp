#include "openefb/core/airport_info.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>
#include <map>
#include <utility>

namespace openefb {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double earth_radius_metres = 6371000.0;
constexpr double metres_to_feet = 3.280839895;

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n;");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> fields(std::string_view line) {
    std::istringstream stream{std::string(line)};
    std::vector<std::string> result;
    std::string value;
    while (stream >> value) result.push_back(value);
    return result;
}

std::string join(const std::vector<std::string>& values, std::size_t start) {
    std::string result;
    for (std::size_t index = start; index < values.size(); ++index) {
        if (!result.empty()) result += ' ';
        result += values[index];
    }
    return result;
}

double runway_length(double lat1, double lon1, double lat2, double lon2) {
    const double p1 = lat1 * pi / 180.0;
    const double p2 = lat2 * pi / 180.0;
    const double dp = (lat2 - lat1) * pi / 180.0;
    const double dl = (lon2 - lon1) * pi / 180.0;
    const double a = std::sin(dp / 2.0) * std::sin(dp / 2.0) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2.0) * std::sin(dl / 2.0);
    return earth_radius_metres * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a)) * metres_to_feet;
}

std::string surface_name(int code) {
    if (code == 1 || (code >= 20 && code <= 38)) return "Asphalt";
    if (code == 2 || (code >= 50 && code <= 57)) return "Concrete";
    switch (code) {
    case 3: return "Turf";
    case 4: return "Dirt";
    case 5: return "Gravel";
    case 12: return "Dry lakebed";
    case 14: return "Snow / ice";
    case 15: return "Transparent";
    default: return "Other";
    }
}

std::string frequency_type(int code) {
    static constexpr std::array names{"ATIS", "UNICOM", "CLEARANCE", "GROUND", "TOWER", "APPROACH", "DEPARTURE"};
    const int normalized = code >= 1050 ? code - 1050 : code - 50;
    return normalized >= 0 && normalized < static_cast<int>(names.size()) ? names[normalized] : "COM";
}

void append_unique(std::vector<std::string>& target, const std::set<std::string>& values) {
    for (const auto& value : values) {
        target.push_back(value);
    }
}

std::vector<std::string> comma_fields(std::string_view payload) {
    std::istringstream stream(std::string{payload});
    std::vector<std::string> columns;
    std::string value;
    while (std::getline(stream, value, ',')) columns.push_back(trim(value));
    return columns;
}

int numeric_altitude(const std::vector<std::string>& columns) {
    if (columns.size() <= 23 || columns[23].empty()) return 0;
    try {
        return std::stoi(columns[23]);
    } catch (...) {
        return 0;
    }
}

std::optional<double> cifp_coordinate(std::string value) {
    value = trim(std::move(value));
    if (value.size() < 8) return std::nullopt;
    const char hemisphere = value.front();
    const bool latitude = hemisphere == 'N' || hemisphere == 'S';
    const bool longitude = hemisphere == 'E' || hemisphere == 'W';
    if (!latitude && !longitude) return std::nullopt;
    const std::size_t degree_digits = latitude ? 2 : 3;
    if (value.size() <= 1 + degree_digits + 4) return std::nullopt;
    try {
        const double degrees = std::stod(value.substr(1, degree_digits));
        const std::string angle_digits = value.substr(1 + degree_digits);
        if (angle_digits.size() < 4 ||
            !std::all_of(angle_digits.begin(), angle_digits.end(), [](unsigned char character) {
                return std::isdigit(character) != 0;
            })) return std::nullopt;
        const double minutes = std::stod(angle_digits.substr(0, 2));
        const std::string second_digits = angle_digits.substr(2);
        const double seconds = std::stod(second_digits) /
            std::pow(10.0, static_cast<int>(second_digits.size()) - 2);
        double coordinate = degrees + minutes / 60.0 + seconds / 3600.0;
        if (hemisphere == 'S' || hemisphere == 'W') coordinate = -coordinate;
        return coordinate;
    } catch (...) {
        return std::nullopt;
    }
}

std::string approach_runway(std::string_view identifier) {
    if (identifier.size() < 2) return {};
    const auto first_digit = identifier.find_first_of("0123456789");
    if (first_digit == std::string_view::npos) return {};
    std::string runway;
    for (std::size_t index = first_digit;
         index < identifier.size() && runway.size() < 2 &&
         std::isdigit(static_cast<unsigned char>(identifier[index])); ++index)
        runway.push_back(identifier[index]);
    if (runway.empty()) return {};
    const std::size_t suffix = first_digit + runway.size();
    if (suffix < identifier.size() &&
        (identifier[suffix] == 'L' || identifier[suffix] == 'R' || identifier[suffix] == 'C'))
        runway.push_back(identifier[suffix]);
    return runway;
}

std::string approach_display_name(std::string_view identifier) {
    if (identifier.empty()) return "Unknown approach";
    const std::string runway = approach_runway(identifier);
    std::string type;
    switch (identifier.front()) {
    case 'I': type = "ILS"; break;
    case 'L': type = "LOC"; break;
    case 'B': type = "LOC BC"; break;
    case 'R': case 'P': type = "RNAV (GPS)"; break;
    case 'N': type = "NDB"; break;
    case 'D': type = "VOR/DME"; break;
    case 'S': case 'V': type = "VOR"; break;
    default: type = std::string(identifier); break;
    }
    if (!runway.empty()) return type + " RWY " + runway;
    std::string suffix(identifier.substr(1));
    if (!suffix.empty()) type += "-" + suffix;
    return type;
}

void append_leg(std::vector<ApproachLeg>& legs, ApproachLeg leg) {
    if (leg.identifier.empty()) return;
    if (!legs.empty() && legs.back().identifier == leg.identifier &&
        legs.back().sequence == leg.sequence) return;
    legs.push_back(std::move(leg));
}

} // namespace

void AirportInfoModel::begin_search(std::string identifier) {
    std::lock_guard lock(mutex_);
    snapshot_ = {};
    snapshot_.state = AirportLookupState::loading;
    snapshot_.identifier = uppercase(std::move(identifier));
    snapshot_.message = "Searching installed X-Plane airport data...";
    snapshot_.revision = next_revision_++;
}

void AirportInfoModel::update(AirportInfoSnapshot snapshot) {
    std::lock_guard lock(mutex_);
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

AirportInfoSnapshot AirportInfoModel::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

std::optional<AirportInfoSnapshot> parse_airport_apt(std::istream& input, std::string identifier) {
    identifier = uppercase(std::move(identifier));
    AirportInfoSnapshot current;
    bool in_airport = false;
    bool modern_frequencies = false;
    auto matches = [&] { return uppercase(current.identifier) == identifier; };
    auto finalize = [&]() -> std::optional<AirportInfoSnapshot> {
        if (in_airport && matches()) {
            current.state = AirportLookupState::ready;
            return current;
        }
        return std::nullopt;
    };

    std::string line;
    while (std::getline(input, line)) {
        const auto tokens = fields(line);
        if (tokens.empty()) continue;
        int code{};
        try { code = std::stoi(tokens[0]); } catch (...) { continue; }
        if (code == 1 || code == 16 || code == 17) {
            if (auto result = finalize()) return result;
            current = {};
            modern_frequencies = false;
            in_airport = tokens.size() >= 6;
            if (in_airport) {
                current.elevation_feet = std::stoi(tokens[1]);
                current.identifier = uppercase(tokens[4]);
                current.name = join(tokens, 5);
            }
            continue;
        }
        if (!in_airport) continue;
        try {
            if (code == 1302 && tokens.size() >= 3 &&
                (tokens[1] == "icao_code" || tokens[1] == "icao_id")) {
                current.identifier = uppercase(tokens[2]);
            } else if (code == 100 && tokens.size() >= 20) {
                current.runways.push_back({tokens[8] + " / " + tokens[17],
                    runway_length(std::stod(tokens[9]), std::stod(tokens[10]),
                                  std::stod(tokens[18]), std::stod(tokens[19])),
                    std::stod(tokens[1]) * metres_to_feet, surface_name(std::stoi(tokens[2]))});
            } else if (code >= 50 && code <= 56 && tokens.size() >= 2 && !modern_frequencies) {
                current.frequencies.push_back({frequency_type(code), std::stod(tokens[1]) / 100.0,
                                               join(tokens, 2)});
            } else if (code >= 1050 && code <= 1056 && tokens.size() >= 2) {
                if (!modern_frequencies) {
                    current.frequencies.clear();
                    modern_frequencies = true;
                }
                current.frequencies.push_back({frequency_type(code), std::stod(tokens[1]) / 1000.0,
                                               join(tokens, 2)});
            }
        } catch (...) {
        }
    }
    return finalize();
}

void parse_airport_procedures(std::istream& input, AirportInfoSnapshot& airport) {
    std::set<std::string> departures;
    std::set<std::string> arrivals;
    std::set<std::string> approaches;
    std::vector<std::string> lines;
    std::map<std::string, std::pair<double, double>> runway_coordinates;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
        if (line.rfind("RWY:", 0) != 0) continue;
        const auto semicolon = line.find(';');
        if (semicolon == std::string::npos) continue;
        const auto runway_columns = comma_fields(std::string_view(line).substr(4, semicolon - 4));
        const auto coordinate_columns = comma_fields(std::string_view(line).substr(semicolon + 1));
        if (runway_columns.empty() || coordinate_columns.size() < 2) continue;
        const auto latitude = cifp_coordinate(coordinate_columns[0]);
        const auto longitude = cifp_coordinate(coordinate_columns[1]);
        if (latitude && longitude)
            runway_coordinates[uppercase(runway_columns[0])] = {*latitude, *longitude};
    }

    std::map<std::string, ApproachProcedure> details;
    for (const auto& procedure_line : lines) {
        line = procedure_line;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string type = line.substr(0, colon);
        const auto columns = comma_fields(std::string_view(line).substr(colon + 1));
        if (columns.size() < 3 || columns[2].empty()) continue;
        if (type == "SID") departures.insert(columns[2]);
        else if (type == "STAR") arrivals.insert(columns[2]);
        else if (type == "APPCH") {
            const std::string identifier = uppercase(columns[2]);
            approaches.insert(identifier);
            auto& procedure = details[identifier];
            procedure.identifier = identifier;
            procedure.runway = approach_runway(identifier);
            procedure.display_name = approach_display_name(identifier);
            if (columns.size() <= 12) continue;
            ApproachLeg leg;
            try { leg.sequence = std::stoi(columns[0]); } catch (...) {}
            leg.identifier = uppercase(columns.size() > 4 ? columns[4] : std::string{});
            leg.path_terminator = uppercase(columns[11]);
            leg.altitude_feet = numeric_altitude(columns);
            leg.runway = leg.identifier.rfind("RW", 0) == 0;
            if (leg.runway) {
                const auto found = runway_coordinates.find(leg.identifier);
                if (found != runway_coordinates.end()) {
                    leg.latitude_degrees = found->second.first;
                    leg.longitude_degrees = found->second.second;
                }
            }
            const bool missed = columns.size() > 8 && columns[8].find('M') != std::string::npos;
            if (missed && !leg.runway) continue;
            if (uppercase(columns[1]) == "A" && columns.size() > 3 && !columns[3].empty()) {
                const std::string transition_id = uppercase(columns[3]);
                auto transition = std::find_if(procedure.transitions.begin(), procedure.transitions.end(),
                    [&](const auto& value) { return value.identifier == transition_id; });
                if (transition == procedure.transitions.end()) {
                    procedure.transitions.push_back({transition_id, {}});
                    transition = std::prev(procedure.transitions.end());
                }
                append_leg(transition->legs, std::move(leg));
            } else {
                append_leg(procedure.final_legs, std::move(leg));
            }
        }
    }
    append_unique(airport.procedures.departures, departures);
    append_unique(airport.procedures.arrivals, arrivals);
    append_unique(airport.procedures.approaches, approaches);
    airport.procedures.departure_count = departures.size();
    airport.procedures.arrival_count = arrivals.size();
    airport.procedures.approach_count = approaches.size();
    airport.procedures.approach_details.clear();
    for (auto& [identifier, procedure] : details) {
        std::sort(procedure.transitions.begin(), procedure.transitions.end(),
                  [](const auto& left, const auto& right) {
                      return left.identifier < right.identifier;
                  });
        airport.procedures.approach_details.push_back(std::move(procedure));
    }
}

} // namespace openefb
