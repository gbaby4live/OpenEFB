#pragma once

#include <array>
#include <string_view>

namespace openefb {

enum class EfbPage {
    home,
    flight_plan,
    airports,
    progress,
    weather,
    planning,
    aircraft,
    settings,
    about,
};

struct NavigationItem {
    EfbPage page;
    std::string_view label;
};

inline constexpr int status_bar_height = 56;
inline constexpr int sidebar_width = 168;
inline constexpr int navigation_top = 84;
inline constexpr int navigation_item_height = 44;
inline constexpr int navigation_item_gap = 8;

[[nodiscard]] const std::array<NavigationItem, 9>& navigation_items() noexcept;

class UiModel final {
public:
    [[nodiscard]] EfbPage active_page() const noexcept;
    [[nodiscard]] std::string_view active_page_title() const noexcept;

    void select_page(EfbPage page) noexcept;
    bool select_at(int local_x, int local_y) noexcept;

private:
    EfbPage active_page_{EfbPage::home};
};

} // namespace openefb
