#pragma once

#include <functional>
#include <memory>

namespace openefb {

class WindowSurface {
public:
    virtual ~WindowSurface() = default;

    virtual void show() = 0;
    virtual void hide() = 0;
    [[nodiscard]] virtual bool visible() const = 0;
};

using WindowFactory = std::function<std::unique_ptr<WindowSurface>()>;

class WindowController final {
public:
    explicit WindowController(WindowFactory factory);

    bool toggle();
    void hide();
    void reset();

    [[nodiscard]] bool created() const noexcept;
    [[nodiscard]] bool visible() const;

private:
    WindowFactory factory_;
    std::unique_ptr<WindowSurface> window_;
};

} // namespace openefb
