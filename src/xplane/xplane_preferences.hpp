#pragma once

#include "openefb/core/window_geometry.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace openefb::xplane {

struct DisplayPreferences {
    bool high_contrast{false};
    bool comfort_size{false};
    bool inject_traffic{false};
    int traffic_range_nm{100};
    bool show_3d_traffic{true};
    bool show_map_aircraft_info{true};
    bool show_map_route{true};
    bool show_map_labels{true};
    int map_marker_scale{100};
    int route_line_width{7};
};

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
    [[nodiscard]] DisplayPreferences load_display_preferences() const;
    void save_display_preferences(const DisplayPreferences& preferences) const;
    [[nodiscard]] std::filesystem::path flight_plan_directory() const;
    [[nodiscard]] std::filesystem::path flight_log_file() const;

private:
    std::filesystem::path file_path_;
    std::filesystem::path notes_path_;
    std::filesystem::path display_path_;
    std::filesystem::path xplane_root_;
};

} // namespace openefb::xplane
