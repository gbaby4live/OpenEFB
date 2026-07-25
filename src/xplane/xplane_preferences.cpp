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
    notes_path_ = file_path_.parent_path() / "OpenEFB" / "briefing-notes.txt";
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
