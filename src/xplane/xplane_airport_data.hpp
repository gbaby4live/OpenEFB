#pragma once

#include "openefb/core/airport_info.hpp"

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace openefb::xplane {

class XPlaneAirportData final {
public:
    explicit XPlaneAirportData(AirportInfoModel& model);
    ~XPlaneAirportData();

    XPlaneAirportData(const XPlaneAirportData&) = delete;
    XPlaneAirportData& operator=(const XPlaneAirportData&) = delete;

    bool start();
    void stop();
    bool search(std::string identifier);

private:
    void work();
    AirportInfoSnapshot load(std::string identifier) const;

    AirportInfoModel& model_;
    std::filesystem::path xplane_root_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::string pending_identifier_;
    bool stopping_{false};
    std::thread worker_;
};

} // namespace openefb::xplane
