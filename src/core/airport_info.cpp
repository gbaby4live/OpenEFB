#include "openefb/core/airport_info.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>
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
    constexpr std::size_t maximum_visible = 12;
    for (const auto& value : values) {
        if (target.size() >= maximum_visible) break;
        target.push_back(value);
    }
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
    std::string line;
    while (std::getline(input, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string type = line.substr(0, colon);
        std::istringstream payload(line.substr(colon + 1));
        std::string value;
        std::vector<std::string> columns;
        while (std::getline(payload, value, ',')) columns.push_back(trim(value));
        if (columns.size() < 3 || columns[2].empty()) continue;
        if (type == "SID") departures.insert(columns[2]);
        else if (type == "STAR") arrivals.insert(columns[2]);
        else if (type == "APPCH") approaches.insert(columns[2]);
    }
    append_unique(airport.procedures.departures, departures);
    append_unique(airport.procedures.arrivals, arrivals);
    append_unique(airport.procedures.approaches, approaches);
    airport.procedures.departure_count = departures.size();
    airport.procedures.arrival_count = arrivals.size();
    airport.procedures.approach_count = approaches.size();
}

} // namespace openefb
