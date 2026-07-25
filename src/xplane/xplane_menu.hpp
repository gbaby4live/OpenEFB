#pragma once

#include <XPLMMenus.h>

#include <functional>

namespace openefb::xplane {

class XPlaneMenu final {
public:
    explicit XPlaneMenu(std::function<void()> toggle_handler);
    ~XPlaneMenu();

    XPlaneMenu(const XPlaneMenu&) = delete;
    XPlaneMenu& operator=(const XPlaneMenu&) = delete;

    [[nodiscard]] bool valid() const noexcept;

private:
    static void handle_menu(void* menu_ref, void* item_ref);

    std::function<void()> toggle_handler_;
    XPLMMenuID plugins_menu_{nullptr};
    XPLMMenuID menu_{nullptr};
    int parent_item_{-1};
    int toggle_item_{-1};
};

} // namespace openefb::xplane
