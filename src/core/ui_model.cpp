#include "openefb/core/ui_model.hpp"

namespace openefb {

const std::array<NavigationItem, 7>& navigation_items() noexcept {
    static constexpr std::array items{
        NavigationItem{EfbPage::home, "Home"},
        NavigationItem{EfbPage::flight_plan, "Flight Plan"},
        NavigationItem{EfbPage::progress, "Progress"},
        NavigationItem{EfbPage::weather, "Weather"},
        NavigationItem{EfbPage::aircraft, "Aircraft"},
        NavigationItem{EfbPage::settings, "Settings"},
        NavigationItem{EfbPage::about, "About"},
    };
    return items;
}

EfbPage UiModel::active_page() const noexcept { return active_page_; }

std::string_view UiModel::active_page_title() const noexcept {
    for (const auto& item : navigation_items()) {
        if (item.page == active_page_) {
            return item.label;
        }
    }
    return "OpenEFB";
}

void UiModel::select_page(EfbPage page) noexcept { active_page_ = page; }

bool UiModel::select_at(int local_x, int local_y) noexcept {
    constexpr int horizontal_padding = 12;
    if (local_x < horizontal_padding || local_x >= sidebar_width - horizontal_padding) {
        return false;
    }

    const auto& items = navigation_items();
    for (std::size_t index = 0; index < items.size(); ++index) {
        const int top = navigation_top + static_cast<int>(index) * (navigation_item_height + navigation_item_gap);
        if (local_y >= top && local_y < top + navigation_item_height) {
            active_page_ = items[index].page;
            return true;
        }
    }
    return false;
}

} // namespace openefb
