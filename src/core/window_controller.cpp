#include "openefb/core/window_controller.hpp"

#include <utility>

namespace openefb {

WindowController::WindowController(WindowFactory factory) : factory_(std::move(factory)) {}

bool WindowController::toggle() {
    if (!window_) {
        window_ = factory_ ? factory_() : nullptr;
        if (!window_) {
            return false;
        }
    }

    if (window_->visible()) {
        window_->hide();
    } else {
        window_->show();
    }
    return true;
}

void WindowController::hide() {
    if (window_ && window_->visible()) {
        window_->hide();
    }
}

void WindowController::reset() { window_.reset(); }

bool WindowController::created() const noexcept { return window_ != nullptr; }

bool WindowController::visible() const { return window_ && window_->visible(); }

} // namespace openefb
