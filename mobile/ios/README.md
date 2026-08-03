# OpenEFB Mobile for iOS

This native SwiftUI shell hosts the same locally served OpenEFB flight deck as
the installable web app. It keeps the X-Plane computer address, requests Apple's
local-network permission, preserves the paired web session, and supports iPhone
and iPad rotation.

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

The current simulator bridge uses authenticated local HTTP. The broad transport
exception in `Info.plist` is limited by product behavior to the address entered by
the pilot, but it must be replaced by pinned TLS before an App Store submission.
