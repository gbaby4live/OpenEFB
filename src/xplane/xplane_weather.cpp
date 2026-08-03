#include "xplane_weather.hpp"

#include <XPLMWeather.h>

#if IBM
#include <Windows.h>
#include <WinHttp.h>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 5.0F;

std::pair<std::string, std::string> route_airports(const FlightPlanSnapshot& flight_plan) {
    const auto departure = std::find_if(flight_plan.legs.begin(), flight_plan.legs.end(),
                                        [](const FlightPlanLeg& leg) {
                                            return leg.kind == WaypointKind::airport && !leg.identifier.empty();
                                        });
    const auto destination = std::find_if(flight_plan.legs.rbegin(), flight_plan.legs.rend(),
                                          [](const FlightPlanLeg& leg) {
                                              return leg.kind == WaypointKind::airport && !leg.identifier.empty();
                                          });
    return {departure == flight_plan.legs.end() ? std::string{} : departure->identifier,
            destination == flight_plan.legs.rend() ? std::string{} : destination->identifier};
}

AirportWeather simulator_metar(std::string airport) {
    AirportWeather weather;
    weather.airport_id = std::move(airport);
    if (weather.airport_id.empty()) return weather;
    XPLMFixedString150_t metar{};
    XPLMGetMETARForAirport(weather.airport_id.c_str(), &metar);
    weather.metar = metar.buffer;
    if (!weather.metar.empty()) weather.source = WeatherSource::simulator;
    return weather;
}

std::string read_cache(const std::filesystem::path& root, const std::string& airport,
                       std::string_view extension) {
    try {
        std::ifstream input(root / (airport + std::string(extension)),
                            std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto size = input.tellg();
        if (size <= 0 || size > 2048) return {};
        std::string value(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        input.read(value.data(), size);
        return input ? value : std::string{};
    } catch (...) {
        return {};
    }
}

void save_cache(const std::filesystem::path& root, const std::string& airport,
                const std::string& value, std::string_view extension) {
    try {
        std::filesystem::create_directories(root);
        std::ofstream output(root / (airport + std::string(extension)),
                             std::ios::binary | std::ios::trunc);
        output << value;
    } catch (...) {
    }
}

#if IBM
struct WeatherDownload { std::string body; std::string status; };

WeatherDownload download_product(const std::vector<std::string>& airports, bool forecast) {
    if (airports.empty()) return {{}, "Waiting for route airports"};
    std::wstring path = forecast
        ? L"/api/data/taf?format=json&ids="
        : L"/api/data/metar?format=json&taf=false&hours=2&ids=";
    for (std::size_t index = 0; index < airports.size(); ++index) {
        if (index) path += L",";
        path.append(airports[index].begin(), airports[index].end());
    }
    HINTERNET session = WinHttpOpen(L"OpenEFB/1.1.0-mobile-2 (+https://github.com/Gbaby4live/OpenEFB)",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {{}, "Online weather connection could not start"};
    WinHttpSetTimeouts(session, 3000, 3000, 5000, 5000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure_protocols, sizeof(secure_protocols));
    HINTERNET connection = WinHttpConnect(session, L"aviationweather.gov",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    std::string result;
    std::string status_message = connection ? "Online weather request failed"
                                            : "Online weather host could not be reached";
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            status_message = "Online weather connected";
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0 ||
                    result.size() + available > 16 * 1024) break;
                const auto offset = result.size();
                result.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, result.data() + offset, available, &read)) {
                    result.clear();
                    break;
                }
                result.resize(offset + read);
            }
        } else if (status != 0) {
            status_message = "Online weather HTTP " + std::to_string(status);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (result.empty() && status_message == "Online weather connected")
        status_message = "Online weather returned no report";
    return {std::move(result), std::move(status_message)};
}
#else
struct WeatherDownload { std::string body; std::string status; };
WeatherDownload download_product(const std::vector<std::string>&, bool) {
    return {{}, "Online weather adapter is unavailable on this platform"};
}
#endif

std::map<std::string, std::string> parse_reports(
    std::string_view value, const std::vector<std::string>& requested_airports,
    std::string_view field) {
    std::map<std::string, std::string> result;
    // Prefer the API's structured raw-observation field. This remains robust
    // if JSON field order or whitespace changes.
    std::size_t json_position = 0;
    const std::string field_token = "\"" + std::string(field) + "\"";
    while ((json_position = value.find(field_token, json_position)) != std::string_view::npos) {
        const auto colon = value.find(':', json_position + field_token.size());
        const auto quote = colon == std::string_view::npos
            ? std::string_view::npos : value.find('"', colon + 1);
        if (quote == std::string_view::npos) break;
        std::string raw;
        std::size_t cursor = quote + 1;
        for (; cursor < value.size(); ++cursor) {
            const char character = value[cursor];
            if (character == '"') break;
            if (character == '\\' && cursor + 1 < value.size()) {
                const char escaped = value[++cursor];
                raw += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped;
            } else {
                raw += character;
            }
        }
        for (const auto& airport : requested_airports) {
            if (raw.find(airport) != std::string::npos) result[airport] = raw;
        }
        json_position = cursor;
    }
    if (!result.empty()) return result;

    // Retain compatibility with raw-text responses and optional METAR prefixes.
    std::istringstream input{std::string(value)};
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        for (const auto& airport : requested_airports) {
            const auto position = line.find(airport);
            if (position != std::string::npos) {
                // The raw endpoint has used both "KSEA ..." and
                // "METAR KSEA ..." forms. Index by the requested station
                // instead of assuming the identifier is the first token.
                result[airport] = line.substr(position);
            }
        }
    }
    return result;
}

} // namespace

class XPlaneWeather::Implementation final {
public:
    explicit Implementation(std::filesystem::path cache_directory)
        : cache_directory_(std::move(cache_directory)) {}

    void start() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = false;
            requested_ = {};
            request_pending_ = false;
            online_metars_.clear();
            online_tafs_.clear();
            cached_metars_.clear();
            cached_tafs_.clear();
            status_ = "Online weather is starting";
        }
        worker_ = std::thread([this] { work(); });
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_one();
        if (worker_.joinable()) worker_.join();
    }

    void request(std::string departure, std::string destination) {
        std::lock_guard lock(mutex_);
        const std::pair requested{std::move(departure), std::move(destination)};
        const auto now = std::chrono::steady_clock::now();
        const auto interval = online_metars_.empty() ? std::chrono::minutes(1)
                                                      : std::chrono::minutes(5);
        if (requested == requested_ && now - last_request_ < interval) return;
        requested_ = requested;
        last_request_ = now;
        request_pending_ = true;
        condition_.notify_one();
    }

    AirportWeather best(std::string airport, AirportWeather simulator) const {
        if (airport.empty()) return {};
        std::lock_guard lock(mutex_);
        simulator.airport_id = airport;
        if (const auto online_metar = online_metars_.find(airport);
            online_metar != online_metars_.end()) {
            simulator.metar = online_metar->second;
            simulator.source = WeatherSource::online;
        } else if (simulator.metar.empty()) {
            if (const auto cached_metar = cached_metars_.find(airport);
                cached_metar != cached_metars_.end()) {
                simulator.metar = cached_metar->second;
                simulator.source = WeatherSource::cache;
            }
        }
        if (const auto online_taf = online_tafs_.find(airport);
            online_taf != online_tafs_.end()) {
            simulator.taf = online_taf->second;
            simulator.forecast_source = WeatherSource::online;
        } else if (const auto cached_taf = cached_tafs_.find(airport);
                   cached_taf != cached_tafs_.end()) {
            simulator.taf = cached_taf->second;
            simulator.forecast_source = WeatherSource::cache;
        }
        return simulator;
    }

    std::string status() const {
        std::lock_guard lock(mutex_);
        return status_;
    }

private:
    void work() {
        for (;;) {
            std::pair<std::string, std::string> requested;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_ || request_pending_; });
                if (stopping_) return;
                requested = requested_;
                request_pending_ = false;
            }
            std::vector<std::string> airports;
            if (!requested.first.empty()) airports.push_back(requested.first);
            if (!requested.second.empty() && requested.second != requested.first) airports.push_back(requested.second);
            std::map<std::string, std::string> cached_metars;
            std::map<std::string, std::string> cached_tafs;
            for (const auto& airport : airports) {
                if (auto value = read_cache(cache_directory_, airport, ".metar"); !value.empty())
                    cached_metars[airport] = std::move(value);
                if (auto value = read_cache(cache_directory_, airport, ".taf"); !value.empty())
                    cached_tafs[airport] = std::move(value);
            }
            const auto metar_response = download_product(airports, false);
            const auto taf_response = download_product(airports, true);
            const auto downloaded_metars = parse_reports(metar_response.body, airports, "rawOb");
            const auto downloaded_tafs = parse_reports(taf_response.body, airports, "rawTAF");
            for (const auto& [airport, metar] : downloaded_metars)
                save_cache(cache_directory_, airport, metar, ".metar");
            for (const auto& [airport, taf] : downloaded_tafs)
                save_cache(cache_directory_, airport, taf, ".taf");
            {
                std::lock_guard lock(mutex_);
                cached_metars_ = std::move(cached_metars);
                cached_tafs_ = std::move(cached_tafs);
                online_metars_ = downloaded_metars;
                online_tafs_ = downloaded_tafs;
                status_ = metar_response.status + " / forecast " +
                    (downloaded_tafs.empty() ? taf_response.status : "connected");
                for (const auto& [airport, metar] : downloaded_metars)
                    cached_metars_[airport] = metar;
                for (const auto& [airport, taf] : downloaded_tafs)
                    cached_tafs_[airport] = taf;
            }
        }
    }

    std::filesystem::path cache_directory_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::pair<std::string, std::string> requested_;
    std::map<std::string, std::string> online_metars_;
    std::map<std::string, std::string> online_tafs_;
    std::map<std::string, std::string> cached_metars_;
    std::map<std::string, std::string> cached_tafs_;
    std::string status_{"Online weather is starting"};
    std::chrono::steady_clock::time_point last_request_{};
    bool request_pending_{false};
    bool stopping_{false};
};

XPlaneWeather::XPlaneWeather(WeatherModel& model, const FlightPlanModel& flight_plan_model,
                             std::filesystem::path cache_directory)
    : model_(model), flight_plan_model_(flight_plan_model),
      implementation_(std::make_unique<Implementation>(std::move(cache_directory))) {}

XPlaneWeather::~XPlaneWeather() { stop(); }

bool XPlaneWeather::start() {
    if (flight_loop_id_) return true;
    implementation_->start();
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_BeforeFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) {
        implementation_->stop();
        model_.mark_unavailable();
        return false;
    }
    XPLMScheduleFlightLoop(flight_loop_id_, -1.0F, 1);
    return true;
}

void XPlaneWeather::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
        implementation_->stop();
    }
    model_.mark_unavailable();
}

void XPlaneWeather::sample() {
    WeatherSnapshot snapshot;
    const auto& flight_plan = flight_plan_model_.snapshot();
    snapshot.route_revision = flight_plan.revision;
    if (!flight_plan.available || flight_plan.legs.empty()) {
        model_.update(std::move(snapshot));
        return;
    }
    auto [departure, destination] = route_airports(flight_plan);
    implementation_->request(departure, destination);
    snapshot.departure = implementation_->best(departure, simulator_metar(departure));
    snapshot.destination = implementation_->best(destination, simulator_metar(destination));
    snapshot.online_status = implementation_->status();
    model_.update(std::move(snapshot));
}

float XPlaneWeather::flight_loop(float, float, int, void* refcon) {
    auto* weather = static_cast<XPlaneWeather*>(refcon);
    if (!weather) return 0.0F;
    weather->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
