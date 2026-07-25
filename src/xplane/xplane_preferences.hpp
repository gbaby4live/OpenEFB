#pragma once

#include "openefb/core/window_geometry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace openefb::xplane {

class XPlanePreferences final {
public:
    XPlanePreferences();

    [[nodiscard]] std::optional<WindowGeometry> load_geometry() const;
    void save_geometry(const WindowGeometry& geometry) const;
    [[nodiscard]] std::filesystem::path map_cache_directory() const;
    [[nodiscard]] std::filesystem::path weather_cache_directory() const;
    [[nodiscard]] std::filesystem::path briefing_library_directory() const;
    [[nodiscard]] std::string load_briefing_notes() const;
    void save_briefing_notes(std::string_view notes) const;

private:
    std::filesystem::path file_path_;
    std::filesystem::path notes_path_;
};

} // namespace openefb::xplane
