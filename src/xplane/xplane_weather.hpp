#pragma once

#include "openefb/core/flight_plan_model.hpp"
#include "openefb/core/weather_model.hpp"

#include <XPLMProcessing.h>

#include <filesystem>
#include <memory>

namespace openefb::xplane {

class XPlaneWeather final {
public:
    XPlaneWeather(WeatherModel& model, const FlightPlanModel& flight_plan_model,
                  std::filesystem::path cache_directory);
    ~XPlaneWeather();

    XPlaneWeather(const XPlaneWeather&) = delete;
    XPlaneWeather& operator=(const XPlaneWeather&) = delete;

    bool start();
    void stop();

private:
    void sample();
    static float flight_loop(float elapsed_since_last_call, float elapsed_since_last_loop,
                             int counter, void* refcon);

    WeatherModel& model_;
    const FlightPlanModel& flight_plan_model_;
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
    XPLMFlightLoopID flight_loop_id_{nullptr};
};

} // namespace openefb::xplane
