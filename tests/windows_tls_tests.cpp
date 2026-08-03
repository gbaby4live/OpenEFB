#include "windows_tls.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <filesystem>
#include <string>
#include <thread>

namespace {

bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAILED: " << message << '\n';
    return condition;
}

} // namespace

int main() {
    WSADATA winsock{};
    if (!require(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "Winsock starts")) return 1;

    openefb::xplane::WindowsTlsContext tls;
    const auto certificate_path = std::filesystem::temp_directory_path() /
                                  ("openefb-tls-test-" + std::to_string(GetCurrentProcessId()) + ".cer");
    if (!tls.initialize(certificate_path, false)) {
        std::cout << "SKIPPED: Windows user key storage is unavailable in this environment\n";
        WSACleanup();
        return 77;
    }
    if (!require(tls.fingerprint().size() == 64, "TLS identity has a SHA-256 fingerprint") ||
        !require(tls.verification_code().size() == 6 &&
                     std::all_of(tls.verification_code().begin(), tls.verification_code().end(),
                                 [](unsigned char value) { return std::isdigit(value) != 0; }),
                 "TLS identity has a six-digit verification code")) {
        WSACleanup();
        return 1;
    }

    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (!require(listener != INVALID_SOCKET &&
                     bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
                     listen(listener, 1) == 0,
                 "loopback TLS listener starts")) {
        if (listener != INVALID_SOCKET) closesocket(listener);
        WSACleanup();
        return 1;
    }
    int address_size = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_size);
    bool server_ok{};
    std::thread server([&] {
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        auto connection = tls.accept(static_cast<std::uintptr_t>(client));
        std::string request;
        server_ok = connection && connection->receive(request) && request.starts_with("GET /secure ") &&
                    connection->send("HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\nsecure");
        shutdown(client, SD_BOTH);
        closesocket(client);
    });

    HINTERNET session = WinHttpOpen(L"OpenEFB TLS test", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = session ? WinHttpConnect(session, L"127.0.0.1", ntohs(address.sin_port), 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/secure", nullptr,
                                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                        WINHTTP_FLAG_SECURE) : nullptr;
    DWORD security = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    bool client_ok = request &&
        WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &security, sizeof(security)) &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr);
    std::string body;
    if (client_ok) {
        std::array<char, 32> buffer{};
        DWORD received{};
        while (WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &received) && received)
            body.append(buffer.data(), received);
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    server.join();
    closesocket(listener);
    WSACleanup();
    std::error_code cleanup_error;
    std::filesystem::remove(certificate_path, cleanup_error);

    return require(client_ok && server_ok && body == "secure",
                   "Schannel serves an encrypted HTTP request and response") ? 0 : 1;
}
