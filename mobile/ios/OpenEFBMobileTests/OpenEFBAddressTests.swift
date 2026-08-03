import XCTest
@testable import OpenEFBMobile

final class OpenEFBAddressTests: XCTestCase {
    func testCertificateVerificationCodeUsesLeadingHashBytes() {
        let fingerprint = Data([0x12, 0x34, 0x56, 0x78] + Array(repeating: 0, count: 28))
        XCTAssertEqual(CertificateIdentity.verificationCode(fingerprint: fingerprint), "419896")
    }

    func testAddsLocalHTTPSAndDefaultPort() throws {
        let url = try OpenEFBAddress.normalize("192.168.1.25").get()
        XCTAssertEqual(url.absoluteString, "https://192.168.1.25:8383/")
    }

    func testPreservesPrivateHTTPSAndPort() throws {
        let url = try OpenEFBAddress.normalize("https://10.2.3.4:9443/mobile?old=1#section").get()
        XCTAssertEqual(url.absoluteString, "https://10.2.3.4:9443/mobile")
    }

    func testAcceptsPrivateIPv4Ranges() {
        ["10.0.0.1", "172.16.0.1", "172.31.255.254", "192.168.0.2", "169.254.10.1", "127.0.0.1"]
            .forEach { XCTAssertTrue(OpenEFBAddress.isPrivateHost($0), $0) }
    }

    func testRejectsPublicAndMalformedHosts() {
        ["8.8.8.8", "example.com", "fcommunity.com", "172.32.0.1", "192.167.1.2", "999.1.1.1"]
            .forEach { XCTAssertFalse(OpenEFBAddress.isPrivateHost($0), $0) }
    }

    func testAcceptsLocalNamesAndPrivateIPv6() {
        ["localhost", "openefb.local", "::1", "fe80::1234", "fd00::10"]
            .forEach { XCTAssertTrue(OpenEFBAddress.isPrivateHost($0), $0) }
    }

    func testRejectsUnsupportedScheme() {
        XCTAssertEqual(OpenEFBAddress.normalize("ftp://192.168.1.3").failure,
                       .unsupportedScheme)
    }

    func testRejectsUnencryptedHTTP() {
        XCTAssertEqual(OpenEFBAddress.normalize("http://192.168.1.3").failure,
                       .insecureScheme)
    }

    func testRejectsCredentials() {
        XCTAssertEqual(OpenEFBAddress.normalize("http://pilot:secret@192.168.1.3").failure,
                       .invalid)
    }
}

private extension Result {
    var failure: Failure? {
        if case .failure(let error) = self { return error }
        return nil
    }
}
