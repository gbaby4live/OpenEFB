#pragma once

#include <cstdint>
#include <string>

namespace openefb {

struct AirportWeather {
    std::string airport_id;
    std::string metar;
};

struct WeatherSnapshot {
    bool available{false};
    AirportWeather departure;
    AirportWeather destination;
    std::uint64_t route_revision{};
    std::uint64_t revision{};
};

class WeatherModel final {
public:
    void update(WeatherSnapshot snapshot);
    void mark_unavailable() noexcept;

    [[nodiscard]] const WeatherSnapshot& snapshot() const noexcept;

private:
    WeatherSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
