#include "windows_tls.hpp"

#if IBM
#include <winsock2.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>
#include <wincrypt.h>
#include <ncrypt.h>
#endif

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <utility>
#include <vector>

namespace openefb::xplane {

#if IBM
namespace {

constexpr wchar_t identity_subject[] = L"CN=OpenEFB Local Bridge";
constexpr wchar_t identity_container[] = L"OpenEFB Mobile TLS Identity";

bool socket_send_all(SOCKET socket, const char* data, std::size_t size) {
    std::size_t sent{};
    while (sent < size) {
        const auto remaining = std::min<std::size_t>(size - sent, 1U << 20U);
        const int count = ::send(socket, data + sent, static_cast<int>(remaining), 0);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

CRYPT_KEY_PROV_INFO identity_key_info() {
    CRYPT_KEY_PROV_INFO info{};
    info.pwszContainerName = const_cast<wchar_t*>(identity_container);
    info.pwszProvName = const_cast<wchar_t*>(MS_KEY_STORAGE_PROVIDER);
    info.dwKeySpec = CERT_NCRYPT_KEY_SPEC;
    return info;
}

PCCERT_CONTEXT load_identity(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return nullptr;
    const std::vector<unsigned char> encoded{std::istreambuf_iterator<char>(stream),
                                             std::istreambuf_iterator<char>()};
    if (encoded.empty()) return nullptr;
    PCCERT_CONTEXT certificate = CertCreateCertificateContext(
        X509_ASN_ENCODING, encoded.data(), static_cast<DWORD>(encoded.size()));
    auto key_info = identity_key_info();
    if (certificate && !CertSetCertificateContextProperty(
                           certificate, CERT_KEY_PROV_INFO_PROP_ID, 0, &key_info)) {
        CertFreeCertificateContext(certificate);
        certificate = nullptr;
    }
    return certificate;
}

bool save_identity(const std::filesystem::path& path, PCCERT_CONTEXT certificate) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (stream) stream.write(reinterpret_cast<const char*>(certificate->pbCertEncoded),
                             certificate->cbCertEncoded);
    return static_cast<bool>(stream);
}

PCCERT_CONTEXT create_identity() {
    NCRYPT_PROV_HANDLE provider{};
    if (const auto result = NCryptOpenStorageProvider(&provider, MS_KEY_STORAGE_PROVIDER, 0);
        result != ERROR_SUCCESS) {
        std::fprintf(stderr, "OpenEFB TLS: NCryptOpenStorageProvider failed 0x%lx\n", result);
        return nullptr;
    }

    NCRYPT_KEY_HANDLE key{};
    SECURITY_STATUS status = NCryptOpenKey(provider, &key, identity_container, 0, NCRYPT_SILENT_FLAG);
    const bool create_key = status != ERROR_SUCCESS;
    if (create_key)
        status = NCryptCreatePersistedKey(provider, &key, NCRYPT_RSA_ALGORITHM,
                                          identity_container, 0, NCRYPT_SILENT_FLAG);
    DWORD bits = 2048;
    if (status == ERROR_SUCCESS && create_key)
        status = NCryptSetProperty(key, NCRYPT_LENGTH_PROPERTY,
                                  reinterpret_cast<PBYTE>(&bits), sizeof(bits), 0);
    if (status == ERROR_SUCCESS && create_key) status = NCryptFinalizeKey(key, 0);
    if (status != ERROR_SUCCESS) {
        if (key) NCryptFreeObject(key);
        NCryptFreeObject(provider);
        return nullptr;
    }

    DWORD subject_size{};
    CertStrToNameW(X509_ASN_ENCODING, identity_subject, CERT_X500_NAME_STR,
                   nullptr, nullptr, &subject_size, nullptr);
    std::vector<BYTE> subject(subject_size);
    if (!CertStrToNameW(X509_ASN_ENCODING, identity_subject, CERT_X500_NAME_STR,
                        nullptr, subject.data(), &subject_size, nullptr)) {
        std::fprintf(stderr, "OpenEFB TLS: CertStrToName failed %lu\n", GetLastError());
        NCryptFreeObject(key);
        NCryptFreeObject(provider);
        return nullptr;
    }

    CERT_NAME_BLOB name{subject_size, subject.data()};
    auto key_info = identity_key_info();
    CRYPT_ALGORITHM_IDENTIFIER signature{};
    signature.pszObjId = const_cast<char*>(szOID_RSA_SHA256RSA);
    SYSTEMTIME start{};
    GetSystemTime(&start);
    SYSTEMTIME end = start;
    end.wYear = static_cast<WORD>(std::min<int>(start.wYear + 10, 9999));
    if (end.wMonth == 2 && end.wDay == 29) end.wDay = 28;

    PCCERT_CONTEXT created = CertCreateSelfSignCertificate(
        key, &name, 0, &key_info, &signature, &start, &end, nullptr);
    if (!created) std::fprintf(stderr, "OpenEFB TLS: CertCreateSelfSignCertificate failed %lu\n", GetLastError());
    NCryptFreeObject(key);
    NCryptFreeObject(provider);
    return created;
}

std::vector<unsigned char> certificate_hash(PCCERT_CONTEXT certificate) {
    DWORD size{};
    if (!CertGetCertificateContextProperty(certificate, CERT_SHA256_HASH_PROP_ID, nullptr, &size)) return {};
    std::vector<unsigned char> hash(size);
    if (!CertGetCertificateContextProperty(certificate, CERT_SHA256_HASH_PROP_ID, hash.data(), &size)) return {};
    hash.resize(size);
    return hash;
}

bool certificate_has_private_key(PCCERT_CONTEXT certificate) {
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE private_key{};
    DWORD key_spec{};
    BOOL release_key{};
    const bool available = CryptAcquireCertificatePrivateKey(
        certificate, CRYPT_ACQUIRE_ONLY_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG |
                         CRYPT_ACQUIRE_SILENT_FLAG,
        nullptr, &private_key, &key_spec, &release_key) != FALSE;
    if (available && release_key) NCryptFreeObject(private_key);
    return available;
}

std::string hex_string(const std::vector<unsigned char>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::string make_verification_code(const std::vector<unsigned char>& hash) {
    if (hash.size() < 4) return {};
    const std::uint32_t value = (static_cast<std::uint32_t>(hash[0]) << 24U) |
                                (static_cast<std::uint32_t>(hash[1]) << 16U) |
                                (static_cast<std::uint32_t>(hash[2]) << 8U) |
                                static_cast<std::uint32_t>(hash[3]);
    std::array<char, 7> code{};
    std::snprintf(code.data(), code.size(), "%06u", value % 1000000U);
    return code.data();
}

} // namespace

struct WindowsTlsContext::Impl {
    CredHandle credentials{};
    PCCERT_CONTEXT certificate{};
    bool credential_valid{};
    std::string fingerprint;
    std::string code;

    ~Impl() {
        if (credential_valid) FreeCredentialsHandle(&credentials);
        if (certificate) CertFreeCertificateContext(certificate);
    }
};

struct WindowsTlsConnection::Impl {
    SOCKET socket{INVALID_SOCKET};
    CtxtHandle context{};
    bool context_valid{};
    SecPkgContext_StreamSizes sizes{};
    std::vector<char> encrypted;

    ~Impl() {
        if (context_valid) DeleteSecurityContext(&context);
    }

    bool handshake(const CredHandle& credentials) {
        encrypted.reserve(65536);
        bool first = true;
        for (;;) {
            if (encrypted.empty()) {
                std::array<char, 16384> incoming{};
                const int count = recv(socket, incoming.data(), static_cast<int>(incoming.size()), 0);
                if (count <= 0) return false;
                encrypted.insert(encrypted.end(), incoming.data(), incoming.data() + count);
            }

            SecBuffer input_buffers[2]{{static_cast<unsigned long>(encrypted.size()), SECBUFFER_TOKEN,
                                        encrypted.data()},
                                       {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc input{SECBUFFER_VERSION, 2, input_buffers};
            SecBuffer output_buffer{0, SECBUFFER_TOKEN, nullptr};
            SecBufferDesc output{SECBUFFER_VERSION, 1, &output_buffer};
            ULONG attributes{};
            TimeStamp expiry{};
            SECURITY_STATUS status = AcceptSecurityContext(
                const_cast<CredHandle*>(&credentials), first ? nullptr : &context, &input,
                ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                    ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
                SECURITY_NATIVE_DREP, &context, &output, &attributes, &expiry);
            if (first && (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED ||
                          status == SEC_I_COMPLETE_NEEDED || status == SEC_I_COMPLETE_AND_CONTINUE)) {
                context_valid = true;
                first = false;
            }
            if (status == SEC_I_COMPLETE_NEEDED || status == SEC_I_COMPLETE_AND_CONTINUE)
                CompleteAuthToken(&context, &output);
            if (output_buffer.pvBuffer && output_buffer.cbBuffer) {
                const bool sent = socket_send_all(socket, static_cast<const char*>(output_buffer.pvBuffer),
                                                  output_buffer.cbBuffer);
                FreeContextBuffer(output_buffer.pvBuffer);
                if (!sent) return false;
            }
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                std::array<char, 16384> incoming{};
                const int count = recv(socket, incoming.data(), static_cast<int>(incoming.size()), 0);
                if (count <= 0 || encrypted.size() + static_cast<std::size_t>(count) > 131072) return false;
                encrypted.insert(encrypted.end(), incoming.data(), incoming.data() + count);
                continue;
            }
            if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED &&
                status != SEC_I_COMPLETE_NEEDED && status != SEC_I_COMPLETE_AND_CONTINUE) return false;

            if (input_buffers[1].BufferType == SECBUFFER_EXTRA) {
                const auto extra = input_buffers[1].cbBuffer;
                std::vector<char> remaining(encrypted.end() - extra, encrypted.end());
                encrypted.swap(remaining);
            } else encrypted.clear();

            if (status == SEC_E_OK || status == SEC_I_COMPLETE_NEEDED)
                return QueryContextAttributes(&context, SECPKG_ATTR_STREAM_SIZES, &sizes) == SEC_E_OK;
        }
    }
};

#else

struct WindowsTlsContext::Impl {};
struct WindowsTlsConnection::Impl {};

#endif

WindowsTlsContext::WindowsTlsContext() : impl_(std::make_unique<Impl>()) {}
WindowsTlsContext::~WindowsTlsContext() = default;
WindowsTlsContext::WindowsTlsContext(WindowsTlsContext&&) noexcept = default;
WindowsTlsContext& WindowsTlsContext::operator=(WindowsTlsContext&&) noexcept = default;

bool WindowsTlsContext::initialize(const std::filesystem::path& certificate_path,
                                   bool require_persistence) {
#if IBM
    if (impl_->credential_valid) return true;
    std::filesystem::path resolved_path = certificate_path;
    if (resolved_path.empty()) {
        std::array<wchar_t, 32768> local_data{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data.data(),
                                                      static_cast<DWORD>(local_data.size()));
        if (!length || length >= local_data.size()) return false;
        resolved_path = std::filesystem::path(local_data.data()) / "OpenEFB" / "mobile-identity.cer";
    }
    impl_->certificate = load_identity(resolved_path);
    if (impl_->certificate && !certificate_has_private_key(impl_->certificate)) {
        CertFreeCertificateContext(impl_->certificate);
        impl_->certificate = nullptr;
    }
    if (!impl_->certificate) {
        impl_->certificate = create_identity();
        if (impl_->certificate && !save_identity(resolved_path, impl_->certificate) &&
            require_persistence) {
            CertFreeCertificateContext(impl_->certificate);
            impl_->certificate = nullptr;
        }
    }
    if (!impl_->certificate) {
        std::fprintf(stderr, "OpenEFB TLS: certificate unavailable %lu\n", GetLastError());
        return false;
    }

    SCHANNEL_CRED schannel{};
    schannel.dwVersion = SCHANNEL_CRED_VERSION;
    schannel.cCreds = 1;
    schannel.paCred = &impl_->certificate;
    schannel.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry{};
    if (!certificate_has_private_key(impl_->certificate)) {
        std::fprintf(stderr, "OpenEFB TLS: certificate private key unavailable %lu\n", GetLastError());
        return false;
    }
    const auto status = AcquireCredentialsHandleW(nullptr, const_cast<wchar_t*>(UNISP_NAME_W),
                                                   SECPKG_CRED_INBOUND, nullptr, &schannel,
                                                   nullptr, nullptr, &impl_->credentials, &expiry);
    if (status != SEC_E_OK) {
        std::fprintf(stderr, "OpenEFB TLS: AcquireCredentialsHandle failed 0x%lx\n", status);
        return false;
    }
    const auto hash = certificate_hash(impl_->certificate);
    if (hash.empty()) std::fprintf(stderr, "OpenEFB TLS: SHA-256 certificate hash unavailable %lu\n", GetLastError());
    impl_->fingerprint = hex_string(hash);
    impl_->code = make_verification_code(hash);
    impl_->credential_valid = !impl_->fingerprint.empty() && !impl_->code.empty();
    if (!impl_->credential_valid) FreeCredentialsHandle(&impl_->credentials);
    return impl_->credential_valid;
#else
    (void)certificate_path;
    (void)require_persistence;
    return false;
#endif
}

void WindowsTlsContext::reset() { impl_ = std::make_unique<Impl>(); }
bool WindowsTlsContext::ready() const noexcept {
#if IBM
    return impl_->credential_valid;
#else
    return false;
#endif
}
std::string_view WindowsTlsContext::fingerprint() const noexcept {
#if IBM
    return impl_->fingerprint;
#else
    return {};
#endif
}
std::string_view WindowsTlsContext::verification_code() const noexcept {
#if IBM
    return impl_->code;
#else
    return {};
#endif
}

std::unique_ptr<WindowsTlsConnection> WindowsTlsContext::accept(std::uintptr_t socket) const {
#if IBM
    if (!ready()) return {};
    auto connection = std::make_unique<WindowsTlsConnection::Impl>();
    connection->socket = static_cast<SOCKET>(socket);
    if (!connection->handshake(impl_->credentials)) return {};
    return std::unique_ptr<WindowsTlsConnection>(new WindowsTlsConnection(std::move(connection)));
#else
    (void)socket;
    return {};
#endif
}

WindowsTlsConnection::WindowsTlsConnection(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
WindowsTlsConnection::~WindowsTlsConnection() = default;
WindowsTlsConnection::WindowsTlsConnection(WindowsTlsConnection&&) noexcept = default;
WindowsTlsConnection& WindowsTlsConnection::operator=(WindowsTlsConnection&&) noexcept = default;

bool WindowsTlsConnection::receive(std::string& plaintext) {
    plaintext.clear();
#if IBM
    for (;;) {
        if (impl_->encrypted.empty()) {
            std::array<char, 16384> incoming{};
            const int count = recv(impl_->socket, incoming.data(), static_cast<int>(incoming.size()), 0);
            if (count <= 0) return false;
            impl_->encrypted.insert(impl_->encrypted.end(), incoming.data(), incoming.data() + count);
        }
        SecBuffer buffers[4]{{static_cast<unsigned long>(impl_->encrypted.size()), SECBUFFER_DATA,
                              impl_->encrypted.data()},
                             {0, SECBUFFER_EMPTY, nullptr}, {0, SECBUFFER_EMPTY, nullptr},
                             {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
        const auto status = DecryptMessage(&impl_->context, &message, 0, nullptr);
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            std::array<char, 16384> incoming{};
            const int count = recv(impl_->socket, incoming.data(), static_cast<int>(incoming.size()), 0);
            if (count <= 0 || impl_->encrypted.size() + static_cast<std::size_t>(count) > 131072) return false;
            impl_->encrypted.insert(impl_->encrypted.end(), incoming.data(), incoming.data() + count);
            continue;
        }
        if (status != SEC_E_OK) return false;
        std::vector<char> extra;
        for (const auto& buffer : buffers) {
            if (buffer.BufferType == SECBUFFER_DATA && buffer.cbBuffer)
                plaintext.append(static_cast<const char*>(buffer.pvBuffer), buffer.cbBuffer);
            else if (buffer.BufferType == SECBUFFER_EXTRA && buffer.cbBuffer) {
                const auto* begin = static_cast<const char*>(buffer.pvBuffer);
                extra.assign(begin, begin + buffer.cbBuffer);
            }
        }
        impl_->encrypted.swap(extra);
        if (!plaintext.empty()) return true;
    }
#else
    return false;
#endif
}

bool WindowsTlsConnection::send(std::string_view plaintext) {
#if IBM
    std::size_t offset{};
    while (offset < plaintext.size()) {
        const auto chunk = std::min<std::size_t>(plaintext.size() - offset, impl_->sizes.cbMaximumMessage);
        std::vector<char> record(impl_->sizes.cbHeader + chunk + impl_->sizes.cbTrailer);
        std::copy_n(plaintext.data() + offset, chunk, record.data() + impl_->sizes.cbHeader);
        SecBuffer buffers[4]{{impl_->sizes.cbHeader, SECBUFFER_STREAM_HEADER, record.data()},
                             {static_cast<unsigned long>(chunk), SECBUFFER_DATA,
                              record.data() + impl_->sizes.cbHeader},
                             {impl_->sizes.cbTrailer, SECBUFFER_STREAM_TRAILER,
                              record.data() + impl_->sizes.cbHeader + chunk},
                             {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc message{SECBUFFER_VERSION, 4, buffers};
        if (EncryptMessage(&impl_->context, 0, &message, 0) != SEC_E_OK) return false;
        const auto encrypted_size = static_cast<std::size_t>(buffers[0].cbBuffer) +
                                    buffers[1].cbBuffer + buffers[2].cbBuffer;
        if (!socket_send_all(impl_->socket, record.data(), encrypted_size)) return false;
        offset += chunk;
    }
    return true;
#else
    (void)plaintext;
    return false;
#endif
}

} // namespace openefb::xplane
