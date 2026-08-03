#include "xplane_briefing_library.hpp"

#include "openefb/core/faa_chart_catalog.hpp"

#if IBM
#include <Windows.h>
#include <shellapi.h>
#include <winhttp.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace openefb::xplane {
namespace {

constexpr float sample_interval_seconds = 2.0F;
constexpr auto automatic_refresh_interval = std::chrono::minutes(5);
constexpr std::size_t maximum_catalog_size = 48 * 1024 * 1024;
constexpr std::size_t maximum_chart_size = 24 * 1024 * 1024;

bool valid_faa_catalog(std::string_view catalog) {
    return catalog.size() > 1024 && catalog.find("<digital_tpp ") != std::string_view::npos &&
           catalog.rfind("</digital_tpp>") != std::string_view::npos;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool supported_extension(const std::string& extension) {
    static const std::set<std::string> extensions{".pdf", ".txt", ".md"};
    return extensions.contains(extension);
}

std::string read_text(const std::filesystem::path& path, std::size_t maximum = 32 * 1024) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto size = input.tellg();
    if (size <= 0 || size > static_cast<std::streamoff>(maximum)) return {};
    std::string content(static_cast<std::size_t>(size), '\0');
    input.seekg(0);
    input.read(content.data(), size);
    return input ? content : std::string{};
}

bool write_text(const std::filesystem::path& path, std::string_view content) {
    try {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        return output.good();
    } catch (...) {
        return false;
    }
}

bool write_binary(const std::filesystem::path& path, const std::vector<std::uint8_t>& content) {
    if (content.empty()) return false;
    try {
        std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".download";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(content.data()),
                         static_cast<std::streamsize>(content.size()));
            if (!output.good()) return false;
        }
        std::error_code error;
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

std::string pdf_escape(std::string value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (unsigned char character : value) {
        if (character == '(' || character == ')' || character == '\\') escaped += '\\';
        escaped += character >= 32 && character <= 126 ? static_cast<char>(character) : '?';
    }
    return escaped;
}

std::vector<std::uint8_t> briefing_pdf(std::string_view text) {
    std::vector<std::string> lines;
    std::istringstream input{std::string(text)};
    std::string source_line;
    while (std::getline(input, source_line) && lines.size() < 48) {
        if (source_line.empty()) { lines.emplace_back(); continue; }
        while (source_line.size() > 88 && lines.size() < 48) {
            auto split = source_line.rfind(' ', 88);
            if (split == std::string::npos || split < 20) split = 88;
            lines.push_back(source_line.substr(0, split));
            source_line.erase(0, std::min(source_line.size(), split + 1));
        }
        if (lines.size() < 48) lines.push_back(std::move(source_line));
    }
    std::ostringstream content;
    content << "BT\n/F1 11 Tf\n50 750 Td\n14 TL\n";
    for (const auto& line : lines) content << '(' << pdf_escape(line) << ") Tj\nT*\n";
    content << "ET\n";
    const std::string stream = content.str();
    const std::array<std::string, 5> objects{
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>",
        "<< /Length " + std::to_string(stream.size()) + " >>\nstream\n" + stream + "endstream",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    };
    std::string pdf = "%PDF-1.4\n";
    std::array<std::size_t, 5> offsets{};
    for (std::size_t index = 0; index < objects.size(); ++index) {
        offsets[index] = pdf.size();
        pdf += std::to_string(index + 1) + " 0 obj\n" + objects[index] + "\nendobj\n";
    }
    const auto xref = pdf.size();
    pdf += "xref\n0 6\n0000000000 65535 f \n";
    char offset[32]{};
    for (const auto value : offsets) {
        std::snprintf(offset, sizeof(offset), "%010zu 00000 n \n", value);
        pdf += offset;
    }
    pdf += "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return {pdf.begin(), pdf.end()};
}

std::string safe_filename(std::string value) {
    for (char& character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != ' ' && character != '-' && character != '_' &&
            character != '(' && character != ')') character = '_';
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '.')) value.pop_back();
    if (value.size() > 110) value.resize(110);
    return value.empty() ? "FAA Chart" : value;
}

std::string utc_timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if IBM
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char value[32]{};
    std::strftime(value, sizeof(value), "%Y-%m-%d %H:%M UTC", &utc);
    return value;
}

std::string current_faa_cycle() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if IBM
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    return faa_cycle_for_date(utc.tm_year + 1900, static_cast<unsigned>(utc.tm_mon + 1),
                              static_cast<unsigned>(utc.tm_mday));
}

#if IBM
struct FaaDownload {
    std::vector<std::uint8_t> data;
    std::string status;
};

FaaDownload download_faa_file(const std::wstring& path, std::size_t maximum_size) {
    HINTERNET session = WinHttpOpen(L"OpenEFB/1.1.0-mobile-2 (+https://github.com/Gbaby4live/OpenEFB)",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {{}, "WinHTTP session error " + std::to_string(GetLastError())};
    WinHttpSetTimeouts(session, 5000, 5000, 30000, 30000);
    DWORD secure_protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secure_protocols, sizeof(secure_protocols));
    HINTERNET connection = WinHttpConnect(session, L"aeronav.faa.gov", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connection ? WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    FaaDownload result;
    result.status = connection ? "FAA request failed" :
                                 "FAA connection error " + std::to_string(GetLastError());
    if (request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status{};
        DWORD status_size = sizeof(status);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                WINHTTP_NO_HEADER_INDEX) && status == 200) {
            result.status = "FAA HTTP 200";
            DWORD expected_size{};
            DWORD expected_size_length = sizeof(expected_size);
            const bool has_expected_size = WinHttpQueryHeaders(
                request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &expected_size, &expected_size_length,
                WINHTTP_NO_HEADER_INDEX) != FALSE;
            for (;;) {
                DWORD available{};
                if (!WinHttpQueryDataAvailable(request, &available)) {
                    result.data.clear();
                    result.status = "FAA response ended early (error " +
                                    std::to_string(GetLastError()) + ")";
                    break;
                }
                if (available == 0) break;
                if (result.data.size() + available > maximum_size) {
                    result.data.clear();
                    result.status = "FAA file exceeded the safe download limit";
                    break;
                }
                const auto offset = result.data.size();
                result.data.resize(offset + available);
                DWORD read{};
                if (!WinHttpReadData(request, result.data.data() + offset, available, &read)) {
                    result.data.clear();
                    result.status = "FAA read error " + std::to_string(GetLastError());
                    break;
                }
                result.data.resize(offset + read);
            }
            if (has_expected_size && !result.data.empty() &&
                result.data.size() != expected_size) {
                result.status = "FAA response was incomplete (" +
                    std::to_string(result.data.size()) + " of " +
                    std::to_string(expected_size) + " bytes)";
                result.data.clear();
            }
        } else if (status != 0) {
            result.status = "FAA HTTP " + std::to_string(status);
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (result.data.empty() && result.status == "FAA HTTP 200")
        result.status = "FAA returned an empty file";
    return result;
}
#else
struct FaaDownload { std::vector<std::uint8_t> data; std::string status; };
FaaDownload download_faa_file(const std::wstring&, std::size_t) {
    return {{}, "Automatic FAA downloads are currently available in the Windows build"};
}
#endif

std::string route_text(const std::vector<std::string>& route) {
    std::string value;
    for (const auto& waypoint : route) {
        if (!value.empty()) value += " > ";
        value += waypoint;
        if (value.size() > 1000) { value += " ..."; break; }
    }
    return value;
}

} // namespace

XPlaneBriefingLibrary::XPlaneBriefingLibrary(BriefingModel& model,
                                               const FlightPlanModel& flight_plan_model,
                                               const WeatherModel& weather_model,
                                               const PlanningModel& planning_model,
                                               std::filesystem::path directory)
    : model_(model), flight_plan_model_(flight_plan_model), weather_model_(weather_model),
      planning_model_(planning_model), directory_(std::move(directory)) {}

XPlaneBriefingLibrary::~XPlaneBriefingLibrary() { stop(); }

bool XPlaneBriefingLibrary::start() {
    if (flight_loop_id_) return true;
    stopping_ = false;
    try {
        worker_ = std::thread([this] { work(); });
    } catch (...) {
        model_.update_library({}, "Airport archive worker could not start");
        return false;
    }
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) {
        stop();
        return false;
    }
    refresh();
    sample();
    XPLMScheduleFlightLoop(flight_loop_id_, sample_interval_seconds, 1);
    return true;
}

void XPlaneBriefingLibrary::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_archive_.reset();
    }
    condition_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void XPlaneBriefingLibrary::refresh() {
    ArchiveRequest request;
    const auto& flight_plan = flight_plan_model_.snapshot();
    for (const auto& leg : flight_plan.legs) {
        if (leg.kind != WaypointKind::airport) continue;
        if (request.departure.empty()) request.departure = leg.identifier;
        request.destination = leg.identifier;
        request.route.push_back(leg.identifier);
    }
    if (!request.departure.empty() && !request.destination.empty()) {
        request.route.clear();
        for (const auto& leg : flight_plan.legs) request.route.push_back(leg.identifier);
        request.weather = weather_model_.snapshot();
        request.planning = planning_model_.snapshot();
    }
    {
        std::lock_guard lock(mutex_);
        refresh_requested_ = true;
        if (!request.departure.empty() && !request.destination.empty())
            pending_archive_ = std::move(request);
    }
    condition_.notify_one();
}

void XPlaneBriefingLibrary::sample() {
    {
        std::optional<std::vector<LibraryEntry>> ready;
        std::string message;
        {
            std::lock_guard lock(mutex_);
            ready = std::move(ready_entries_);
            ready_entries_.reset();
            message = std::move(ready_message_);
            ready_message_.clear();
        }
        if (ready) model_.update_library(std::move(*ready), std::move(message));
    }

    const auto& flight_plan = flight_plan_model_.snapshot();
    std::string departure;
    std::string destination;
    for (const auto& leg : flight_plan.legs) {
        if (leg.kind != WaypointKind::airport) continue;
        if (departure.empty()) departure = leg.identifier;
        destination = leg.identifier;
    }
    if (departure.empty() || destination.empty()) return;
    const auto& weather = weather_model_.snapshot();
    const std::string endpoints = departure + ">" + destination;
    const std::string weather_signature = weather.departure.airport_id + "\n" +
        weather.departure.metar + "\n" + weather.destination.airport_id + "\n" +
        weather.destination.metar;
    const auto now = std::chrono::steady_clock::now();
    const bool route_or_weather_changed =
        endpoints != last_endpoints_ || weather_signature != last_weather_signature_;
    const bool periodic_refresh_due =
        last_archive_time_.time_since_epoch().count() == 0 ||
        now - last_archive_time_ >= automatic_refresh_interval;
    if (!route_or_weather_changed && !periodic_refresh_due) return;

    ArchiveRequest request;
    request.departure = departure;
    request.destination = destination;
    request.weather = weather;
    request.planning = planning_model_.snapshot();
    for (const auto& leg : flight_plan.legs) request.route.push_back(leg.identifier);
    {
        std::lock_guard lock(mutex_);
        pending_archive_ = std::move(request);
    }
    last_endpoints_ = endpoints;
    last_weather_signature_ = weather_signature;
    last_archive_time_ = now;
    condition_.notify_one();
}

void XPlaneBriefingLibrary::work() {
    for (;;) {
        std::optional<ArchiveRequest> request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stopping_ || pending_archive_.has_value() || refresh_requested_;
            });
            if (stopping_) return;
            request = std::move(pending_archive_);
            pending_archive_.reset();
            refresh_requested_ = false;
        }
        if (request) archive(*request);
        auto entries = scan_library();
        {
            std::lock_guard lock(mutex_);
            ready_entries_ = std::move(entries);
            ready_message_ = request ? "DEP/DEST briefs and charts synchronized"
                                     : "Output/preferences/OpenEFB/Library";
        }
    }
}

void XPlaneBriefingLibrary::archive(const ArchiveRequest& request) {
    std::vector<std::string> airports{request.departure};
    if (request.destination != request.departure) airports.push_back(request.destination);
    const std::string cycle = current_faa_cycle();
    std::string catalog;
    std::string catalog_status = "saved catalog";
    const auto catalog_path = directory_ / "faa-cache" / ("d-TPP_Metafile-" + cycle + ".xml");
    catalog = read_text(catalog_path, maximum_catalog_size);
    if (!catalog.empty() && !valid_faa_catalog(catalog)) {
        catalog.clear();
        catalog_status = "discarded an incomplete saved FAA catalog";
        std::error_code ignored;
        std::filesystem::remove(catalog_path, ignored);
    }
    if (catalog.empty() && !stopping_) {
        const std::wstring remote = L"/d-tpp/" + std::wstring(cycle.begin(), cycle.end()) +
                                    L"/xml_data/d-TPP_Metafile.xml";
        for (int attempt = 1; attempt <= 3 && catalog.empty() && !stopping_; ++attempt) {
            const auto downloaded = download_faa_file(remote, maximum_catalog_size);
            catalog_status = downloaded.status + " (attempt " + std::to_string(attempt) + ")";
            if (downloaded.data.empty()) continue;
            std::string candidate(reinterpret_cast<const char*>(downloaded.data.data()),
                                  downloaded.data.size());
            if (!valid_faa_catalog(candidate)) {
                catalog_status = "FAA catalog download was incomplete (attempt " +
                                 std::to_string(attempt) + ")";
                continue;
            }
            if (write_binary(catalog_path, downloaded.data)) catalog = std::move(candidate);
            else catalog_status = "FAA catalog could not be saved";
        }
    }

    for (const auto& airport : airports) {
        if (stopping_) return;
        const auto chart_folder = directory_ / "Charts" / airport;
        const auto document_folder = directory_ / "Documents" / airport;
        try {
            std::filesystem::create_directories(chart_folder);
            std::filesystem::create_directories(document_folder);
        } catch (...) {
            continue;
        }
        std::ostringstream briefing;
        briefing << "OpenEFB Airport Briefing Archive\n"
                 << "Saved: " << utc_timestamp() << "\n"
                 << "Airport: " << airport << "\n"
                 << "Role: " << (airport == request.departure ? "Departure" : "Destination") << "\n"
                 << "Route: " << route_text(request.route) << "\n\n";
        const auto& airport_weather = airport == request.departure
            ? request.weather.departure : request.weather.destination;
        briefing << "METAR: " << (airport_weather.metar.empty() ? "Unavailable" : airport_weather.metar) << "\n";
        if (request.planning.available) {
            constexpr double pounds_per_kilogram = 2.2046226218;
            briefing << "Gross weight: "
                     << request.planning.loading.gross_weight_kg * pounds_per_kilogram << " lb\n"
                     << "Fuel: "
                     << request.planning.loading.fuel_weight_kg * pounds_per_kilogram << " lb\n"
                     << "Reserve: " << request.planning.reserve_minutes << " minutes\n";
            if (request.planning.fuel_plan_available) {
                briefing << "Trip fuel: "
                         << request.planning.trip_fuel_kg * pounds_per_kilogram << " lb\n"
                         << "Fuel margin: "
                         << request.planning.fuel_margin_kg * pounds_per_kilogram << " lb\n"
                         << "Predicted landing weight: "
                         << request.planning.predicted_landing_weight_kg * pounds_per_kilogram
                         << " lb\n";
            }
        }
        briefing << "\nSimulator planning aid only. Verify current official aviation information.\n";
        const auto briefing_path = document_folder / "OpenEFB-Latest-Briefing.pdf";
        if (write_binary(briefing_path, briefing_pdf(briefing.str()))) {
            std::error_code ignored;
            std::filesystem::remove(document_folder / "OpenEFB-Latest-Briefing.txt", ignored);
        }

        const auto chart_status = chart_folder / "Chart Download Status.pdf";
        {
            std::error_code ignored;
            std::filesystem::remove(chart_folder / "Chart Download Status.txt", ignored);
        }
        if (catalog.empty()) {
            write_binary(chart_status, briefing_pdf(
                "OpenEFB Chart Download Status\n\nFAA chart catalog was not available. "
                "Cycle: " + cycle + "\nAirport: " + airport + "\nResult: " + catalog_status +
                "\nCheck internet access, then select Refresh."));
            continue;
        }
        const auto charts = parse_faa_chart_catalog(catalog, airport);
        if (charts.empty()) {
            write_binary(chart_status, briefing_pdf(
                "OpenEFB Chart Download Status\n\nNo FAA d-TPP records matched airport " + airport +
                " in cycle " + cycle + ". OpenEFB checked both ICAO and domestic FAA identifiers. "
                "Confirm the active route contains the airport code, then select Refresh."));
            continue;
        }
        const auto marker_path = chart_folder / ".faa-cycle";
        if (read_text(marker_path, 32) == cycle) {
            const bool complete_cache = std::all_of(charts.begin(), charts.end(), [&](const auto& chart) {
                const auto stem = std::filesystem::path(chart.pdf_name).stem().string();
                const auto filename = safe_filename(
                    chart.code + " - " + chart.name + " [" + stem + "]") + ".pdf";
                std::error_code error;
                return std::filesystem::is_regular_file(chart_folder / filename, error) && !error;
            });
            if (complete_cache) continue;
        }
        bool complete = true;
        std::size_t downloaded_count{};
        std::string last_download_status;
        for (const auto& chart : charts) {
            if (stopping_) return;
            const auto stem = std::filesystem::path(chart.pdf_name).stem().string();
            const auto filename = safe_filename(chart.code + " - " + chart.name + " [" + stem + "]") + ".pdf";
            const auto local_path = chart_folder / filename;
            const std::wstring remote = L"/d-tpp/" + std::wstring(cycle.begin(), cycle.end()) + L"/" +
                                        std::wstring(chart.pdf_name.begin(), chart.pdf_name.end());
            const auto pdf = download_faa_file(remote, maximum_chart_size);
            last_download_status = pdf.status;
            if (pdf.data.size() < 5 || pdf.data[0] != '%' || pdf.data[1] != 'P' ||
                pdf.data[2] != 'D' || pdf.data[3] != 'F' ||
                !write_binary(local_path, pdf.data)) complete = false;
            else ++downloaded_count;
        }
        if (complete) {
            write_text(marker_path, cycle);
            std::error_code ignored;
            std::filesystem::remove(chart_status, ignored);
        } else {
            write_binary(chart_status, briefing_pdf(
                "OpenEFB Chart Download Status\n\nDownloaded " +
                std::to_string(downloaded_count) + " of " +
                std::to_string(charts.size()) + " FAA charts for " + airport +
                " in cycle " + cycle + ". Last result: " + last_download_status +
                ". Select Refresh to retry."));
        }
    }
}

std::vector<LibraryEntry> XPlaneBriefingLibrary::scan_library() const {
    std::vector<LibraryEntry> entries;
    try {
        const std::array folders{
            std::pair{directory_ / "Charts", LibraryCategory::chart},
            std::pair{directory_ / "Documents", LibraryCategory::document},
        };
        for (const auto& [folder, category] : folders) {
            std::filesystem::create_directories(folder);
            for (const auto& item : std::filesystem::recursive_directory_iterator(folder)) {
                if (entries.size() >= 500 || !item.is_regular_file()) continue;
                const auto extension = lowercase(item.path().extension().string());
                if (!supported_extension(extension)) continue;
                if (item.path().filename().string().find(".openefb.pdf") != std::string::npos) {
                    continue;
                }
                const auto relative = std::filesystem::relative(item.path(), folder).string();
                if (extension == ".txt" || extension == ".md") {
                    const auto text = read_text(item.path());
                    if (text.empty()) continue;
                    auto converted = item.path();
                    converted.replace_extension(".openefb.pdf");
                    bool needs_conversion = !std::filesystem::is_regular_file(converted);
                    if (!needs_conversion) {
                        std::error_code timestamp_error;
                        needs_conversion = std::filesystem::last_write_time(converted, timestamp_error) <
                                           std::filesystem::last_write_time(item.path(), timestamp_error);
                    }
                    if (needs_conversion && !write_binary(converted, briefing_pdf(text))) continue;
                    auto display_name = std::filesystem::path(relative);
                    display_name.replace_extension(".pdf");
                    entries.push_back({category, display_name.string(), converted.string(), {}});
                    continue;
                }
                entries.push_back({category, relative, item.path().string(), {}});
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
            if (left.category != right.category) return left.category < right.category;
            return lowercase(left.name) < lowercase(right.name);
        });
    } catch (...) {
    }
    return entries;
}

float XPlaneBriefingLibrary::flight_loop(float, float, int, void* refcon) {
    auto* library = static_cast<XPlaneBriefingLibrary*>(refcon);
    if (!library) return 0.0F;
    library->sample();
    return sample_interval_seconds;
}

bool XPlaneBriefingLibrary::open_selected() const {
    const auto* entry = model_.selected_entry();
    if (!entry) return false;
#if IBM
    const auto path = std::filesystem::path(entry->path).wstring();
    const auto result = reinterpret_cast<std::intptr_t>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    return false;
#endif
}

const std::filesystem::path& XPlaneBriefingLibrary::directory() const noexcept { return directory_; }

} // namespace openefb::xplane
