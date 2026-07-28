#include "xplane_flight_logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <utility>

namespace openefb::xplane {
namespace {
std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
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
}

XPlaneFlightLogger::XPlaneFlightLogger(FlightLogModel& model, const TelemetryModel& telemetry,
                                       const FlightPlanModel& flight_plan,
                                       std::filesystem::path history_path)
    : model_(model), telemetry_(telemetry), flight_plan_(flight_plan),
      history_path_(std::move(history_path)) {}
XPlaneFlightLogger::~XPlaneFlightLogger() { stop(); }
bool XPlaneFlightLogger::start() {
    if (flight_loop_id_) return true;
    load_history();
    on_ground_ = XPLMFindDataRef("sim/flightmodel/failures/onground_any");
    if (!on_ground_) return false;
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) return false;
    XPLMScheduleFlightLoop(flight_loop_id_, 1.0F, 1);
    return true;
}
void XPlaneFlightLogger::stop() {
    if (flight_loop_id_) { XPLMDestroyFlightLoop(flight_loop_id_); flight_loop_id_ = nullptr; }
    save_history();
}
void XPlaneFlightLogger::sample(float elapsed_seconds) {
    model_.update(telemetry_.snapshot(), flight_plan_.snapshot(),
                  XPLMGetDatai(on_ground_) != 0, elapsed_seconds, utc_timestamp());
    const auto& entries = model_.snapshot().entries;
    const std::string current_front = entries.empty() ? std::string{} : entries.front().completed_utc;
    if (current_front != persisted_front_timestamp_) {
        save_history();
        persisted_front_timestamp_ = current_front;
    }
}
float XPlaneFlightLogger::flight_loop(float elapsed, float, int, void* refcon) {
    auto* logger = static_cast<XPlaneFlightLogger*>(refcon);
    if (!logger) return 0.0F;
    logger->sample(elapsed);
    return 1.0F;
}

void XPlaneFlightLogger::load_history() {
    std::vector<FlightLogEntry> entries;
    try {
        std::ifstream input(history_path_);
        std::string signature;
        int version{};
        if (!(input >> signature >> version) || signature != "OpenEFBFlightLog" || version != 1) {
            return;
        }
        std::string record;
        while (entries.size() < 100 && input >> record) {
            if (record != "F") break;
            FlightLogEntry entry;
            std::size_t track_count{};
            if (!(input >> std::quoted(entry.completed_utc)
                        >> std::quoted(entry.aircraft_name)
                        >> std::quoted(entry.departure)
                        >> std::quoted(entry.destination)
                        >> entry.airborne_seconds >> entry.distance_nm
                        >> entry.maximum_altitude_feet >> entry.landing_vertical_speed_fpm
                        >> track_count) || track_count > 4096) break;
            bool complete = true;
            for (std::size_t index = 0; index < track_count; ++index) {
                FlightTrackPoint point;
                if (!(input >> record) || record != "P" ||
                    !(input >> point.latitude_degrees >> point.longitude_degrees >>
                      point.altitude_feet)) {
                    complete = false;
                    break;
                }
                entry.track.push_back(point);
            }
            if (!complete) break;
            entries.push_back(std::move(entry));
        }
    } catch (...) {
        return;
    }
    model_.replace_entries(std::move(entries));
    const auto& loaded = model_.snapshot().entries;
    persisted_front_timestamp_ = loaded.empty() ? std::string{} : loaded.front().completed_utc;
}

void XPlaneFlightLogger::save_history() const {
    try {
        std::filesystem::create_directories(history_path_.parent_path());
        const auto temporary = history_path_.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::trunc);
            output << "OpenEFBFlightLog 1\n";
            output << std::setprecision(10);
            for (const auto& entry : model_.snapshot().entries) {
                output << "F " << std::quoted(entry.completed_utc) << ' '
                       << std::quoted(entry.aircraft_name) << ' '
                       << std::quoted(entry.departure) << ' '
                       << std::quoted(entry.destination) << ' '
                       << entry.airborne_seconds << ' ' << entry.distance_nm << ' '
                       << entry.maximum_altitude_feet << ' '
                       << entry.landing_vertical_speed_fpm << ' '
                       << entry.track.size() << '\n';
                for (const auto& point : entry.track) {
                    output << "P " << point.latitude_degrees << ' '
                           << point.longitude_degrees << ' ' << point.altitude_feet << '\n';
                }
            }
            if (!output.good()) return;
        }
        std::error_code error;
        std::filesystem::remove(history_path_, error);
        error.clear();
        std::filesystem::rename(temporary, history_path_, error);
        if (error) std::filesystem::remove(temporary, error);
    } catch (...) {
    }
}
} // namespace openefb::xplane
