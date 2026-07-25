#include "openefb/core/navigation_database_model.hpp"

namespace openefb {

NavigationDatabaseModel::NavigationDatabaseModel() {
    snapshot_.points = std::make_shared<const std::vector<MapNavigationPoint>>();
}

void NavigationDatabaseModel::begin_load() {
    std::lock_guard lock(mutex_);
    snapshot_.loading = true;
    snapshot_.available = false;
    ++snapshot_.revision;
}

void NavigationDatabaseModel::update(std::vector<MapNavigationPoint> points, bool loading) {
    std::lock_guard lock(mutex_);
    snapshot_.points = std::make_shared<const std::vector<MapNavigationPoint>>(std::move(points));
    snapshot_.loading = loading;
    snapshot_.available = !snapshot_.points->empty();
    ++snapshot_.revision;
}

void NavigationDatabaseModel::mark_unavailable() {
    std::lock_guard lock(mutex_);
    snapshot_.loading = false;
    snapshot_.available = false;
    ++snapshot_.revision;
}

NavigationDatabaseSnapshot NavigationDatabaseModel::snapshot() const {
    std::lock_guard lock(mutex_);
    return snapshot_;
}

} // namespace openefb
