#include "xplane_map_pois.hpp"

#if IBM
#include <Windows.h>
#include <WinHttp.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace openefb::xplane {
namespace {

struct PoiRequest {
    double latitude{};
    double longitude{};
    double range_nm{};
};

double distance_nm(double latitude_1, double longitude_1,
                   double latitude_2, double longitude_2) {
    const double north = (latitude_2 - latitude_1) * 60.0;
    const double east = (longitude_2 - longitude_1) * 60.0 *
        std::cos(latitude_1 * 3.14159265358979323846 / 180.0);
    return std::hypot(north, east);
}

#if IBM
std::string percent_encode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char character : value) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_' || character == '.' || character == '~') {
            result += static_cast<char>(character);
        } else {
            result += '%';
            result += hex[character >> 4];
            result += hex[character & 15];
        }
    }
    return result;
}

std::string overpass_query(const PoiRequest& request) {
    const int radius = static_cast<int>(std::clamp(request.range_nm * 1852.0 * 1.25,
                                                   500.0, 15000.0));
    char around[96]{};
    std::snprintf(around, sizeof(around), "(around:%d,%.7f,%.7f)", radius,
                  request.latitude, request.longitude);
    const std::string area(around);
    return "[out:csv(::id,::lat,::lon,name,amenity,tourism,leisure,golf,historic;false;\"\\t\")][timeout:12];("
           "nwr" + area + "[amenity~\"^(restaurant|cafe|fast_food|bar|pub)$\"];"
           "nwr" + area + "[leisure=\"golf_course\"];"
           "nwr" + area + "[golf];"
           "nwr" + area + "[tourism~\"^(attraction|museum|theme_park|zoo|viewpoint|gallery)$\"];"
           "nwr" + area + "[historic];);out center 240;";
}

std::string download_pois(const PoiRequest& request) {
    const std::string body = "data=" + percent_encode(overpass_query(request));
    HINTERNET session = WinHttpOpen(
        L"OpenEFB/1.0.0-rc12 (+https://github.com/Gbaby4live/OpenEFB)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};
    WinHttpSetTimeouts(session, 4000, 4000, 15000, 15000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure_protocols, sizeof(secure_protocols));
    HINTERNET connection = WinHttpConnect(session, L"overpass-api.de",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET http = connection ? WinHttpOpenRequest(
        connection, L"POST", L"/api/interpreter", nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::string response;
    const wchar_t* headers = L"Content-Type: application/x-www-form-urlencoded\r\n";
    if (http && WinHttpSendRequest(http, headers, static_cast<DWORD>(-1L),
                                   const_cast<char*>(body.data()),
                                   static_cast<DWORD>(body.size()),
                                   static_cast<DWORD>(body.size()), 0) &&
        WinHttpReceiveResponse(http, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(http, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(http, &available) || available == 0) break;
                if (response.size() + available > 2 * 1024 * 1024) {
                    response.clear();
                    break;
                }
                const auto offset = response.size();
                response.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(http, response.data() + offset, available, &read)) {
                    response.clear();
                    break;
                }
                response.resize(offset + read);
            }
        }
    }
    if (http) WinHttpCloseHandle(http);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return response;
}
#else
std::string download_pois(const PoiRequest&) { return {}; }
#endif

} // namespace

class XPlaneMapPois::Implementation final {
public:
    Implementation() : worker_([this] { work(); }) {}
    ~Implementation() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void update(double latitude, double longitude, double range_nm) {
        if (!std::isfinite(latitude) || !std::isfinite(longitude) || range_nm > 40.0) return;
        std::lock_guard lock(mutex_);
        if (last_request_ &&
            distance_nm(last_request_->latitude, last_request_->longitude,
                        latitude, longitude) < std::max(0.15, range_nm * 0.35) &&
            std::abs(last_request_->range_nm - range_nm) < range_nm * 0.45 &&
            (status_ != "Places offline" ||
             std::chrono::steady_clock::now() < retry_after_)) return;
        pending_ = PoiRequest{latitude, longitude, range_nm};
        status_ = "Loading places";
        condition_.notify_one();
    }

    std::vector<MapPoi> snapshot() const {
        std::lock_guard lock(mutex_);
        return pois_;
    }

    std::string status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

private:
    void work() {
        for (;;) {
            PoiRequest request;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                request = *pending_;
                pending_.reset();
                last_request_ = request;
            }
            const auto response = download_pois(request);
            auto parsed = parse_overpass_pois(response);
            {
                std::lock_guard lock(mutex_);
                if (!response.empty()) pois_ = std::move(parsed);
                status_ = response.empty() ? "Places offline" :
                    std::to_string(pois_.size()) + " places";
                if (response.empty()) {
                    retry_after_ = std::chrono::steady_clock::now() + std::chrono::minutes(1);
                }
            }
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<PoiRequest> pending_;
    std::optional<PoiRequest> last_request_;
    std::vector<MapPoi> pois_;
    std::string status_{"Zoom in for places"};
    std::chrono::steady_clock::time_point retry_after_{};
    bool stopping_{false};
    std::thread worker_;
};

XPlaneMapPois::XPlaneMapPois() : implementation_(std::make_unique<Implementation>()) {}
XPlaneMapPois::~XPlaneMapPois() = default;
void XPlaneMapPois::update(double latitude, double longitude, double range_nm) {
    implementation_->update(latitude, longitude, range_nm);
}
std::vector<MapPoi> XPlaneMapPois::snapshot() const { return implementation_->snapshot(); }
std::string XPlaneMapPois::status() const { return implementation_->status(); }

} // namespace openefb::xplane
