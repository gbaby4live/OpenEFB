#include "xplane_navigation_database.hpp"

#include <array>

namespace openefb::xplane {
namespace {

WaypointKind waypoint_kind(XPLMNavType type) {
    if (type == xplm_Nav_Airport) return WaypointKind::airport;
    if (type == xplm_Nav_VOR) return WaypointKind::vor;
    if (type == xplm_Nav_NDB) return WaypointKind::ndb;
    if (type == xplm_Nav_Fix) return WaypointKind::fix;
    return WaypointKind::other;
}

bool wanted(XPLMNavType type) {
    return type == xplm_Nav_Airport || type == xplm_Nav_VOR ||
           type == xplm_Nav_NDB || type == xplm_Nav_Fix;
}

} // namespace

XPlaneNavigationDatabase::XPlaneNavigationDatabase(NavigationDatabaseModel& model) : model_(model) {}
XPlaneNavigationDatabase::~XPlaneNavigationDatabase() { stop(); }

bool XPlaneNavigationDatabase::start() {
    if (flight_loop_id_) return true;
    points_.clear();
    next_ = XPLMGetFirstNavAid();
    if (next_ == XPLM_NAV_NOT_FOUND) return false;
    model_.begin_load();
    XPLMCreateFlightLoop_t parameters{};
    parameters.structSize = sizeof(parameters);
    parameters.phase = xplm_FlightLoop_Phase_AfterFlightModel;
    parameters.callbackFunc = flight_loop;
    parameters.refcon = this;
    flight_loop_id_ = XPLMCreateFlightLoop(&parameters);
    if (!flight_loop_id_) return false;
    XPLMScheduleFlightLoop(flight_loop_id_, -1.0F, 1);
    return true;
}

void XPlaneNavigationDatabase::stop() {
    if (flight_loop_id_) {
        XPLMDestroyFlightLoop(flight_loop_id_);
        flight_loop_id_ = nullptr;
    }
}

float XPlaneNavigationDatabase::scan() {
    constexpr int batch_size = 300;
    constexpr std::size_t maximum_points = 75000;
    for (int count = 0; count < batch_size && next_ != XPLM_NAV_NOT_FOUND; ++count) {
        const auto current = next_;
        next_ = XPLMGetNextNavAid(current);
        XPLMNavType type{};
        float latitude{};
        float longitude{};
        std::array<char, 64> identifier{};
        std::array<char, 256> name{};
        XPLMGetNavAidInfo(current, &type, &latitude, &longitude, nullptr, nullptr, nullptr,
                          identifier.data(), name.data(), nullptr);
        if (wanted(type) && identifier[0] != '\0' && points_.size() < maximum_points) {
            points_.push_back({identifier.data(), name.data(), waypoint_kind(type), latitude, longitude});
        }
    }
    if (next_ == XPLM_NAV_NOT_FOUND || points_.size() >= maximum_points) {
        model_.update(points_, false);
        return 0.0F;
    }
    if (points_.size() >= 1000 && points_.size() % 3000 < batch_size) model_.update(points_, true);
    return -1.0F;
}

float XPlaneNavigationDatabase::flight_loop(float, float, int, void* refcon) {
    auto* database = static_cast<XPlaneNavigationDatabase*>(refcon);
    return database ? database->scan() : 0.0F;
}

} // namespace openefb::xplane
