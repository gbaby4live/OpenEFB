import Combine
import Foundation
import Network

@MainActor
final class ConnectionModel: ObservableObject {
    @Published var address: String
    @Published private(set) var connectedURL: URL?
    @Published private(set) var error = ""
    @Published private(set) var networkAvailable = true
    @Published private(set) var reloadGeneration = 0

    private let defaults: UserDefaults
    private let monitor = NWPathMonitor()
    private let monitorQueue = DispatchQueue(label: "org.openefb.mobile.network")
    private static let addressKey = "openefb.server"

    init(defaults: UserDefaults = .standard, monitorNetwork: Bool = true) {
        self.defaults = defaults
        address = defaults.string(forKey: Self.addressKey) ?? ""
        if monitorNetwork {
            monitor.pathUpdateHandler = { [weak self] path in
                Task { @MainActor in self?.networkAvailable = path.status == .satisfied }
            }
            monitor.start(queue: monitorQueue)
        }
    }

    deinit { monitor.cancel() }

    func connect() {
        switch OpenEFBAddress.normalize(address) {
        case .success(let url):
            address = url.absoluteString
            defaults.set(address, forKey: Self.addressKey)
            error = ""
            connectedURL = url
        case .failure(let validationError):
            error = validationError.localizedDescription
        }
    }

    func disconnect() {
        connectedURL = nil
        error = ""
    }

    func resume() {
        guard connectedURL != nil else { return }
        reloadGeneration &+= 1
    }

    func reportWebFailure(_ message: String) {
        error = message
    }

    func reportWebReady() {
        error = ""
    }
}
