import SwiftUI

struct ContentView: View {
    @Environment(\.scenePhase) private var scenePhase
    @StateObject private var connection = ConnectionModel()

    var body: some View {
        Group {
            if let connectedURL = connection.connectedURL {
                ZStack(alignment: .topTrailing) {
                    OpenEFBWebView(
                        url: connectedURL,
                        reloadGeneration: connection.reloadGeneration,
                        onReady: connection.reportWebReady,
                        onFailure: connection.reportWebFailure
                    )
                    .ignoresSafeArea(.container, edges: .bottom)

                    VStack(alignment: .trailing, spacing: 8) {
                        Button(action: connection.disconnect) {
                            Image(systemName: "rectangle.portrait.and.arrow.right")
                                .font(.system(size: 15, weight: .bold))
                                .padding(10)
                                .background(.black.opacity(0.74), in: Circle())
                                .foregroundStyle(.white)
                        }
                        .accessibilityLabel("Change OpenEFB computer")
                        if !connection.error.isEmpty {
                            Text(connection.error)
                                .font(.caption.bold())
                                .padding(10)
                                .frame(maxWidth: 310, alignment: .leading)
                                .background(.black.opacity(0.86), in: RoundedRectangle(cornerRadius: 10))
                                .foregroundStyle(.orange)
                        }
                    }
                    .padding(12)
                }
            } else {
                ConnectionView(model: connection)
            }
        }
        .preferredColorScheme(.dark)
        .onChange(of: scenePhase) { _, phase in
            if phase == .active { connection.resume() }
        }
    }
}

private struct ConnectionView: View {
    @ObservedObject var model: ConnectionModel

    var body: some View {
        ZStack {
            LinearGradient(
                colors: [Color(red: 0.03, green: 0.10, blue: 0.15),
                         Color(red: 0.02, green: 0.04, blue: 0.07)],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()

            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    Text("OE")
                        .font(.system(size: 22, weight: .black))
                        .foregroundStyle(Color(red: 0.01, green: 0.08, blue: 0.12))
                        .frame(width: 54, height: 54)
                        .background(
                            LinearGradient(colors: [.cyan, .blue],
                                           startPoint: .topLeading,
                                           endPoint: .bottomTrailing),
                            in: RoundedRectangle(cornerRadius: 15)
                        )
                    Text("OPEN EFB MOBILE").font(.caption.bold()).tracking(2).foregroundStyle(.cyan)
                    Text("Connect to your flight deck").font(.largeTitle.bold())
                    Text("Enter the complete address shown on the OpenEFB About page while this device and the X-Plane computer are on the same private network.")
                        .foregroundStyle(.secondary)

                    Label(model.networkAvailable ? "Network available" : "Waiting for a network connection",
                          systemImage: model.networkAvailable ? "wifi" : "wifi.slash")
                        .font(.caption.bold())
                        .foregroundStyle(model.networkAvailable ? .green : .orange)

                    TextField("http://192.168.1.10:8383", text: $model.address)
                        .textInputAutocapitalization(.never)
                        .keyboardType(.URL)
                        .autocorrectionDisabled()
                        .submitLabel(.go)
                        .onSubmit(model.connect)
                        .padding(14)
                        .background(Color.white.opacity(0.07), in: RoundedRectangle(cornerRadius: 12))
                        .accessibilityLabel("OpenEFB computer address")

                    Button("Open Flight Deck", action: model.connect)
                        .frame(maxWidth: .infinity)
                        .padding(14)
                        .background(
                            LinearGradient(colors: [.cyan, .blue],
                                           startPoint: .leading,
                                           endPoint: .trailing),
                            in: RoundedRectangle(cornerRadius: 12)
                        )
                        .foregroundStyle(Color(red: 0.01, green: 0.07, blue: 0.10))
                        .fontWeight(.black)

                    if !model.error.isEmpty {
                        Text(model.error)
                            .font(.footnote)
                            .foregroundStyle(.red)
                            .accessibilityIdentifier("connection-error")
                    }

                    Text("For safety, the native app accepts only private-network, localhost, or .local OpenEFB addresses.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .padding(26)
                .frame(maxWidth: 560)
                .frame(maxWidth: .infinity)
            }
        }
    }
}
