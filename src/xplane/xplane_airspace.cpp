#include "xplane_airspace.hpp"

#include <XPLMUtilities.h>

#include <array>
#include <fstream>

namespace openefb::xplane {

XPlaneAirspace::XPlaneAirspace(AirspaceModel& model) : model_(model) {
    std::array<char, 1024> system_path{};
    XPLMGetSystemPath(system_path.data());
    xplane_root_ = std::filesystem::path(system_path.data());
}

XPlaneAirspace::~XPlaneAirspace() { stop(); }

bool XPlaneAirspace::start() {
    if (worker_.joinable()) return true;
    model_.begin_load();
    try {
        worker_ = std::thread([this] { load(); });
        return true;
    } catch (...) {
        model_.update(AirspaceLoadState::error, {}, "Airspace worker could not start");
        return false;
    }
}

void XPlaneAirspace::stop() {
    if (worker_.joinable()) worker_.join();
}

void XPlaneAirspace::load() {
    const std::array paths{
        xplane_root_ / "Custom Data" / "airspaces" / "airspace.txt",
        xplane_root_ / "Resources" / "default data" / "airspaces" / "airspace.txt",
    };
    for (const auto& path : paths) {
        std::ifstream input(path);
        if (!input) continue;
        try {
            auto zones = parse_openair(input);
            const auto count = zones.size();
            model_.update(AirspaceLoadState::ready,
                          std::make_shared<const std::vector<AirspaceZone>>(std::move(zones)),
                          std::to_string(count) + " airspace zones loaded");
        } catch (...) {
            model_.update(AirspaceLoadState::error, {}, "Installed OpenAIR data could not be read");
        }
        return;
    }
    model_.update(AirspaceLoadState::unavailable, {}, "No installed X-Plane airspace file found");
}

} // namespace openefb::xplane
