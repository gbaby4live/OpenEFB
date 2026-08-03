#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace openefb::xplane {

class WindowsTlsConnection {
public:
    ~WindowsTlsConnection();
    WindowsTlsConnection(WindowsTlsConnection&&) noexcept;
    WindowsTlsConnection& operator=(WindowsTlsConnection&&) noexcept;

    WindowsTlsConnection(const WindowsTlsConnection&) = delete;
    WindowsTlsConnection& operator=(const WindowsTlsConnection&) = delete;

    [[nodiscard]] bool receive(std::string& plaintext);
    [[nodiscard]] bool send(std::string_view plaintext);

private:
    struct Impl;
    explicit WindowsTlsConnection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
    friend class WindowsTlsContext;
};

class WindowsTlsContext {
public:
    WindowsTlsContext();
    ~WindowsTlsContext();
    WindowsTlsContext(WindowsTlsContext&&) noexcept;
    WindowsTlsContext& operator=(WindowsTlsContext&&) noexcept;

    WindowsTlsContext(const WindowsTlsContext&) = delete;
    WindowsTlsContext& operator=(const WindowsTlsContext&) = delete;

    [[nodiscard]] bool initialize(const std::filesystem::path& certificate_path,
                                  bool require_persistence = true);
    void reset();
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::string_view fingerprint() const noexcept;
    [[nodiscard]] std::string_view verification_code() const noexcept;
    [[nodiscard]] std::unique_ptr<WindowsTlsConnection> accept(std::uintptr_t socket) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace openefb::xplane
