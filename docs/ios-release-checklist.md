# OpenEFB native iOS release checklist

The repository contains a compilable, unsigned iPhone/iPad shell. A public
TestFlight or App Store claim is not complete until every gate below is satisfied.

## Source and continuous integration

- [x] SwiftUI application and persistent connection screen
- [x] Private-network-only address validation
- [x] Same-origin WebKit navigation and external Safari handoff
- [x] Foreground reconnect and WebKit process recovery
- [x] Local-network purpose string and privacy manifest
- [x] iPhone/iPad portrait and landscape support
- [x] Address-validation unit tests
- [x] macOS CI project generation and unsigned simulator compilation

## Transport security

- [x] Replace authenticated local HTTP with Schannel HTTPS
- [x] Pin the OpenEFB simulator bridge identity in the native client
- [x] Remove `NSAllowsArbitraryLoadsInWebContent`
- [ ] Re-run pairing, replay, wrong-code, expired-session, and hostile-LAN tests
- [ ] Document certificate/key rotation and recovery

The private identity key is non-exportable and remains in the Windows user's CNG
key storage. The public certificate is stored at
`%LOCALAPPDATA%\OpenEFB\mobile-identity.cer`. If that public file is lost or
replaced, OpenEFB creates a new certificate and shows a new ID code; the phone
rejects the change until the pilot enters that new code. A future Settings action
will provide intentional full key rotation without requiring command-line tools.

## Apple build and device acceptance

- [ ] Open the generated project with the current stable Xcode
- [ ] Select the OpenEFB Apple Developer team and final bundle identifier
- [ ] Add final AppIcon and launch assets with confirmed licensing
- [ ] Run unit tests on the current iOS Simulator
- [ ] Test on a physical iPhone and iPad over Wi-Fi
- [ ] Verify background/foreground, screen lock, rotation, PDF, and network-loss recovery
- [ ] Produce an Archive with no signing, privacy, or entitlement warnings

## App Store Connect and TestFlight

- [ ] Create the App Store Connect application record
- [ ] Complete privacy disclosures: no tracking and local simulator data only
- [ ] Provide support URL, privacy-policy URL, screenshots, and review notes
- [ ] Upload the signed Archive and pass automated processing
- [ ] Complete internal TestFlight acceptance before inviting external testers
- [ ] Publish only after encrypted transport and physical-device acceptance pass
