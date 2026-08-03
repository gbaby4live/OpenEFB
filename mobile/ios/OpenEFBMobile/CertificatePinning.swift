import CryptoKit
import Foundation
import Security

enum CertificateIdentity {
    static func fingerprint(certificate: SecCertificate) -> Data {
        Data(SHA256.hash(data: SecCertificateCopyData(certificate) as Data))
    }

    static func verificationCode(fingerprint: Data) -> String? {
        guard fingerprint.count >= 4 else { return nil }
        let bytes = [UInt8](fingerprint.prefix(4))
        let value = (UInt32(bytes[0]) << 24) | (UInt32(bytes[1]) << 16) |
            (UInt32(bytes[2]) << 8) | UInt32(bytes[3])
        return String(format: "%06u", value % 1_000_000)
    }
}

struct CertificatePinStore {
    private static let service = "org.openefb.mobile.certificate-pin"

    func pin(for origin: String) -> Data? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: origin,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne
        ]
        var result: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &result) == errSecSuccess else { return nil }
        return result as? Data
    }

    @discardableResult
    func save(_ pin: Data, for origin: String) -> Bool {
        let identity: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: Self.service,
            kSecAttrAccount as String: origin
        ]
        let values: [String: Any] = [
            kSecValueData as String: pin,
            kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
        ]
        let update = SecItemUpdate(identity as CFDictionary, values as CFDictionary)
        if update == errSecSuccess { return true }
        guard update == errSecItemNotFound else { return false }
        return SecItemAdd(identity.merging(values) { _, new in new } as CFDictionary, nil) == errSecSuccess
    }
}
