#include "openefb/core/telemetry_model.hpp"

#include <cmath>
#include <utility>

namespace openefb {

void TelemetryModel::update(TelemetrySnapshot snapshot) {
    snapshot.available = true;
    snapshot.heading_degrees = std::fmod(snapshot.heading_degrees, 360.0);
    if (snapshot.heading_degrees < 0.0) {
        snapshot.heading_degrees += 360.0;
    }
    if (snapshot.aircraft_name.empty()) {
        snapshot.aircraft_name = "Unknown aircraft";
    }
    snapshot.revision = next_revision_++;
    snapshot_ = std::move(snapshot);
}

void TelemetryModel::mark_unavailable() noexcept { snapshot_.available = false; }

const TelemetrySnapshot& TelemetryModel::snapshot() const noexcept { return snapshot_; }

} // namespace openefb
