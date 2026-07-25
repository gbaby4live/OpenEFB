#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace openefb::xplane {

class XPlanePdfViewer final {
public:
    XPlanePdfViewer();
    ~XPlanePdfViewer();
    XPlanePdfViewer(const XPlanePdfViewer&) = delete;
    XPlanePdfViewer& operator=(const XPlanePdfViewer&) = delete;

    void open(std::filesystem::path path);
    void close();
    void previous_page();
    void next_page();
    void draw(int left, int top, int right, int bottom);

    [[nodiscard]] bool visible() const noexcept;
    [[nodiscard]] int page_number() const noexcept;
    [[nodiscard]] int page_count() const noexcept;
    [[nodiscard]] std::string title() const;
    [[nodiscard]] std::string status() const;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation_;
};

} // namespace openefb::xplane
