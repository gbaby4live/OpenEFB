import SwiftUI
import UIKit
import WebKit

struct OpenEFBWebView: UIViewRepresentable {
    let url: URL
    let reloadGeneration: Int
    let onReady: () -> Void
    let onFailure: (String) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator(origin: url, onReady: onReady, onFailure: onFailure)
    }

    func makeUIView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .default()
        configuration.allowsInlineMediaPlayback = true
        configuration.preferences.isElementFullscreenEnabled = true
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.navigationDelegate = context.coordinator
        view.uiDelegate = context.coordinator
        view.allowsBackForwardNavigationGestures = false
        view.scrollView.contentInsetAdjustmentBehavior = .never
        view.load(URLRequest(url: url, cachePolicy: .reloadRevalidatingCacheData, timeoutInterval: 10))
        return view
    }

    func updateUIView(_ view: WKWebView, context: Context) {
        context.coordinator.origin = url
        context.coordinator.onReady = onReady
        context.coordinator.onFailure = onFailure
        if view.url?.host != url.host || view.url?.port != url.port {
            view.load(URLRequest(url: url, cachePolicy: .reloadRevalidatingCacheData, timeoutInterval: 10))
        } else if context.coordinator.reloadGeneration != reloadGeneration {
            context.coordinator.reloadGeneration = reloadGeneration
            view.reloadFromOrigin()
        }
    }

    final class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
        var origin: URL
        var onReady: () -> Void
        var onFailure: (String) -> Void
        var reloadGeneration = 0

        init(origin: URL, onReady: @escaping () -> Void,
             onFailure: @escaping (String) -> Void) {
            self.origin = origin
            self.onReady = onReady
            self.onFailure = onFailure
        }

        func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                     decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            guard let destination = navigationAction.request.url else {
                decisionHandler(.cancel); return
            }
            if sameOrigin(destination, origin) || destination.absoluteString == "about:blank" {
                decisionHandler(.allow)
            } else {
                if navigationAction.navigationType == .linkActivated {
                    UIApplication.shared.open(destination)
                }
                decisionHandler(.cancel)
            }
        }

        func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                     for navigationAction: WKNavigationAction,
                     windowFeatures: WKWindowFeatures) -> WKWebView? {
            guard let destination = navigationAction.request.url else { return nil }
            if sameOrigin(destination, origin) { webView.load(navigationAction.request) }
            else { UIApplication.shared.open(destination) }
            return nil
        }

        func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!,
                     withError error: Error) { report(error) }
        func webView(_ webView: WKWebView, didFail navigation: WKNavigation!,
                     withError error: Error) { report(error) }
        func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) { onReady() }
        func webViewWebContentProcessDidTerminate(_ webView: WKWebView) { webView.reloadFromOrigin() }

        private func report(_ error: Error) {
            let nsError = error as NSError
            guard nsError.code != NSURLErrorCancelled else { return }
            onFailure("The X-Plane computer could not be reached. Keep OpenEFB running and confirm both devices are on the same network.")
        }

        private func sameOrigin(_ left: URL, _ right: URL) -> Bool {
            left.scheme?.lowercased() == right.scheme?.lowercased() &&
                left.host?.lowercased() == right.host?.lowercased() &&
                effectivePort(left) == effectivePort(right)
        }

        private func effectivePort(_ url: URL) -> Int {
            url.port ?? (url.scheme?.lowercased() == "https" ? 443 : 80)
        }
    }
}
