#include "xplane_mobile_server.hpp"
#include "xplane_airport_data.hpp"
#include "xplane_briefing_library.hpp"
#include "xplane_flight_plan.hpp"

#if IBM
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <string_view>
#include <vector>

namespace openefb::xplane {
namespace {

constexpr unsigned short mobile_port = 8383;

std::string json_escape(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (character >= 0x20) result += static_cast<char>(character);
            break;
        }
    }
    return result;
}

std::string number(double value, int precision = 5) {
    if (!std::isfinite(value)) return "0";
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    return buffer;
}

std::string pairing_code() {
    std::random_device source;
    std::mt19937 generator(source());
    std::uniform_int_distribution<int> distribution(0, 999999);
    char code[7]{};
    std::snprintf(code, sizeof(code), "%06d", distribution(generator));
    return code;
}

std::string session_token() {
    std::random_device source;
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (int index = 0; index < 8; ++index)
        output << std::setw(8) << static_cast<std::uint32_t>(source());
    return output.str();
}

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string url_decode(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '+') result.push_back(' ');
        else if (value[index] == '%' && index + 2 < value.size()) {
            const int high = hex_digit(value[index + 1]);
            const int low = hex_digit(value[index + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                index += 2;
            } else result.push_back(value[index]);
        } else result.push_back(value[index]);
    }
    return result;
}

std::map<std::string, std::string> form_values(std::string_view source) {
    std::map<std::string, std::string> values;
    std::size_t start{};
    while (start <= source.size()) {
        const auto end = source.find('&', start);
        const auto item = source.substr(start, end == std::string_view::npos ? source.size() - start
                                                                            : end - start);
        const auto equals = item.find('=');
        const auto key = url_decode(item.substr(0, equals));
        const auto value = equals == std::string_view::npos ? std::string{} : url_decode(item.substr(equals + 1));
        if (!key.empty()) values[key] = value;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return values;
}

std::map<std::string, std::string> query_values(std::string_view target) {
    const auto query = target.find('?');
    return query == std::string_view::npos ? std::map<std::string, std::string>{}
                                           : form_values(target.substr(query + 1));
}

std::vector<std::string> split(std::string_view value, char delimiter) {
    std::vector<std::string> result;
    std::size_t start{};
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        result.emplace_back(value.substr(start, end == std::string_view::npos ? value.size() - start
                                                                              : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return result;
}

std::optional<std::uint64_t> unsigned_number(std::string_view value) {
    try {
        std::size_t used{};
        const auto parsed = std::stoull(std::string(value), &used);
        if (used != value.size()) return std::nullopt;
        return parsed;
    } catch (...) { return std::nullopt; }
}

std::vector<FlightPlanLeg> parse_route(std::string_view value) {
    std::vector<FlightPlanLeg> route;
    for (const auto& line : split(value, '\n')) {
        if (line.empty()) continue;
        const auto columns = split(line, '\t');
        if (columns.size() != 5 || columns[0].empty() || columns[0].size() > 32) return {};
        try {
            FlightPlanLeg leg;
            leg.index = static_cast<int>(route.size());
            leg.identifier = columns[0];
            leg.kind = static_cast<WaypointKind>(std::clamp(std::stoi(columns[1]), 0, 5));
            leg.altitude_feet = std::stoi(columns[2]);
            leg.latitude_degrees = std::stod(columns[3]);
            leg.longitude_degrees = std::stod(columns[4]);
            if (!std::isfinite(leg.latitude_degrees) || !std::isfinite(leg.longitude_degrees) ||
                leg.latitude_degrees < -90.0 || leg.latitude_degrees > 90.0 ||
                leg.longitude_degrees < -180.0 || leg.longitude_degrees > 180.0) return {};
            route.push_back(std::move(leg));
        } catch (...) { return {}; }
        if (route.size() > 150) return {};
    }
    return route;
}

std::string content_type(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "application/javascript; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".webmanifest") return "application/manifest+json";
    return "application/octet-stream";
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

#if IBM
std::string local_ipv4() {
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) != 0) return "127.0.0.1";
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses{};
    if (getaddrinfo(hostname, nullptr, &hints, &addresses) != 0) return "127.0.0.1";
    std::string result{"127.0.0.1"};
    for (auto* address = addresses; address; address = address->ai_next) {
        const auto* socket_address = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
        char text[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, &socket_address->sin_addr, text, sizeof(text)) &&
            std::string_view(text) != "127.0.0.1") {
            result = text;
            break;
        }
    }
    freeaddrinfo(addresses);
    return result;
}

void send_response(WindowsTlsConnection& connection, int status, std::string_view type,
                   std::string_view body, bool no_store = false) {
    const char* label = status == 200 ? "OK" : status == 202 ? "Accepted" :
        status == 400 ? "Bad Request" : status == 401 ? "Unauthorized" :
        status == 409 ? "Conflict" : status == 500 ? "Internal Server Error" : "Not Found";
    std::ostringstream header;
    header << "HTTP/1.1 " << status << ' ' << label << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "X-Content-Type-Options: nosniff\r\n"
           << "Referrer-Policy: no-referrer\r\n"
           << (no_store ? "Cache-Control: no-store\r\n" : "Cache-Control: public, max-age=300\r\n")
           << "Connection: close\r\n\r\n";
    const auto header_text = header.str();
    if (!connection.send(header_text)) return;
    (void)connection.send(body);
}
#endif

} // namespace

XPlaneMobileServer::XPlaneMobileServer(TelemetryModel& telemetry_model,
                                       FlightPlanModel& flight_plan_model,
                                       RouteProgressModel& route_progress_model,
                                       WeatherModel& weather_model,
                                       TrafficModel& traffic_model,
                                       AirportInfoModel& airport_info_model,
                                       BriefingModel& briefing_model,
                                       XPlaneFlightPlan& xplane_flight_plan,
                                       XPlaneAirportData& xplane_airport_data,
                                       XPlaneBriefingLibrary& briefing_library,
                                       std::filesystem::path web_root)
    : telemetry_model_(telemetry_model), flight_plan_model_(flight_plan_model),
      route_progress_model_(route_progress_model), weather_model_(weather_model),
      traffic_model_(traffic_model), airport_info_model_(airport_info_model),
      briefing_model_(briefing_model), xplane_flight_plan_(xplane_flight_plan),
      xplane_airport_data_(xplane_airport_data), briefing_library_(briefing_library),
      web_root_(std::move(web_root)) {}

XPlaneMobileServer::~XPlaneMobileServer() { stop(); }

bool XPlaneMobileServer::start() {
#if IBM
    if (worker_.joinable()) return true;
    if (!tls_context_.initialize({})) {
        std::lock_guard lock(mutex_);
        status_.message = "Mobile HTTPS identity could not be initialized";
        if (!tls_context_.error().empty())
            status_.message += ": " + std::string(tls_context_.error());
        return false;
    }
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        std::lock_guard lock(mutex_);
        status_.message = "Mobile companion could not start networking";
        return false;
    }
    const SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) {
        WSACleanup();
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(mobile_port);
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(server, 8) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        std::lock_guard lock(mutex_);
        status_.message = "Mobile port 8383 is unavailable";
        return false;
    }
    stopping_ = false;
    listen_socket_ = static_cast<std::uintptr_t>(server);
    {
        std::lock_guard lock(mutex_);
        status_.running = true;
        status_.pairing_code = pairing_code();
        status_.identity_code = std::string(tls_context_.verification_code());
        status_.url = "https://" + local_ipv4() + ":" + std::to_string(mobile_port);
        status_.message = "Encrypted mobile flight deck ready on the same Wi-Fi";
        session_token_ = session_token();
        pending_commands_.clear();
        command_results_.clear();
    }
    sample();
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (flight_loop_id_) XPLMScheduleFlightLoop(flight_loop_id_, 0.5F, 1);
    worker_ = std::thread([this] { serve(); });
    return true;
#else
    std::lock_guard lock(mutex_);
    status_.message = "Mobile 2.0 currently requires the Windows plugin";
    return false;
#endif
}

void XPlaneMobileServer::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
#if IBM
    const bool network_started = worker_.joinable() || listen_socket_ != 0;
    stopping_ = true;
    const SOCKET server = static_cast<SOCKET>(listen_socket_);
    if (server != 0 && server != INVALID_SOCKET) {
        shutdown(server, SD_BOTH);
        closesocket(server);
    }
    listen_socket_ = 0;
    if (worker_.joinable()) worker_.join();
    if (network_started) WSACleanup();
#endif
    std::lock_guard lock(mutex_);
    status_.running = false;
    status_.message = "Mobile companion stopped";
    session_token_.clear();
    tls_context_.reset();
    pending_commands_.clear();
    command_results_.clear();
}

MobileServerStatus XPlaneMobileServer::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

void XPlaneMobileServer::process_commands() {
    std::deque<MobileCommand> commands;
    {
        std::lock_guard lock(mutex_);
        commands.swap(pending_commands_);
    }
    for (auto& command : commands) {
        CommandResult result{command.id, false, "Command was not applied"};
        const auto route_snapshot = flight_plan_model_.snapshot();
        if ((command.kind == CommandKind::apply_route || command.kind == CommandKind::apply_approach) &&
            command.expected_revision != route_snapshot.revision) {
            result.message = "The X-Plane flight plan changed. Reload it before applying your mobile draft.";
        } else if (command.kind == CommandKind::apply_route) {
            if (command.route.size() < 2) result.message = "A route needs departure and destination legs";
            else {
                const auto applied = xplane_flight_plan_.apply_route(command.route);
                result.success = applied.success;
                result.message = applied.message;
            }
        } else if (command.kind == CommandKind::search_airport) {
            result.success = xplane_airport_data_.search(command.airport);
            result.message = result.success ? "Loading installed procedures for " + command.airport
                                            : "Airport procedure service is unavailable";
        } else if (command.kind == CommandKind::refresh_library) {
            briefing_library_.refresh();
            result.success = true;
            result.message = "Briefing and chart refresh started";
        } else if (command.kind == CommandKind::apply_approach) {
            const auto airport = airport_info_model_.snapshot();
            const auto procedure = std::find_if(
                airport.procedures.approach_details.begin(), airport.procedures.approach_details.end(),
                [&](const auto& value) { return uppercase(value.identifier) == uppercase(command.approach); });
            if (airport.identifier != command.airport || procedure == airport.procedures.approach_details.end()) {
                result.message = "Reload this airport's approaches before applying";
            } else {
                const auto applied = xplane_flight_plan_.apply_approach(
                    *procedure, command.transition, command.airport,
                    command.destination_endpoint, command.excluded_fixes);
                result.success = applied.success;
                result.message = applied.message;
            }
        }
        std::lock_guard lock(mutex_);
        command_results_.push_back(std::move(result));
        while (command_results_.size() > 32) command_results_.pop_front();
    }
}

void XPlaneMobileServer::sample() {
    process_commands();
    const auto telemetry = telemetry_model_.snapshot();
    const auto route = flight_plan_model_.snapshot();
    const auto progress = route_progress_model_.snapshot();
    const auto weather = weather_model_.snapshot();
    const auto traffic = traffic_model_.snapshot();
    const auto legs = complete_flight_plan_legs(route);
    std::ostringstream json;
    json << "{\"version\":\"2.0\",\"readOnly\":false,\"telemetry\":{" 
         << "\"available\":" << (telemetry.available ? "true" : "false")
         << ",\"aircraft\":\"" << json_escape(telemetry.aircraft_name) << "\""
         << ",\"lat\":" << number(telemetry.latitude_degrees)
         << ",\"lon\":" << number(telemetry.longitude_degrees)
         << ",\"altitudeFt\":" << number(telemetry.altitude_feet, 0)
         << ",\"groundSpeedKt\":" << number(telemetry.ground_speed_knots, 1)
         << ",\"headingDeg\":" << number(telemetry.heading_degrees, 1)
         << ",\"verticalSpeedFpm\":" << number(telemetry.vertical_speed_fpm, 0)
         << "},\"route\":{\"available\":" << (route.available ? "true" : "false")
         << ",\"revision\":" << route.revision
         << ",\"activeIndex\":" << route.active_leg_index << ",\"legs\":[";
    for (std::size_t index = 0; index < legs.size(); ++index) {
        if (index) json << ',';
        const auto& leg = legs[index];
        json << "{\"id\":\"" << json_escape(leg.identifier) << "\",\"kind\":"
             << static_cast<int>(leg.kind) << ",\"lat\":"
             << number(leg.latitude_degrees) << ",\"lon\":" << number(leg.longitude_degrees)
             << ",\"altitudeFt\":" << leg.altitude_feet
             << ",\"active\":" << (leg.active ? "true" : "false") << '}';
    }
    json << "]},\"progress\":{\"available\":" << (progress.available ? "true" : "false")
         << ",\"active\":\"" << json_escape(progress.active_waypoint.identifier) << "\""
         << ",\"distanceNm\":" << number(progress.active_waypoint.distance_nm, 1)
         << ",\"destination\":\"" << json_escape(progress.destination.identifier) << "\""
         << ",\"destinationDistanceNm\":" << number(progress.destination.distance_nm, 1)
         << "},\"weather\":{\"departure\":{\"id\":\""
         << json_escape(weather.departure.airport_id) << "\",\"metar\":\""
         << json_escape(weather.departure.metar) << "\"},\"destination\":{\"id\":\""
         << json_escape(weather.destination.airport_id) << "\",\"metar\":\""
         << json_escape(weather.destination.metar) << "\"}},\"traffic\":[";
    const auto traffic_count = std::min<std::size_t>(40, traffic.targets.size());
    for (std::size_t index = 0; index < traffic_count; ++index) {
        if (index) json << ',';
        const auto& target = traffic.targets[index];
        json << "{\"callsign\":\"" << json_escape(target.callsign) << "\",\"lat\":"
             << number(target.latitude_degrees) << ",\"lon\":" << number(target.longitude_degrees)
             << ",\"altitudeFt\":" << number(target.altitude_feet, 0)
             << ",\"headingDeg\":" << number(target.track_degrees, 1) << '}';
    }
    json << "]}";

    const auto airport = airport_info_model_.snapshot();
    const char* airport_state = airport.state == AirportLookupState::loading ? "loading" :
        airport.state == AirportLookupState::ready ? "ready" :
        airport.state == AirportLookupState::not_found ? "not_found" :
        airport.state == AirportLookupState::error ? "error" : "idle";
    std::ostringstream airport_json;
    airport_json << "{\"state\":\"" << airport_state << "\",\"id\":\""
                 << json_escape(airport.identifier) << "\",\"name\":\""
                 << json_escape(airport.name) << "\",\"message\":\""
                 << json_escape(airport.message) << "\",\"approaches\":[";
    for (std::size_t approach_index = 0;
         approach_index < airport.procedures.approach_details.size(); ++approach_index) {
        if (approach_index) airport_json << ',';
        const auto& approach = airport.procedures.approach_details[approach_index];
        airport_json << "{\"id\":\"" << json_escape(approach.identifier)
                     << "\",\"name\":\"" << json_escape(approach.display_name)
                     << "\",\"runway\":\"" << json_escape(approach.runway)
                     << "\",\"transitions\":[";
        for (std::size_t transition_index = 0; transition_index < approach.transitions.size(); ++transition_index) {
            if (transition_index) airport_json << ',';
            const auto& transition = approach.transitions[transition_index];
            airport_json << "{\"id\":\"" << json_escape(transition.identifier) << "\",\"legs\":[";
            for (std::size_t leg_index = 0; leg_index < transition.legs.size(); ++leg_index) {
                if (leg_index) airport_json << ',';
                const auto& leg = transition.legs[leg_index];
                airport_json << "{\"id\":\"" << json_escape(leg.identifier)
                             << "\",\"sequence\":" << leg.sequence
                             << ",\"altitudeFt\":" << leg.altitude_feet << '}';
            }
            airport_json << "]}";
        }
        airport_json << "],\"finalLegs\":[";
        for (std::size_t leg_index = 0; leg_index < approach.final_legs.size(); ++leg_index) {
            if (leg_index) airport_json << ',';
            const auto& leg = approach.final_legs[leg_index];
            airport_json << "{\"id\":\"" << json_escape(leg.identifier)
                         << "\",\"sequence\":" << leg.sequence
                         << ",\"altitudeFt\":" << leg.altitude_feet << '}';
        }
        airport_json << "]}";
    }
    airport_json << "]}";

    std::ostringstream library_json;
    std::vector<std::filesystem::path> library_paths;
    library_json << "{\"airport\":\"" << json_escape(briefing_model_.library_airport())
                 << "\",\"message\":\"" << json_escape(briefing_model_.library_message())
                 << "\",\"entries\":[";
    const auto& entries = briefing_model_.library();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        if (index) library_json << ',';
        const auto& entry = entries[index];
        library_json << "{\"id\":" << index << ",\"category\":\""
                     << (entry.category == LibraryCategory::chart ? "chart" : "document")
                     << "\",\"name\":\"" << json_escape(entry.name) << "\"}";
        library_paths.emplace_back(entry.path);
    }
    library_json << "]}";

    std::lock_guard lock(mutex_);
    snapshot_json_ = std::move(json).str();
    airport_json_ = std::move(airport_json).str();
    library_json_ = std::move(library_json).str();
    library_paths_ = std::move(library_paths);
}

float XPlaneMobileServer::flight_loop(float, float, int, void* refcon) {
    static_cast<XPlaneMobileServer*>(refcon)->sample();
    return 0.5F;
}

void XPlaneMobileServer::serve() {
#if IBM
    const SOCKET server = static_cast<SOCKET>(listen_socket_);
    while (!stopping_) {
        const SOCKET client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (stopping_) break;
            continue;
        }
        DWORD timeout_ms = 2000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        handle_client(static_cast<std::uintptr_t>(client));
        shutdown(client, SD_BOTH);
        closesocket(client);
    }
#endif
}

void XPlaneMobileServer::handle_client(std::uintptr_t client_socket) {
#if IBM
    auto connection = tls_context_.accept(client_socket);
    if (!connection) return;
    std::string request;
    request.reserve(8192);
    std::size_t expected_size{};
    while (request.size() < 65536) {
        std::string plaintext;
        if (!connection->receive(plaintext)) break;
        request.append(plaintext);
        const auto headers_end = request.find("\r\n\r\n");
        if (headers_end == std::string::npos) continue;
        if (!expected_size) {
            std::string lower_headers = request.substr(0, headers_end);
            std::transform(lower_headers.begin(), lower_headers.end(), lower_headers.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            const auto content_length = lower_headers.find("content-length:");
            std::size_t body_size{};
            if (content_length != std::string::npos) {
                const auto value_start = content_length + 15;
                const auto value_end = lower_headers.find("\r\n", value_start);
                try { body_size = std::stoul(lower_headers.substr(value_start, value_end - value_start)); }
                catch (...) { body_size = 65537; }
            }
            if (body_size > 60000) {
                send_response(*connection, 400, "application/json", "{\"error\":\"request too large\"}", true);
                return;
            }
            expected_size = headers_end + 4 + body_size;
        }
        if (request.size() >= expected_size) break;
    }
    if (request.empty()) return;
    const auto first_space = request.find(' ');
    const auto path_end = first_space == std::string::npos ? std::string::npos : request.find(' ', first_space + 1);
    if (first_space == std::string::npos || path_end == std::string::npos) return;
    const std::string method = request.substr(0, first_space);
    const std::string target = request.substr(first_space + 1, path_end - first_space - 1);
    const auto headers_end = request.find("\r\n\r\n");
    const std::string_view body = headers_end == std::string::npos ? std::string_view{}
        : std::string_view(request).substr(headers_end + 4);
    const auto query = query_values(target);
    auto query_value = [&](std::string_view key) -> std::string {
        const auto found = query.find(std::string(key));
        return found == query.end() ? std::string{} : found->second;
    };
    auto authenticated = [&] {
        std::lock_guard lock(mutex_);
        return !session_token_.empty() && query_value("token") == session_token_;
    };

    if (method == "GET" && target.starts_with("/api/v2/session")) {
        MobileServerStatus current_status;
        std::string token;
        {
            std::lock_guard lock(mutex_);
            current_status = status_;
            token = session_token_;
        }
        if (query_value("code") != current_status.pairing_code) {
            send_response(*connection, 401, "application/json", "{\"error\":\"pairing code required\"}", true);
            return;
        }
        send_response(*connection, 200, "application/json; charset=utf-8",
                      "{\"version\":\"2.0\",\"token\":\"" + token + "\"}", true);
        return;
    }

    if (method == "GET" && target.starts_with("/api/v1/snapshot")) {
        MobileServerStatus current_status;
        std::string snapshot;
        {
            std::lock_guard lock(mutex_);
            current_status = status_;
            snapshot = snapshot_json_;
        }
        if (query_value("code") != current_status.pairing_code) {
            send_response(*connection, 401, "application/json", "{\"error\":\"pairing code required\"}", true);
            return;
        }
        send_response(*connection, 200, "application/json; charset=utf-8", snapshot, true);
        return;
    }

    if (target.starts_with("/api/v2/") && !authenticated()) {
        send_response(*connection, 401, "application/json", "{\"error\":\"session expired\"}", true);
        return;
    }
    if (method == "GET" && target.starts_with("/api/v2/snapshot")) {
        std::string snapshot;
        { std::lock_guard lock(mutex_); snapshot = snapshot_json_; }
        send_response(*connection, 200, "application/json; charset=utf-8", snapshot, true);
        return;
    }
    if (method == "GET" && target.starts_with("/api/v2/airport")) {
        std::string airport;
        { std::lock_guard lock(mutex_); airport = airport_json_; }
        send_response(*connection, 200, "application/json; charset=utf-8", airport, true);
        return;
    }
    if (method == "GET" && target.starts_with("/api/v2/library/file")) {
        const auto id = unsigned_number(query_value("id"));
        std::filesystem::path path;
        {
            std::lock_guard lock(mutex_);
            if (id && *id < library_paths_.size()) path = library_paths_[static_cast<std::size_t>(*id)];
        }
        if (path.empty() || !std::filesystem::is_regular_file(path)) {
            send_response(*connection, 404, "application/json", "{\"error\":\"document unavailable\"}", true);
            return;
        }
        const auto file = read_file(path);
        if (file.empty() || file.size() > 32 * 1024 * 1024) {
            send_response(*connection, 404, "application/json", "{\"error\":\"document unavailable\"}", true);
            return;
        }
        send_response(*connection, 200, content_type(path), file, true);
        return;
    }
    if (method == "GET" && target.starts_with("/api/v2/library")) {
        std::string library;
        { std::lock_guard lock(mutex_); library = library_json_; }
        send_response(*connection, 200, "application/json; charset=utf-8", library, true);
        return;
    }
    if (method == "GET" && target.starts_with("/api/v2/command")) {
        const auto id = unsigned_number(query_value("id"));
        if (!id) {
            send_response(*connection, 400, "application/json", "{\"error\":\"command id required\"}", true);
            return;
        }
        std::optional<CommandResult> result;
        {
            std::lock_guard lock(mutex_);
            const auto found = std::find_if(command_results_.begin(), command_results_.end(),
                                            [&](const auto& value) { return value.id == *id; });
            if (found != command_results_.end()) result = *found;
        }
        if (!result) {
            send_response(*connection, 200, "application/json", "{\"done\":false}", true);
            return;
        }
        send_response(*connection, 200, "application/json; charset=utf-8",
                      "{\"done\":true,\"success\":" + std::string(result->success ? "true" : "false") +
                      ",\"message\":\"" + json_escape(result->message) + "\"}", true);
        return;
    }
    if (method == "POST" && target.starts_with("/api/v2/commands")) {
        const auto values = form_values(body);
        const auto action_value = values.find("action");
        if (action_value == values.end()) {
            send_response(*connection, 400, "application/json", "{\"error\":\"action required\"}", true);
            return;
        }
        MobileCommand command;
        const auto revision = values.find("revision");
        if (revision != values.end()) command.expected_revision = unsigned_number(revision->second).value_or(0);
        if (action_value->second == "apply_route") {
            command.kind = CommandKind::apply_route;
            const auto route = values.find("route");
            if (route == values.end() || (command.route = parse_route(route->second)).size() < 2) {
                send_response(*connection, 400, "application/json", "{\"error\":\"valid route required\"}", true);
                return;
            }
        } else if (action_value->second == "search_airport") {
            command.kind = CommandKind::search_airport;
            const auto airport = values.find("airport");
            command.airport = airport == values.end() ? std::string{} : uppercase(airport->second);
            if (command.airport.size() < 2 || command.airport.size() > 7 ||
                !std::all_of(command.airport.begin(), command.airport.end(), [](unsigned char value) {
                    return std::isalnum(value) != 0;
                })) {
                send_response(*connection, 400, "application/json", "{\"error\":\"valid airport required\"}", true);
                return;
            }
        } else if (action_value->second == "apply_approach") {
            command.kind = CommandKind::apply_approach;
            command.airport = uppercase(values.contains("airport") ? values.at("airport") : "");
            command.approach = uppercase(values.contains("approach") ? values.at("approach") : "");
            command.transition = uppercase(values.contains("transition") ? values.at("transition") : "");
            command.destination_endpoint = !values.contains("destination") || values.at("destination") != "0";
            if (values.contains("excluded")) {
                for (auto value : split(values.at("excluded"), ',')) if (!value.empty()) command.excluded_fixes.push_back(value);
            }
            if (command.airport.empty() || command.approach.empty()) {
                send_response(*connection, 400, "application/json", "{\"error\":\"approach selection required\"}", true);
                return;
            }
        } else if (action_value->second == "refresh_library") {
            command.kind = CommandKind::refresh_library;
        } else {
            send_response(*connection, 400, "application/json", "{\"error\":\"unknown action\"}", true);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            command.id = next_command_id_++;
            pending_commands_.push_back(command);
        }
        send_response(*connection, 202, "application/json", "{\"commandId\":" + std::to_string(command.id) + "}", true);
        return;
    }

    if (method != "GET") {
        send_response(*connection, 404, "text/plain", "Not found", true);
        return;
    }
    std::string relative = target == "/" ? "index.html" : target.substr(1);
    const auto static_query = relative.find('?');
    if (static_query != std::string::npos) relative.resize(static_query);
    if (relative.find("..") != std::string::npos || relative.find('\\') != std::string::npos) {
        send_response(*connection, 404, "text/plain", "Not found", true);
        return;
    }
    const auto path = web_root_ / relative;
    const auto file_body = read_file(path);
    if (file_body.empty()) {
        send_response(*connection, 404, "text/plain", "Not found", true);
        return;
    }
    // Mobile assets change rapidly during simulator testing. Avoid Safari keeping
    // an older pairing script after the plugin folder is replaced.
    send_response(*connection, 200, content_type(path), file_body, true);
#else
    (void)client_socket;
#endif
}

} // namespace openefb::xplane
