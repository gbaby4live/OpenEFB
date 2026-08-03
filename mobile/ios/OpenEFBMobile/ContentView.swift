import SwiftUI
import WebKit

struct ContentView: View {
    @AppStorage("openefb.server") private var savedAddress = ""
    @State private var address = ""
    @State private var connectedURL: URL?
    @State private var error = ""

    var body: some View {
        Group {
            if let connectedURL {
                ZStack(alignment: .topTrailing) {
                    OpenEFBWebView(url: connectedURL)
                        .ignoresSafeArea(.container, edges: .bottom)
                    Button {
                        self.connectedURL = nil
                    } label: {
                        Image(systemName: "rectangle.portrait.and.arrow.right")
                            .font(.system(size: 15, weight: .bold))
                            .padding(10)
                            .background(.black.opacity(0.72), in: Circle())
                            .foregroundStyle(.white)
                    }
                    .padding(12)
                    .accessibilityLabel("Change OpenEFB computer")
                }
            } else {
                connectionView
            }
        }
        .preferredColorScheme(.dark)
        .onAppear {
            address = savedAddress
            if !savedAddress.isEmpty { connect() }
        }
    }

    private var connectionView: some View {
        ZStack {
            LinearGradient(colors: [Color(red: 0.03, green: 0.10, blue: 0.15), Color(red: 0.02, green: 0.04, blue: 0.07)], startPoint: .topLeading, endPoint: .bottomTrailing)
                .ignoresSafeArea()
            VStack(alignment: .leading, spacing: 18) {
                Text("OE")
                    .font(.system(size: 22, weight: .black))
                    .foregroundStyle(Color(red: 0.01, green: 0.08, blue: 0.12))
                    .frame(width: 54, height: 54)
                    .background(LinearGradient(colors: [.cyan, .blue], startPoint: .topLeading, endPoint: .bottomTrailing), in: RoundedRectangle(cornerRadius: 15))
                Text("OPEN EFB MOBILE").font(.caption.bold()).tracking(2).foregroundStyle(.cyan)
                Text("Connect to your flight deck").font(.largeTitle.bold())
                Text("Enter the complete address shown on the OpenEFB About page while this iPhone and the X-Plane computer are on the same Wi-Fi network.")
                    .foregroundStyle(.secondary)
                TextField("http://192.168.1.10:8383", text: $address)
                    .textInputAutocapitalization(.never)
                    .keyboardType(.URL)
                    .autocorrectionDisabled()
                    .padding(14)
                    .background(Color.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 12))
                Button("Open Flight Deck") { connect() }
                    .frame(maxWidth: .infinity)
                    .padding(14)
                    .background(LinearGradient(colors: [.cyan, .blue], startPoint: .leading, endPoint: .trailing), in: RoundedRectangle(cornerRadius: 12))
                    .foregroundStyle(Color(red: 0.01, green: 0.07, blue: 0.10))
                    .fontWeight(.black)
                if !error.isEmpty { Text(error).font(.footnote).foregroundStyle(.red) }
            }
            .padding(26)
        }
    }

    private func connect() {
        var value = address.trimmingCharacters(in: .whitespacesAndNewlines)
        if !value.contains("://") { value = "http://" + value }
        guard let url = URL(string: value), url.host != nil else {
            error = "Enter the complete OpenEFB address shown in X-Plane."
            return
        }
        savedAddress = value
        error = ""
        connectedURL = url
    }
}

struct OpenEFBWebView: UIViewRepresentable {
    let url: URL

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .default()
        configuration.allowsInlineMediaPlayback = true
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.allowsBackForwardNavigationGestures = true
        view.scrollView.contentInsetAdjustmentBehavior = .never
        view.load(URLRequest(url: url, cachePolicy: .reloadRevalidatingCacheData))
        return view
    }

    func updateUIView(_ view: WKWebView, context: Context) {
        if view.url?.host != url.host || view.url?.port != url.port {
            view.load(URLRequest(url: url, cachePolicy: .reloadRevalidatingCacheData))
        }
    }
}
