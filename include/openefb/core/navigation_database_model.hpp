#pragma once

#include "openefb/core/flight_plan_model.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace openefb {

struct MapNavigationPoint {
    std::string identifier;
    std::string name;
    WaypointKind kind{WaypointKind::other};
    double latitude_degrees{};
    double longitude_degrees{};
};

struct NavigationDatabaseSnapshot {
    bool loading{false};
    bool available{false};
    std::shared_ptr<const std::vector<MapNavigationPoint>> points;
    std::uint64_t revision{};
};

class NavigationDatabaseModel final {
public:
    NavigationDatabaseModel();
    void begin_load();
    void update(std::vector<MapNavigationPoint> points, bool loading);
    void mark_unavailable();
    [[nodiscard]] NavigationDatabaseSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    NavigationDatabaseSnapshot snapshot_;
};

} // namespace openefb
