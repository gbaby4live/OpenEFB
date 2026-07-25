#include "xplane_airport_data.hpp"

#include <XPLMUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <utility>
#include <vector>

namespace openefb::xplane {

namespace {

std::string uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return value;
}

bool valid_identifier(const std::string& identifier) {
    return identifier.size() >= 2 && identifier.size() <= 7 &&
           std::all_of(identifier.begin(), identifier.end(), [](unsigned char character) {
               return std::isalnum(character) != 0;
           });
}

} // namespace

XPlaneAirportData::XPlaneAirportData(AirportInfoModel& model) : model_(model) {
    std::array<char, 1024> system_path{};
    XPLMGetSystemPath(system_path.data());
    xplane_root_ = std::filesystem::path(system_path.data());
}

XPlaneAirportData::~XPlaneAirportData() { stop(); }

bool XPlaneAirportData::start() {
    if (worker_.joinable()) return true;
    stopping_ = false;
    try {
        worker_ = std::thread([this] { work(); });
        return true;
    } catch (...) {
        AirportInfoSnapshot snapshot;
        snapshot.state = AirportLookupState::error;
        snapshot.message = "Airport-data worker could not start";
        model_.update(std::move(snapshot));
        return false;
    }
}

void XPlaneAirportData::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        pending_identifier_.clear();
    }
    condition_.notify_one();
    if (worker_.joinable()) worker_.join();
}

bool XPlaneAirportData::search(std::string identifier) {
    identifier = uppercase(std::move(identifier));
    if (!valid_identifier(identifier) || !worker_.joinable()) return false;
    model_.begin_search(identifier);
    {
        std::lock_guard lock(mutex_);
        pending_identifier_ = std::move(identifier);
    }
    condition_.notify_one();
    return true;
}

void XPlaneAirportData::work() {
    for (;;) {
        std::string identifier;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !pending_identifier_.empty(); });
            if (stopping_) return;
            identifier = std::move(pending_identifier_);
            pending_identifier_.clear();
        }
        model_.update(load(std::move(identifier)));
    }
}

AirportInfoSnapshot XPlaneAirportData::load(std::string identifier) const {
    std::vector<std::filesystem::path> airport_files;
    std::set<std::filesystem::path> unique_files;
    try {
        const auto custom_scenery = xplane_root_ / "Custom Scenery";
        if (std::filesystem::is_directory(custom_scenery)) {
            for (const auto& entry : std::filesystem::directory_iterator(custom_scenery)) {
                const auto path = entry.path() / "Earth nav data" / "apt.dat";
                if (std::filesystem::is_regular_file(path) && unique_files.insert(path).second)
                    airport_files.push_back(path);
            }
        }
    } catch (...) {
    }
    const std::array built_in{
        xplane_root_ / "Global Scenery" / "Global Airports" / "Earth nav data" / "apt.dat",
        xplane_root_ / "Custom Scenery" / "Global Airports" / "Earth nav data" / "apt.dat",
        xplane_root_ / "Resources" / "default scenery" / "default apt dat" /
            "Earth nav data" / "apt.dat",
    };
    for (const auto& path : built_in) {
        if (unique_files.insert(path).second) airport_files.push_back(path);
    }

    std::optional<AirportInfoSnapshot> airport;
    for (const auto& path : airport_files) {
        std::ifstream input(path);
        if (!input) continue;
        airport = parse_airport_apt(input, identifier);
        if (airport) break;
    }
    if (!airport) {
        AirportInfoSnapshot result;
        result.state = AirportLookupState::not_found;
        result.identifier = identifier;
        result.message = "Airport not found in installed X-Plane scenery data";
        return result;
    }

    const std::array procedure_files{
        xplane_root_ / "Custom Data" / "CIFP" / (identifier + ".dat"),
        xplane_root_ / "Resources" / "default data" / "CIFP" / (identifier + ".dat"),
    };
    for (const auto& path : procedure_files) {
        std::ifstream input(path);
        if (!input) continue;
        parse_airport_procedures(input, *airport);
        break;
    }
    airport->state = AirportLookupState::ready;
    airport->message = "Installed X-Plane airport and procedure data";
    return *airport;
}

} // namespace openefb::xplane
