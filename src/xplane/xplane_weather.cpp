#include "xplane_weather.hpp"

#include <XPLMWeather.h>

#include <algorithm>
#include <utility>

namespace openefb::xplane {

namespace {

constexpr float sample_interval_seconds = 15.0F;

AirportWeather read_metar(const FlightPlanLeg& leg) {
    AirportWeather weather;
    weather.airport_id = leg.identifier;
    XPLMFixedString150_t metar{};
    XPLMGetMETARForAirport(weather.airport_id.c_str(), &metar);
    weather.metar = metar.buffer;
    return weather;
}

} // namespace

XPlaneWeather::XPlaneWeather(WeatherModel& model, const FlightPlanModel& flight_plan_model)
    : model_(model), flight_plan_model_(flight_plan_model) {}

XPlaneWeather::~XPlaneWeather() { stop(); }

bool XPlaneWeather::start() {
    if (flight_loop_id_) {
        return true;
    }

    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_BeforeFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) {
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

    const auto departure = std::find_if(flight_plan.legs.begin(), flight_plan.legs.end(),
                                        [](const FlightPlanLeg& leg) {
                                            return leg.kind == WaypointKind::airport && !leg.identifier.empty();
                                        });
    const auto destination = std::find_if(flight_plan.legs.rbegin(), flight_plan.legs.rend(),
                                          [](const FlightPlanLeg& leg) {
                                              return leg.kind == WaypointKind::airport && !leg.identifier.empty();
                                          });
    if (departure != flight_plan.legs.end()) {
        snapshot.departure = read_metar(*departure);
    }
    if (destination != flight_plan.legs.rend()) {
        snapshot.destination = read_metar(*destination);
    }
    model_.update(std::move(snapshot));
}

float XPlaneWeather::flight_loop(float, float, int, void* refcon) {
    auto* weather = static_cast<XPlaneWeather*>(refcon);
    if (!weather) {
        return 0.0F;
    }
    weather->sample();
    return sample_interval_seconds;
}

} // namespace openefb::xplane
