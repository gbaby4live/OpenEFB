#include "xplane_preferences.hpp"

#include <XPLMUtilities.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace openefb::xplane {

XPlanePreferences::XPlanePreferences() {
    std::array<char, 512> preferences_path{};
    XPLMGetPrefsPath(preferences_path.data());
    XPLMExtractFileAndPath(preferences_path.data());
    file_path_ = std::filesystem::path(preferences_path.data()) / "OpenEFB.prf";
    notes_path_ = file_path_.parent_path() / "OpenEFB" / "briefing-notes.txt";
    display_path_ = file_path_.parent_path() / "OpenEFB" / "display.prf";
    std::array<char, 512> system_path{};
    XPLMGetSystemPath(system_path.data());
    xplane_root_ = std::filesystem::path(system_path.data());
}

DisplayPreferences XPlanePreferences::load_display_preferences() const {
    DisplayPreferences preferences;
    try {
        std::ifstream input(display_path_);
        std::string signature;
        int version{};
        int high_contrast{};
        int comfort_size{};
        int inject_traffic{};
        int traffic_range_nm{100};
        int legacy_night_mode{};
        if (input >> signature >> version && signature == "OpenEFBDisplay" &&
            ((version == 1 && (input >> high_contrast >> comfort_size)) ||
             (version == 2 && (input >> legacy_night_mode >> high_contrast >> comfort_size)) ||
             (version == 3 && (input >> high_contrast >> comfort_size >> inject_traffic)) ||
             (version == 4 && (input >> high_contrast >> comfort_size >> inject_traffic >>
                                      traffic_range_nm)))) {
            preferences.high_contrast = high_contrast != 0;
            preferences.comfort_size = comfort_size != 0;
            preferences.inject_traffic = inject_traffic != 0;
            preferences.traffic_range_nm = std::clamp(traffic_range_nm, 25, 200);
        }
    } catch (...) {
    }
    return preferences;
}

void XPlanePreferences::save_display_preferences(const DisplayPreferences& preferences) const {
    try {
        std::filesystem::create_directories(display_path_.parent_path());
        std::ofstream output(display_path_, std::ios::trunc);
        output << "OpenEFBDisplay 4\n" << (preferences.high_contrast ? 1 : 0) << ' '
               << (preferences.comfort_size ? 1 : 0) << ' '
               << (preferences.inject_traffic ? 1 : 0) << ' '
               << std::clamp(preferences.traffic_range_nm, 25, 200) << '\n';
    } catch (...) {
    }
}

std::filesystem::path XPlanePreferences::flight_plan_directory() const {
    return xplane_root_ / "Output" / "FMS plans";
}

std::filesystem::path XPlanePreferences::flight_log_file() const {
    return file_path_.parent_path() / "OpenEFB" / "flight-history.tsv";
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

std::filesystem::path XPlanePreferences::weather_cache_directory() const {
    return file_path_.parent_path() / "OpenEFB" / "weather-cache";
}

std::filesystem::path XPlanePreferences::briefing_library_directory() const {
    return file_path_.parent_path() / "OpenEFB" / "Library";
}

std::string XPlanePreferences::load_briefing_notes() const {
    try {
        std::ifstream input(notes_path_, std::ios::binary | std::ios::ate);
        if (!input) return {};
        const auto size = input.tellg();
        if (size <= 0 || size > 2048) return {};
        std::string notes(static_cast<std::size_t>(size), '\0');
        input.seekg(0);
        input.read(notes.data(), size);
        return input ? notes : std::string{};
    } catch (...) {
        return {};
    }
}

void XPlanePreferences::save_briefing_notes(std::string_view notes) const {
    try {
        std::filesystem::create_directories(notes_path_.parent_path());
        std::ofstream output(notes_path_, std::ios::binary | std::ios::trunc);
        output.write(notes.data(), static_cast<std::streamsize>(notes.size()));
    } catch (...) {
    }
}

} // namespace openefb::xplane
