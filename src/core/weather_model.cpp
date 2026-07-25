#include "openefb/core/weather_model.hpp"

#include <utility>

namespace openefb {

void WeatherModel::update(WeatherSnapshot snapshot) {
    snapshot.available = true;
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

void WeatherModel::mark_unavailable() noexcept { snapshot_.available = false; }

const WeatherSnapshot& WeatherModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
