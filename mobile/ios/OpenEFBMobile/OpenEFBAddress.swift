import Foundation

enum OpenEFBAddressError: LocalizedError, Equatable {
    case invalid
    case unsupportedScheme
    case insecureScheme
    case publicHost

    var errorDescription: String? {
        switch self {
        case .invalid: return "Enter the complete OpenEFB address shown in X-Plane."
        case .unsupportedScheme: return "The OpenEFB address must begin with https://."
        case .insecureScheme: return "Use the encrypted https:// address displayed by OpenEFB."
        case .publicHost: return "Use the private-network address displayed by OpenEFB, not a public website."
        }
    }
}

enum OpenEFBAddress {
    static func normalize(_ input: String) -> Result<URL, OpenEFBAddressError> {
        var value = input.trimmingCharacters(in: .whitespacesAndNewlines)
        if !value.contains("://") { value = "https://" + value }
        guard var components = URLComponents(string: value),
              let scheme = components.scheme?.lowercased(),
              let host = components.host?.lowercased(), !host.isEmpty,
              components.user == nil, components.password == nil else {
            return .failure(.invalid)
        }
        guard scheme == "https" else {
            return .failure(scheme == "http" ? .insecureScheme : .unsupportedScheme)
        }
        guard isPrivateHost(host) else { return .failure(.publicHost) }
        components.scheme = scheme
        components.host = host
        components.port = components.port ?? 8383
        components.path = components.path.isEmpty ? "/" : components.path
        components.query = nil
        components.fragment = nil
        guard let url = components.url else { return .failure(.invalid) }
        return .success(url)
    }

    static func isPrivateHost(_ host: String) -> Bool {
        if host == "localhost" || host.hasSuffix(".local") { return true }
        if host.contains(":") &&
            (host == "::1" || host.hasPrefix("fe80:") ||
             host.hasPrefix("fc") || host.hasPrefix("fd")) { return true }
        let octets = host.split(separator: ".").compactMap { Int($0) }
        guard octets.count == 4, octets.allSatisfy({ 0...255 ~= $0 }) else { return false }
        return octets[0] == 10 ||
            (octets[0] == 172 && 16...31 ~= octets[1]) ||
            (octets[0] == 192 && octets[1] == 168) ||
            (octets[0] == 169 && octets[1] == 254) ||
            octets[0] == 127
    }
}
