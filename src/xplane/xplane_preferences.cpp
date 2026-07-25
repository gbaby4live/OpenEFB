#include "xplane_preferences.hpp"

#include <XPLMUtilities.h>

#include <array>
#include <fstream>
#include <string>

namespace openefb::xplane {

XPlanePreferences::XPlanePreferences() {
    std::array<char, 512> preferences_path{};
    XPLMGetPrefsPath(preferences_path.data());
    XPLMExtractFileAndPath(preferences_path.data());
    file_path_ = std::filesystem::path(preferences_path.data()) / "OpenEFB.prf";
}

std::optional<WindowGeometry> XPlanePreferences::load_geometry() const {
    try {
        std::ifstream input(file_path_);
        std::string signature;
        int version{};
        WindowGeometry geometry;
        if (input >> signature >> version >> geometry.left >> geometry.top >> geometry.right >> geometry.bottom &&
            signature == "OpenEFB" && version == 1 && geometry.valid()) {
            return geometry;
        }
    } catch (...) {
    }
    return std::nullopt;
}

void XPlanePreferences::save_geometry(const WindowGeometry& geometry) const {
    if (!geometry.valid()) {
        return;
    }
    try {
        std::ofstream output(file_path_, std::ios::trunc);
        output << "OpenEFB 1\n"
               << geometry.left << ' ' << geometry.top << ' ' << geometry.right << ' ' << geometry.bottom << '\n';
    } catch (...) {
    }
}

std::filesystem::path XPlanePreferences::map_cache_directory() const {
    return file_path_.parent_path() / "OpenEFB" / "map-cache";
}

} // namespace openefb::xplane
