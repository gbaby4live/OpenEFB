#include "xplane_menu.hpp"

#include <utility>

namespace openefb::xplane {

XPlaneMenu::XPlaneMenu(std::function<void()> toggle_handler)
    : toggle_handler_(std::move(toggle_handler)), plugins_menu_(XPLMFindPluginsMenu()) {
    if (!plugins_menu_) {
        return;
    }

    parent_item_ = XPLMAppendMenuItem(plugins_menu_, "OpenEFB", nullptr, 0);
    if (parent_item_ < 0) {
        return;
    }

    menu_ = XPLMCreateMenu("OpenEFB", plugins_menu_, parent_item_, handle_menu, this);
    if (menu_) {
        toggle_item_ = XPLMAppendMenuItem(menu_, "Show / Hide OpenEFB", nullptr, 0);
    }
}

XPlaneMenu::~XPlaneMenu() {
    if (menu_) {
        XPLMDestroyMenu(menu_);
    }
    if (plugins_menu_ && parent_item_ >= 0) {
        XPLMRemoveMenuItem(plugins_menu_, parent_item_);
    }
}

bool XPlaneMenu::valid() const noexcept { return menu_ != nullptr && toggle_item_ >= 0; }

void XPlaneMenu::handle_menu(void* menu_ref, void*) {
    auto* menu = static_cast<XPlaneMenu*>(menu_ref);
    if (menu && menu->toggle_handler_) {
        menu->toggle_handler_();
    }
}

} // namespace openefb::xplane
