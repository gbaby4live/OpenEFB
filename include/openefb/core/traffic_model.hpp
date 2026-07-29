#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openefb {

struct TrafficTarget {
    std::uint32_t mode_s_id{};
    std::string callsign;
    std::string aircraft_type;
    double latitude_degrees{};
    double longitude_degrees{};
    double altitude_feet{};
    double track_degrees{};
    double ground_speed_knots{};
    double vertical_speed_fpm{};
    bool on_ground{false};
    std::string registration;
    std::string aircraft_name;
    std::string departure_airport;
    std::string destination_airport;
    bool route_lookup_complete{false};
    std::string route_lookup_status;
};

enum class TrafficSource {
    unavailable,
    simulator,
    online,
    blended,
};

struct TrafficSnapshot {
    bool available{false};
    std::vector<TrafficTarget> targets;
    TrafficSource source{TrafficSource::unavailable};
    std::size_t simulator_target_count{};
    std::size_t online_target_count{};
    std::string status;
    int online_range_nm{100};
    bool online_degraded{false};
    bool injection_requested{false};
    bool injection_active{false};
    std::string injection_status{"Disabled"};
    bool visual_traffic_requested{true};
    bool visual_traffic_active{false};
    std::string visual_traffic_status{"Waiting for traffic injection"};
    std::string route_request_callsign;
    std::uint64_t route_request_revision{};
    std::uint64_t revision{};
};

class TrafficModel final {
public:
    void update(std::vector<TrafficTarget> targets,
                TrafficSource source = TrafficSource::simulator,
                std::size_t simulator_target_count = 0,
                std::size_t online_target_count = 0,
                std::string status = {},
                bool online_degraded = false);
    void mark_unavailable() noexcept;
    void set_injection_requested(bool requested);
    void set_injection_state(bool active, std::string status);
    void set_visual_traffic_requested(bool requested);
    void set_visual_traffic_state(bool active, std::string status);
    void request_route_lookup(std::string callsign);
    void set_online_range_nm(int range_nm) noexcept;
    [[nodiscard]] const TrafficSnapshot& snapshot() const noexcept;

private:
    TrafficSnapshot snapshot_;
    std::uint64_t next_revision_{1};
};

} // namespace openefb
