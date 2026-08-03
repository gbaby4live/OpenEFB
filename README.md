# OpenEFB

OpenEFB is an open-source electronic flight bag plugin for X-Plane 12.

## Build the core and tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build the X-Plane plugin

Download the latest SDK from the
[official X-Plane Plugin SDK page](https://developer.x-plane.com/sdk/plugin-sdk-downloads/),
extract it, and point CMake at the extracted SDK:

```sh
cmake -S . -B build -DOPEN_EFB_XPLANE_SDK_PATH=/path/to/SDK
cmake --build build
```

The plugin is emitted with X-Plane's package layout:

- `build/OpenEFB/64/win.xpl`
- `build/OpenEFB/64/mac.xpl`
- `build/OpenEFB/64/lin.xpl`

Without `OPEN_EFB_XPLANE_SDK_PATH`, CMake skips the simulator adapter while the
core library and tests remain fully buildable.

## In X-Plane

After installing the built `OpenEFB` plugin package, open it from:

`Plugins > OpenEFB > Show / Hide OpenEFB`

For a local Windows build, copy the generated `build/OpenEFB` directory into
`X-Plane 12/Resources/plugins` before starting the simulator.

The integrated shell provides a dark tablet layout with a live UTC status bar and nine
mouse-selectable pages: Home, Flight Plan, Airports, Progress, Weather, Planning,
Briefing, Settings, and About. The Fuel page is now Aircraft Planning, combining
fuel with weight-and-balance information. Window position and size are saved in X-Plane's
preferences directory. Home and Aircraft display
live aircraft identity, position, altitude, ground speed, heading, and vertical
speed sampled from X-Plane at 5 Hz. High-contrast text and an enforced minimum
window size keep telemetry cards legible without overlapping. Flight Plan reads
the active X-Plane FMS route once per second and highlights the leg currently
being flown. Weather requests current METARs for the first and last route airports
from AviationWeather.gov, then falls back to X-Plane weather and a saved local
cache; each card labels the source. Progress calculates direct
distance, true bearing, and estimated time to the active waypoint and destination
from live aircraft groundspeed. Fuel shows remaining mass, total engine burn,
endurance, and estimated range from X-Plane's live fuel system. The window
resource is created on first use and released whenever the plugin is disabled
or stopped.

Version 0.13.1 fixes X-Plane texture allocation for visible Street/Topo tiles,
prevents completed Library scans from being republished as empty lists, and adds
visible Up/Down Library controls. The Weather page displays online connection
status and retries failed internet reports once per minute.

Version 0.13.2 accepts both current raw METAR station formats and exposes the map
pipeline stage on Home. Invalid cached responses are discarded, only validated
PNG tiles are saved, and the display distinguishes network, PNG decoding, and
successful texture-upload states.

Version 0.13.3 binds map textures through X-Plane's graphics API and activates
the textured drawing state before upload. Briefing now has only Departure and
Destination airport views. PDF charts render inside OpenEFB with Previous, Next,
and Close controls using the Windows PDF renderer included with the operating system.

Version 1.0.0 RC6 is the interactive map and chart-reliability build. It presents the shared
CPU-rendered BGRA map and PDF surfaces through X-Plane-managed texture binding
and drawing, matching the simulator's supported Vulkan/Zink bridge contract
without installing a competing plugin shader. It hardens X-Plane raster display
for map tiles and in-app PDF pages, adds repeatable release packaging, and is
gated by the complete simulator checklist in `docs/acceptance-test.md`.
It also adds X-Plane `.fms` route import and departure-destination named
exports plus persistent
high-contrast and comfort-size display preferences. It also generates each airport
briefing as an in-app PDF, reports FAA chart
download status inside the CHART list, and enables current Windows TLS for FAA
downloads. Map and PDF page pixels are forced opaque, while a bounded
geometry-based compatibility raster remains available if a texture cannot be created.
RC5 removed renderer diagnostics from the flight-facing interface, moved the
plain-language map connection state beside Live Data, enforced a layout-safe
minimum window size, and added width-fitted PDF pages with mouse-wheel scrolling
and a visible position indicator.
RC6 adds independent Food, Golf, and Sights layers from OpenStreetMap data,
opaque hover and confirmation cards, drag panning, pointer-centered wheel zoom,
an aircraft recenter control, and Street-map zoom down to airport-surface scale.
Confirmed places are inserted after the active FMS leg instead of replacing the
route. It also corrects the current FAA d-TPP catalog filename, accepts larger
official chart PDFs, replaces generated status text with PDFs, and presents
TXT/Markdown library files as converted in-app PDF entries.
RC7 makes all map annotations readable over detailed tiles with opaque,
high-contrast label badges and consistent simulator-native proportional typography.
Airport and place hover cards are compact, and the
route confirmation is now a small light-surface card that shows the place,
category, and coordinates without covering the map. Dedicated zoom, departure,
arrival, and aircraft-centering controls provide the familiar live-map workflow.
One POI control shows or hides Food, Golf, and Sights together while preserving
their distinct marker colors, icons, and descriptions.
The tactical visual pass removes circular range rings, replaces circular status
markers with corner reticles, and uses a recognizable white mini-airplane with a
cyan outline for live position. Map buttons use a compact deep-navy fill, white
shadowed labels, crisp borders, and a brighter solid hover state so every action
remains legible against detailed terrain.

Version 1.0.0 RC8 fixes solid overlays at the graphics-state boundary so map
textures cannot leak into buttons, labels, notifications, or selection cards.
It adds a Sky4Sim-inspired Map Filters panel for aircraft, live aircraft
information, flight plan, labels, weather, airports, navaids, airspace, and POIs.
A compact ALT/GS/HDG strip accompanies the existing zoom, DEP, ARR, HOME,
Street, and Topo tools. RC8 also removes the false sub-1-NM tile clamp, reaches
a 0.005 NM map radius, and removes the unnecessary center guide lines.

Version 1.0.0 RC9 consolidates every optional map overlay inside Map Filters;
the map header now contains only Filter, Street, and Topo. The complete OpenEFB
interface uses an XP-Career-inspired charcoal surface hierarchy with black
toolbars, green primary states, cyan informational accents, yellow hover borders,
filled buttons, and consistent X-Plane proportional sans-serif typography.

Version 1.0.0 RC10 makes solid UI state explicit before every primitive so the
old canvas or active map texture cannot bleed through cards and buttons. It adds
separate shell, page, overlay, scrim, and modal layers; an opaque contextual map
menu with Center, Airport Details, and Add FMS actions; a larger layout-safe
minimum window; and zoom-aware label collision avoidance. Existing OpenEFB
weather, airport lookup, route editing, FMS sync, PDF viewing, and briefing notes
cover the corresponding high-value Sky4Sim workflows without proprietary assets.

Version 1.0.0 RC11 forces opaque controls to render with blending disabled,
then restores blending only for intentionally translucent overlays. Buttons and
map controls are filled blocks with strong state strips instead of thin outlined
"skeleton" boxes; navigation uses filled rows and a left state rail, while the
unnecessary outer page and map frame borders are removed.

Version 1.0.0 RC12 replaces opaque immediate-mode fills with tiny opaque textures
drawn through X-Plane's own image bridge. This is the same proven renderer used
for the visible map tiles and ensures every button, card, navigation row, and
page surface has a real solid fill on Vulkan/Zink instead of a hollow outline.

Fuel flow is displayed in US gallons per hour using the standard avgas density
of 6.0 lb per US gallon. All user-facing weight and fuel-mass values are shown
in pounds; OpenEFB converts X-Plane's internal metric values at presentation time.

Home now contains a large bordered live-map panel without taking over the full
page. On Windows, it loads OpenStreetMap Street tiles by default and offers a
one-click OpenTopoMap Topo view. It overlays aircraft heading, the
programmed route, the active leg, key waypoint labels, destination distance and
ETE, and supports mouse-wheel zoom from 0.005 to 320 nautical miles. Visible tiles
are loaded in the background, cached locally, and credited inside the map;
aircraft and fuel summaries remain visible below the panel. A source label shows
whether the visible basemap came from the network, cache, or vector fallback.
The map can be dragged away from the aircraft, zoomed around the pointer, and
returned to live position with HOME. Food, Golf, and Sights share one POI layer;
hovering a marker identifies it and clicking it offers an opaque confirmation
before inserting the coordinate into the active X-Plane FMS route.
Internet access is required for tiles not already cached. The native tile adapter
requests the true sub-1-NM tile scale instead of clamping airport views to a
regional zoom, allowing Street tiles to reach zoom 19 and Topo tiles to reach
zoom 17 without falsely enlarging a lower-detail image.
The Traffic filter combines nearby X-Plane TCAS targets with live ADS-B traffic
from the free and open [adsb.lol API](https://api.adsb.lol/). Map Filters offers
a persistent 25–200 NM online range in 25-NM steps. Requests run off the
simulator thread no more often than every 15 seconds, reject stale or
out-of-range positions, and fall back to TCAS when the network is unavailable.
Provider failures use a bounded 15/30/60/120-second retry backoff. Recent online
targets remain visibly marked as degraded for no more than 90 seconds, after
which they are removed rather than presented as current. The filter shows the
active source, retry health, configured range, and required adsb.lol ODbL
attribution. Traffic is for simulator situational awareness only and must not be
used for real-world separation or navigation.
Settings includes an opt-in `Inject online traffic into X-Plane TCAS` control.
When enabled and online targets are available, OpenEFB requests exclusive
multiplayer-aircraft access, publishes up to 63 nearby targets to compatible
cockpit traffic displays, and refreshes the TCAS arrays every frame. OpenEFB
does not take control when another provider owns traffic and releases control
when another plugin requests it. Injection supplies cockpit TCAS awareness; it
does not register synthetic flights with X-Plane's native ATC controller.
Yellow traffic aircraft are selectable. Clicking one opens a compact opaque
card with its published identity, altitude, speed, and, when available, its
departure and destination. Route metadata is requested only for the selected
callsign and cached for the simulator session from the
[adsb.lol VRS standing-data project](https://github.com/adsblol/vrs-standing-data),
which is released under CC0. Clicking the same aircraft closes the card.
Inspecting traffic never changes the active FMS route. Provider route metadata
can be missing or stale and is never guessed; all traffic remains simulator-only
situational information, not a source for real-world navigation or separation.
for macOS and Linux is planned; those builds retain the vector map fallback.

Flight Plan now includes an interactive builder with clearly labeled Departure
and Destination fields. Select Edit, type an exact airport identifier, and use
Set DEP or Set DEST. Fixes, VORs, and NDBs can be inserted with Add VIA. Select
draft legs with the mouse, arrow keys, or mouse wheel; use Up, Down, and Remove
to arrange the enroute portion. Changes remain isolated from X-Plane until
Apply is selected. OpenEFB validates all identifiers first and refuses to
overwrite the FMS if its live route changed after the draft was opened.

Airports provides offline lookup against the scenery and navigation data
installed with X-Plane. Search by airport identifier to see field elevation,
runway identifiers, calculated runway dimensions, surfaces, communication
frequencies, and the installed SID, STAR, and approach names. Searches run in
the background so scanning large scenery files does not pause the simulator.

Home now has independent weather, airport, navaid, airspace, traffic, and POI
filters. The installed X-Plane
navigation database supplies nearby airport, VOR, NDB, and fix symbols in addition
to the active route. Airport and OpenStreetMap place symbols are selectable;
OpenEFB asks for confirmation before inserting the coordinate after the active
FMS leg and selecting it for navigation without deleting the remaining route.
WX highlights route endpoints that have current
METAR reports, and AIR draws installed X-Plane OpenAIR boundaries including
polygons, circles, and directional arcs. Custom Data airspace takes priority
over X-Plane's default file. Parsing happens in the background and on-screen
work is bounded to protect simulator frame time.

Aircraft Planning reads the active aircraft's empty, payload, fuel, gross, and
maximum weights plus its live CG offset. It combines destination ETE with current
engine burn and an adjustable reserve to estimate trip fuel, reserve fuel, fuel
margin, and landing weight. Weight and fuel warnings are aircraft-aware, while
the page explicitly avoids invented runway distances or V-speeds when no
aircraft performance profile is available.

Briefing combines the active aircraft, route, departure and destination weather,
and planning readiness on one summary. Its interactive six-item checklist resets
for each flight session, and its multiline notes are saved between sessions.
The local Library scans `Output/preferences/OpenEFB/Library/Charts` and
`Output/preferences/OpenEFB/Library/Documents` for PDF, PNG, JPG, TXT, and MD
files. Text and Markdown are readable inside OpenEFB; charts, images, and PDFs
remain selected inside the Library, and PDF charts open in an in-app page viewer. The list
supports mouse-wheel navigation, keeps its selected document through a refresh,
and provides persistent DEP and DEST filters so both route archives remain easy
to select. Refresh discovers newly added files.

When a route contains departure and destination airports, OpenEFB automatically
creates `Charts/<ICAO>` and `Documents/<ICAO>` archive folders for both endpoints.
It saves a current OpenEFB briefing document with route, METAR, weight, fuel,
reserve, and landing estimates for every airport worldwide. For airports listed
in the official U.S. FAA d-TPP catalog, a background worker also downloads the
current airport diagram and terminal-procedure PDFs. A cycle marker prevents
unchanged charts from being downloaded again; a new effective cycle refreshes
the saved set. Other countries keep their generated briefing and user-provided
documents because no single worldwide source permits permanent offline caching.

See [docs/architecture.md](docs/architecture.md) for architectural boundaries
and threading invariants, and [docs/product-roadmap.md](docs/product-roadmap.md)
for the MSFS 2024-style feature roadmap.

RC27 adds reversible enroute waypoints, current/TAF weather modes, cockpit-matched
indicated altitude and magnetic heading, native X-Plane traffic-advisory
participation, responsive page scrolling, compact briefing controls, airport
name/code map actions, and the refreshed premium cockpit palette.

RC28 adds optional moving exterior traffic through X-Plane's supported instancing
API, capped at 12 objects within 25 NM. It first uses a lightweight recognizable
regional-jet model already installed with X-Plane and retains OpenEFB's original
generic aircraft as a portable fallback; no X-Plane assets are redistributed.
It also fixes compact aircraft-strip clipping, persists additional
map and traffic settings, expands FAA airport identifier matching, ignores FAA
deletion placeholders, and records actionable chart cycle and HTTP diagnostics.
The exterior model is two-sided and carries navigation/strobe points for better
visual acquisition. Its lightweight faceted fuselage makes nearby targets read
as aircraft instead of flat symbols. Exterior models remain independent of TCAS
ownership, so another cooperative traffic plugin can own cockpit TCAS without
making OpenEFB's enabled exterior layer disappear. A saved Settings slider
adjusts the magenta route from a 2-pixel fine line to a 12-pixel bold line.
Nearby airborne traffic inside 3 NM and 1,200 feet also produces an OpenEFB
clock-position, distance, and relative-altitude callout. X-Plane displays and
speaks that callout according to the user's text-to-speech preferences. This is
an EFB safety aid, not a native ATC instruction; equipped aircraft on X-Plane
12.4.1 or newer can independently generate native TCAS TA/RA advisories.

RC29 adds a premium Approach Control panel to Flight Plan. Departure and
destination selectors load every approach and transition published in the
installed X-Plane CIFP database. The panel previews the procedure name, runway,
transition, and navigable fix sequence before Apply writes anything. For a
destination approach on X-Plane 12.1 or newer, Apply uses X-Plane's documented
procedure-aware FMS loader with the published approach, transition, and runway.
That preserves procedure leg types and altitude constraints needed by compatible
stock avionics to calculate VPATH. The validated fix-sequence writer remains a
fallback for older SDKs and departure-endpoint previews. Applying is supported
while parked or airborne, preserves the remaining route, and enforces X-Plane's
100-entry route limit. Aircraft-specific third-party FMCs may still require their
own procedure activation step.

Plate opens the closest matching chart already in the local Briefing Library.
If it is missing, OpenEFB starts a fresh endpoint archive and FAA chart download;
select Plate again after synchronization. OpenEFB rejects and retries truncated
FAA catalogs instead of caching them as a successful cycle. Automatic official
downloads cache every published plate for active U.S. departure and destination
airports and retain visited-airport charts for offline use; a new FAA cycle
refreshes them. Bulk-bundling every major airport worldwide is intentionally not
done because coverage, redistribution rights, and update cycles vary by country.
Other regions use pilot-provided or separately licensed PDFs saved under
`Library/Charts/<ICAO>`.

Version 1.0.0 is the stable OpenEFB desktop release for X-Plane 12. The mobile
companion application is planned as the next product phase.

FAA GeoPDF approach plates and airport charts can show the live aircraft as a
blue airplane directly on the opened chart. The marker uses the plate's embedded
geographic calibration, follows true position and heading through the published
approach vicinity, and displays live altitude plus the active X-Plane approach
constraint when available. Non-georeferenced PDFs remain unmodified rather than
showing an estimated position. The chart list and full Briefing page provide
visible scroll rails and mouse-wheel access at compact window sizes. The compact
navigation control is labeled `Menu`.

The final 1.0 interaction pass places Approach inside Flight Plan Builder and
opens the published transition picker by double-clicking an approach; the old
Transition button is removed. Selected
procedure fixes can be excluded or restored in the preview before Apply. An
unmodified destination procedure continues to use X-Plane's native loader,
while an intentionally edited procedure is written as a clearly identified
custom fix sequence. Generated plans read the active AIRAC cycle from X-Plane's
navigation database, preventing the former `CYCLE 0000` mismatch warning.

Briefing now includes fully offline Preflight, Takeoff/Cruise, and
Descent/Landing general checklists labeled with the current aircraft. They are
operational aids and do not replace the aircraft's approved POH/AFM. Clicking a
saved logbook flight opens its route, duration, distance, altitude, speed,
climb/descent, landing-rate, and track-point details. Chart ownship labels use a
high-contrast light badge with dark text, and both chart and map ownship symbols
use a clean centered outline without a displaced fill shadow.

X-Plane CIFP approach variants such as KSAN `I09-Y` and `I09-Z` retain their
full procedure identifiers while the generated plan correctly targets runway
`RW09`. Transition previews preserve repeated, sequence-distinct fixes such as
procedure-turn legs and can be mouse-wheel scrolled independently.

Flight Plan Builder is now the authoritative route editor. After an approach is
applied, its navigable transition and final fixes appear in the Builder ahead of
the destination and can be selected, moved, or removed. Applying the edited
draft clears the prior native approach layer and writes the complete revised
sequence to X-Plane, so OpenEFB and the FMS cannot retain different copies. The
former Cancel control is labeled `Flight Plan` and returns to an overview that
always lists every route and approach leg with page scrolling.

## Mobile companion 2.0

OpenEFB can host a paired mobile flight deck on the X-Plane computer for an
iPhone or other modern phone connected to the same trusted Wi-Fi network. Open
the in-simulator About page to find the encrypted local URL, stable six-digit
identity code, and per-session pairing code. The responsive companion shows live aircraft
telemetry, an OpenStreetMap moving map, the active magenta route, nearby traffic,
route progress, and departure/destination METARs. Use Safari's **Add to Home
Screen** action to launch it like an app.

Mobile 2.0 adds a session-authenticated command channel. Route changes remain a
phone-side draft until **Apply to FMS** is selected, and revision checks refuse
to overwrite a route changed in X-Plane after editing began. The Plan page can
add, remove, and reorder enroute fixes; browse installed departure or destination
approaches and transitions; exclude optional procedure fixes; and explicitly
apply the result to the simulator. Briefing streams the desktop OpenEFB chart and
document library into an in-app PDF viewer. The shell reconnects after temporary
network loss and includes installable-app cache metadata where the browser permits
secure-context service workers.

The pairing exchange creates a random 256-bit session token and every command is
accepted only on X-Plane's main thread. Windows Schannel encrypts the local HTTPS
channel, while the native app verifies the displayed identity code and pins the
full certificate fingerprint in the device Keychain. The first Windows launch
may require allowing X-Plane through Windows Firewall on private networks. A native
SwiftUI iPhone/iPad shell is provided under `mobile/ios`; device signing and
TestFlight require macOS, Xcode, and the maintainer's Apple Developer account.
The native shell restricts connections to private/local hosts, retains the
paired WebKit session across backgrounding, rechecks the simulator when returning
to the foreground, and prevents the embedded browser from navigating to an
unrelated origin. GitHub's macOS workflow generates the Xcode project and compiles
the app and its address-validation tests for the iOS Simulator. See
`docs/ios-release-checklist.md` for the remaining device and signing gates.
