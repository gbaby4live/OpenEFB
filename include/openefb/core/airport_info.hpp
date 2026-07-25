#pragma once

#include <cstdint>
#include <cstddef>
#include <istream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace openefb {

enum class AirportLookupState { idle, loading, ready, not_found, error };

struct AirportRunway {
    std::string identifiers;
    double length_feet{};
    double width_feet{};
    std::string surface;
};

struct AirportFrequency {
    std::string type;
    double megahertz{};
    std::string name;
};

struct AirportProcedures {
    std::vector<std::string> departures;
    std::vector<std::string> arrivals;
    std::vector<std::string> approaches;
    std::size_t departure_count{};
    std::size_t arrival_count{};
    std::size_t approach_count{};
};

struct AirportInfoSnapshot {
    AirportLookupState state{AirportLookupState::idle};
    std::string identifier;
    std::string name;
    int elevation_feet{};
    std::vector<AirportRunway> runways;
    std::vector<AirportFrequency> frequencies;
    AirportProcedures procedures;
    std::string message;
    std::uint64_t revision{};
};

class AirportInfoModel final {
public:
    void begin_search(std::string identifier);
    void update(AirportInfoSnapshot snapshot);
    [[nodiscard]] AirportInfoSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    AirportInfoSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

[[nodiscard]] std::optional<AirportInfoSnapshot> parse_airport_apt(
    std::istream& input, std::string identifier);
void parse_airport_procedures(std::istream& input, AirportInfoSnapshot& airport);

} // namespace openefb
