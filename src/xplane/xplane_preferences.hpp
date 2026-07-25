#pragma once

#include "openefb/core/window_geometry.hpp"

#include <filesystem>
#include <optional>

namespace openefb::xplane {

class XPlanePreferences final {
public:
    XPlanePreferences();

    [[nodiscard]] std::optional<WindowGeometry> load_geometry() const;
    void save_geometry(const WindowGeometry& geometry) const;
    [[nodiscard]] std::filesystem::path map_cache_directory() const;

private:
    std::filesystem::path file_path_;
};

} // namespace openefb::xplane
