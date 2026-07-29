#include "xplane_online_traffic.hpp"

#if IBM
#include <Windows.h>
#include <WinHttp.h>
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace openefb::xplane {
namespace {

constexpr auto request_interval = std::chrono::seconds(15);
constexpr auto stale_interval = std::chrono::seconds(90);
constexpr auto maximum_retry_interval = std::chrono::seconds(120);
constexpr int minimum_request_radius_nm = 25;
constexpr int default_request_radius_nm = 100;
constexpr int maximum_request_radius_nm = 200;
constexpr double pi = 3.14159265358979323846;
constexpr double maximum_prediction_seconds = 30.0;

struct DownloadResult {
    bool success{false};
    std::string body;
    std::string status;
    unsigned http_status{};
};

struct RouteInfo {
    std::string departure;
    std::string destination;
    bool complete{false};
    std::string status;
};

bool valid_coordinate(double latitude, double longitude) {
    return std::isfinite(latitude) && std::isfinite(longitude) &&
           latitude >= -90.0 && latitude <= 90.0 &&
           longitude >= -180.0 && longitude <= 180.0;
}

std::string trimmed(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
    }).base();
    if (first >= last) return {};
    return {first, last};
}

std::string normalized_callsign(std::string value) {
    value = trimmed(std::move(value));
    std::string result;
    for (const unsigned char character : value) {
        if (std::isalnum(character)) {
            result += static_cast<char>(std::toupper(character));
        }
    }
    if (result.size() > 12) result.resize(12);
    return result;
}

std::optional<std::string> json_scalar(std::string_view object, std::string_view key) {
    const std::string pattern = "\"" + std::string(key) + "\"";
    const auto key_position = object.find(pattern);
    if (key_position == std::string_view::npos) return std::nullopt;
    auto cursor = object.find(':', key_position + pattern.size());
    if (cursor == std::string_view::npos) return std::nullopt;
    ++cursor;
    while (cursor < object.size() && std::isspace(static_cast<unsigned char>(object[cursor]))) {
        ++cursor;
    }
    if (cursor >= object.size() || object.substr(cursor, 4) == "null") return std::nullopt;
    if (object[cursor] == '"') {
        std::string result;
        bool escaped = false;
        for (++cursor; cursor < object.size(); ++cursor) {
            const char character = object[cursor];
            if (escaped) {
                result += character;
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                return result;
            } else {
                result += character;
            }
        }
        return std::nullopt;
    }
    const auto end = object.find_first_of(",}", cursor);
    return trimmed(std::string(object.substr(cursor, end - cursor)));
}

std::optional<double> json_number(std::string_view object, std::string_view key) {
    const auto scalar = json_scalar(object, key);
    if (!scalar || scalar->empty()) return std::nullopt;
    double value{};
    const auto result = std::from_chars(scalar->data(), scalar->data() + scalar->size(), value);
    if (result.ec != std::errc{} || !std::isfinite(value)) return std::nullopt;
    return value;
}

std::uint32_t hexadecimal_id(std::string_view value) {
    if (!value.empty() && value.front() == '~') value.remove_prefix(1);
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, 16);
    return parsed.ec == std::errc{} ? result : 0;
}

double distance_nm(double latitude_1, double longitude_1,
                   double latitude_2, double longitude_2) {
    double longitude_delta = longitude_2 - longitude_1;
    while (longitude_delta > 180.0) longitude_delta -= 360.0;
    while (longitude_delta < -180.0) longitude_delta += 360.0;
    const double mean_latitude = (latitude_1 + latitude_2) * 0.5 * pi / 180.0;
    const double east = longitude_delta * std::cos(mean_latitude) * 60.0;
    const double north = (latitude_2 - latitude_1) * 60.0;
    return std::hypot(east, north);
}

void predict_target_position(TrafficTarget& target, double age_seconds) {
    if (!valid_coordinate(target.latitude_degrees, target.longitude_degrees) ||
        !std::isfinite(target.ground_speed_knots) ||
        !std::isfinite(target.track_degrees)) return;

    const double seconds = std::clamp(age_seconds, 0.0, maximum_prediction_seconds);
    const double distance = std::max(0.0, target.ground_speed_knots) * seconds / 3600.0;
    const double track_radians = target.track_degrees * pi / 180.0;
    const double north_nm = std::cos(track_radians) * distance;
    const double east_nm = std::sin(track_radians) * distance;
    const double original_latitude = target.latitude_degrees;
    target.latitude_degrees = std::clamp(original_latitude + north_nm / 60.0, -90.0, 90.0);

    const double longitude_scale = std::max(
        0.01,
        std::abs(std::cos((original_latitude + target.latitude_degrees) * 0.5 * pi / 180.0)));
    target.longitude_degrees += east_nm / (60.0 * longitude_scale);
    while (target.longitude_degrees > 180.0) target.longitude_degrees -= 360.0;
    while (target.longitude_degrees < -180.0) target.longitude_degrees += 360.0;

    if (!target.on_ground && std::isfinite(target.vertical_speed_fpm)) {
        target.altitude_feet = std::max(
            0.0,
            target.altitude_feet + target.vertical_speed_fpm * seconds / 60.0);
    }
}

std::vector<std::string_view> aircraft_objects(std::string_view json) {
    std::vector<std::string_view> result;
    const auto ac = json.find("\"ac\"");
    const auto array = ac == std::string_view::npos ? std::string_view::npos : json.find('[', ac);
    if (array == std::string_view::npos) return result;
    bool quoted = false;
    bool escaped = false;
    int depth = 0;
    std::size_t start = std::string_view::npos;
    for (std::size_t index = array + 1; index < json.size(); ++index) {
        const char character = json[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') quoted = false;
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '{') {
            if (depth++ == 0) start = index;
        } else if (character == '}' && depth > 0) {
            if (--depth == 0 && start != std::string_view::npos) {
                result.push_back(json.substr(start, index - start + 1));
                start = std::string_view::npos;
            }
        } else if (character == ']' && depth == 0) {
            break;
        }
    }
    return result;
}

std::vector<TrafficTarget> parse_aircraft(std::string_view json,
                                          double center_latitude,
                                          double center_longitude,
                                          int radius_nm) {
    struct Candidate {
        TrafficTarget target;
        double distance{};
    };
    std::vector<Candidate> candidates;
    for (const auto object : aircraft_objects(json)) {
        const auto latitude = json_number(object, "lat");
        const auto longitude = json_number(object, "lon");
        const auto seen_position = json_number(object, "seen_pos");
        if (!latitude || !longitude || !valid_coordinate(*latitude, *longitude) ||
            (seen_position && *seen_position > 45.0)) continue;
        TrafficTarget target;
        if (const auto value = json_scalar(object, "hex")) {
            target.mode_s_id = hexadecimal_id(*value);
        }
        if (const auto value = json_scalar(object, "flight")) target.callsign = trimmed(*value);
        if (const auto value = json_scalar(object, "t")) target.aircraft_type = trimmed(*value);
        if (const auto value = json_scalar(object, "r")) target.registration = trimmed(*value);
        if (const auto value = json_scalar(object, "desc")) target.aircraft_name = trimmed(*value);
        if (target.callsign.empty()) {
            target.callsign = target.registration;
        }
        if (target.callsign.empty()) {
            target.callsign = target.aircraft_type.empty() ? "ADS-B" : target.aircraft_type;
        }
        target.latitude_degrees = *latitude;
        target.longitude_degrees = *longitude;
        const auto barometric_altitude = json_scalar(object, "alt_baro");
        target.on_ground = barometric_altitude && *barometric_altitude == "ground";
        if (!target.on_ground) {
            if (const auto altitude = json_number(object, "alt_baro")) {
                target.altitude_feet = *altitude;
            } else if (const auto geometric_altitude = json_number(object, "alt_geom")) {
                target.altitude_feet = *geometric_altitude;
            }
        } else if (const auto ground_altitude = json_number(object, "alt_geom")) {
            target.altitude_feet = *ground_altitude;
        }
        if (const auto value = json_number(object, "track")) target.track_degrees = *value;
        if (const auto value = json_number(object, "gs")) target.ground_speed_knots = *value;
        if (const auto value = json_number(object, "baro_rate")) {
            target.vertical_speed_fpm = *value;
        } else if (const auto geometric_rate = json_number(object, "geom_rate")) {
            target.vertical_speed_fpm = *geometric_rate;
        }
        const double target_distance = distance_nm(
            center_latitude, center_longitude, *latitude, *longitude);
        if (target_distance > static_cast<double>(radius_nm) + 1.0) continue;
        candidates.push_back({std::move(target), target_distance});
    }
    std::ranges::sort(candidates, {}, &Candidate::distance);
    std::vector<TrafficTarget> result;
    result.reserve(std::min<std::size_t>(63, candidates.size()));
    for (auto& candidate : candidates) {
        if (result.size() >= 63) break;
        result.push_back(std::move(candidate.target));
    }
    return result;
}

RouteInfo parse_route(std::string_view json) {
    RouteInfo route;
    route.complete = true;
    const auto airport_codes = json_scalar(json, "airport_codes");
    if (!airport_codes) return route;
    const auto separator = airport_codes->find('-');
    if (separator == std::string::npos) return route;
    route.departure = trimmed(airport_codes->substr(0, separator));
    route.destination = trimmed(airport_codes->substr(separator + 1));
    return route;
}

#if IBM
DownloadResult download_aircraft(double latitude, double longitude, int radius_nm) {
    wchar_t path[160]{};
    swprintf_s(path, L"/v2/lat/%.5f/lon/%.5f/dist/%d",
        latitude, longitude, radius_nm);
    HINTERNET session = WinHttpOpen(
        L"OpenEFB/1.0.0-rc28 (+https://github.com/Gbaby4live/OpenEFB)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {false, {}, "ADSB.LOL connection could not start"};
    WinHttpSetTimeouts(session, 3000, 3000, 6000, 6000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure_protocols, sizeof(secure_protocols));
    HINTERNET connection = WinHttpConnect(
        session, L"api.adsb.lol", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    DownloadResult result;
    result.status = connection ? "ADSB.LOL request failed" : "ADSB.LOL host unavailable";
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        const bool has_status = WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) != FALSE;
        result.http_status = status;
        if (has_status && status == 200) {
            result.success = true;
            result.status = "ADSB.LOL ONLINE";
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0 ||
                    result.body.size() + available > 4 * 1024 * 1024) break;
                const auto offset = result.body.size();
                result.body.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, result.body.data() + offset, available, &read)) {
                    result.success = false;
                    result.body.clear();
                    result.status = "ADSB.LOL response failed";
                    break;
                }
                result.body.resize(offset + read);
            }
        } else if (has_status && status != 0) {
            result.status = "ADSB.LOL HTTP " + std::to_string(status);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

DownloadResult download_route(const std::string& callsign) {
    if (callsign.size() < 2) return {false, {}, "Route callsign unavailable"};
    const std::string route_path = "/routes/" + callsign.substr(0, 2) + "/" + callsign + ".json";
    const std::wstring path(route_path.begin(), route_path.end());
    HINTERNET session = WinHttpOpen(
        L"OpenEFB/1.0.0-rc28 (+https://github.com/Gbaby4live/OpenEFB)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {false, {}, "Route connection could not start"};
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure_protocols, sizeof(secure_protocols));
    HINTERNET connection = WinHttpConnect(
        session, L"vrs-standing-data.adsb.lol", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    DownloadResult result;
    result.status = connection ? "Route request failed" : "Route host unavailable";
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            result.success = true;
            result.status = "Route data ready";
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0 ||
                    result.body.size() + available > 16 * 1024) break;
                const auto offset = result.body.size();
                result.body.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, result.body.data() + offset, available, &read)) {
                    result.success = false;
                    result.body.clear();
                    result.status = "Route response failed";
                    break;
                }
                result.body.resize(offset + read);
            }
        } else if (status == 404) {
            result.success = true;
            result.status = "Route not published";
        } else if (status != 0) {
            result.status = "Route HTTP " + std::to_string(status);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}
#else
DownloadResult download_aircraft(double, double, int) {
    return {false, {}, "Online traffic is unavailable on this platform"};
}
DownloadResult download_route(const std::string&) {
    return {false, {}, "Route lookup is unavailable on this platform"};
}
#endif

} // namespace

class XPlaneOnlineTraffic::Implementation final {
public:
    void start() {
        std::lock_guard lock(mutex_);
        if (worker_.joinable()) return;
        stopping_ = false;
        status_ = "ADSB.LOL starting";
        worker_ = std::thread([this] { work(); });
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_);
        targets_.clear();
        route_requests_.clear();
        route_cache_.clear();
        available_ = false;
        request_pending_ = false;
        consecutive_failures_ = 0;
        next_request_allowed_ = {};
    }

    void request(double latitude, double longitude, int radius_nm) {
        if (!valid_coordinate(latitude, longitude)) return;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex_);
        if (!worker_.joinable() || request_pending_ || now < next_request_allowed_) return;
        requested_latitude_ = latitude;
        requested_longitude_ = longitude;
        requested_radius_nm_ = std::clamp(
            radius_nm,
            minimum_request_radius_nm,
            maximum_request_radius_nm);
        next_request_allowed_ = now + request_interval;
        request_pending_ = true;
        condition_.notify_one();
    }

    void request_route(std::string callsign) {
        callsign = normalized_callsign(std::move(callsign));
        if (callsign.size() < 2) return;
        std::lock_guard lock(mutex_);
        if (!worker_.joinable() || route_cache_.contains(callsign) ||
            route_requests_.size() >= 8 ||
            std::ranges::find(route_requests_, callsign) != route_requests_.end()) return;
        route_requests_.push_back(std::move(callsign));
        condition_.notify_one();
    }

    OnlineTrafficSnapshot snapshot() const {
        std::lock_guard lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        const auto observation_age = now - last_success_;
        const bool fresh = available_ && observation_age <= stale_interval;
        auto targets = fresh ? targets_ : std::vector<TrafficTarget>{};
        for (auto& target : targets) {
            predict_target_position(
                target,
                std::chrono::duration<double>(observation_age).count());
            const auto route = route_cache_.find(normalized_callsign(target.callsign));
            if (route == route_cache_.end()) continue;
            target.departure_airport = route->second.departure;
            target.destination_airport = route->second.destination;
            target.route_lookup_complete = route->second.complete;
            target.route_lookup_status = route->second.status;
        }
        return {fresh, std::move(targets), status_, fresh && consecutive_failures_ > 0};
    }

private:
    void work() {
        for (;;) {
            double latitude{};
            double longitude{};
            int radius_nm{ default_request_radius_nm };
            std::string route_callsign;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] {
                    return stopping_ || request_pending_ || !route_requests_.empty();
                });
                if (stopping_) return;
                if (!route_requests_.empty()) {
                    route_callsign = std::move(route_requests_.front());
                    route_requests_.pop_front();
                } else {
                    latitude = requested_latitude_;
                    longitude = requested_longitude_;
                    radius_nm = requested_radius_nm_;
                    request_pending_ = false;
                }
            }
            if (!route_callsign.empty()) {
                const auto response = download_route(route_callsign);
                auto route = response.success ? parse_route(response.body) : RouteInfo{};
                route.complete = true;
                route.status = response.status;
                std::lock_guard lock(mutex_);
                route_cache_.insert_or_assign(std::move(route_callsign), std::move(route));
                continue;
            }
            auto response = download_aircraft(latitude, longitude, radius_nm);
            auto targets = response.success
                ? parse_aircraft(response.body, latitude, longitude, radius_nm)
                : std::vector<TrafficTarget>{};
            std::lock_guard lock(mutex_);
            if (response.success) {
                status_ = "ADSB.LOL ONLINE / " + std::to_string(radius_nm) + " NM";
                targets_ = std::move(targets);
                available_ = true;
                last_success_ = std::chrono::steady_clock::now();
                consecutive_failures_ = 0;
            } else {
                ++consecutive_failures_;
                const int exponent = std::min(3, consecutive_failures_ - 1);
                auto delay = request_interval * (1 << exponent);
                if (response.http_status == 429) delay = maximum_retry_interval;
                delay = std::min(delay, maximum_retry_interval);
                next_request_allowed_ = std::chrono::steady_clock::now() + delay;
                status_ = std::move(response.status) + " / retry " +
                          std::to_string(delay.count()) + "s";
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::vector<TrafficTarget> targets_;
    std::deque<std::string> route_requests_;
    std::map<std::string, RouteInfo> route_cache_;
    std::string status_{"ADSB.LOL starting"};
    std::chrono::steady_clock::time_point next_request_allowed_{};
    std::chrono::steady_clock::time_point last_success_{};
    double requested_latitude_{};
    double requested_longitude_{};
    int requested_radius_nm_{ default_request_radius_nm };
    int consecutive_failures_{0};
    bool stopping_{false};
    bool request_pending_{false};
    bool available_{false};
};

XPlaneOnlineTraffic::XPlaneOnlineTraffic()
    : implementation_(std::make_unique<Implementation>()) {}
XPlaneOnlineTraffic::~XPlaneOnlineTraffic() { stop(); }
void XPlaneOnlineTraffic::start() { implementation_->start(); }
void XPlaneOnlineTraffic::stop() { implementation_->stop(); }
void XPlaneOnlineTraffic::request(double latitude, double longitude, int radius_nm) {
    implementation_->request(latitude, longitude, radius_nm);
}
void XPlaneOnlineTraffic::request_route(std::string callsign) {
    implementation_->request_route(std::move(callsign));
}
OnlineTrafficSnapshot XPlaneOnlineTraffic::snapshot() const {
    return implementation_->snapshot();
}

} // namespace openefb::xplane
