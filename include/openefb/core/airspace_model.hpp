#pragma once

#include <cstdint>
#include <istream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openefb {

struct GeoPoint {
    double latitude_degrees{};
    double longitude_degrees{};
};

struct AirspaceZone {
    std::string class_code;
    std::string name;
    std::string floor;
    std::string ceiling;
    std::vector<GeoPoint> boundary;
};

enum class AirspaceLoadState { loading, ready, unavailable, error };

struct AirspaceSnapshot {
    AirspaceLoadState state{AirspaceLoadState::unavailable};
    std::shared_ptr<const std::vector<AirspaceZone>> zones;
    std::string message;
    std::uint64_t revision{};
};

class AirspaceModel final {
public:
    AirspaceModel();
    void begin_load();
    void update(AirspaceLoadState state,
                std::shared_ptr<const std::vector<AirspaceZone>> zones,
                std::string message = {});
    [[nodiscard]] AirspaceSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    AirspaceSnapshot snapshot_;
};

[[nodiscard]] std::vector<AirspaceZone> parse_openair(std::istream& input);

} // namespace openefb
