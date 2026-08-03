# OpenEFB Mobile for iOS

This native SwiftUI shell hosts the same locally served OpenEFB flight deck as
the installable web app. It accepts only private/local computer addresses,
requests Apple's local-network permission, preserves the paired WebKit session,
reconnects after foregrounding, confines embedded navigation to the paired
origin, and supports iPhone and iPad rotation.

## Build prerequisites

- macOS with the current Xcode release
- An Apple Developer team for device signing and TestFlight
- XcodeGen (`brew install xcodegen`)

Generate and open the project:

```sh
cd mobile/ios
xcodegen generate
open OpenEFBMobile.xcodeproj
```

Select the developer team under Signing & Capabilities, choose a physical iPhone,
and Run. TestFlight distribution requires an App Store Connect record and an
Archive uploaded from Xcode.

The `ios-ci.yml` GitHub workflow performs the same project generation and an
unsigned `build-for-testing` against the iOS Simulator SDK. It proves that the
app and unit-test bundle compile without requiring repository signing secrets.

The current simulator bridge uses authenticated local HTTP. The WebKit-only
transport exception in `Info.plist` is additionally constrained by the app's
private-host and same-origin checks, but it must be removed in favor of pinned
encrypted transport before an App Store submission. Follow
`../../docs/ios-release-checklist.md` for the release gates.
