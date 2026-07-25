#pragma once

#include "openefb/core/briefing_model.hpp"
#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/planning_model.hpp"
#include "openefb/core/weather_model.hpp"

#include <XPLMProcessing.h>

#include <condition_variable>
#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace openefb::xplane {

class XPlaneBriefingLibrary final {
public:
    XPlaneBriefingLibrary(BriefingModel& model, const FlightPlanModel& flight_plan_model,
                          const WeatherModel& weather_model, const PlanningModel& planning_model,
                          std::filesystem::path directory);
    ~XPlaneBriefingLibrary();
    XPlaneBriefingLibrary(const XPlaneBriefingLibrary&) = delete;
    XPlaneBriefingLibrary& operator=(const XPlaneBriefingLibrary&) = delete;

    bool start();
    void stop();
    void refresh();
    [[nodiscard]] bool open_selected() const;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

private:
    struct ArchiveRequest {
        std::string departure;
        std::string destination;
        std::vector<std::string> route;
        WeatherSnapshot weather;
        PlanningSnapshot planning;
    };

    void sample();
    void work();
    void archive(const ArchiveRequest& request);
    [[nodiscard]] std::vector<LibraryEntry> scan_library() const;
    static float flight_loop(float, float, int, void* refcon);

    BriefingModel& model_;
    const FlightPlanModel& flight_plan_model_;
    const WeatherModel& weather_model_;
    const PlanningModel& planning_model_;
    std::filesystem::path directory_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<ArchiveRequest> pending_archive_;
    std::optional<std::vector<LibraryEntry>> ready_entries_;
    std::string ready_message_;
    std::string last_endpoints_;
    std::string last_weather_signature_;
    bool refresh_requested_{false};
    std::atomic_bool stopping_{false};
    std::thread worker_;
};

} // namespace openefb::xplane
