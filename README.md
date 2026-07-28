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
of 6.0 lb per US gallon. Remaining fuel mass stays visible in kilograms and
pounds because X-Plane reports fuel internally by mass.

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
from the free and open [adsb.lol API](https://api.adsb.lol/). Online requests are
limited to 100 NM around the aircraft, run off the simulator thread every 15
seconds, reject stale positions, and fall back to TCAS when the network is not
available. The map shows the active source and required adsb.lol ODbL
attribution. Traffic is for simulator situational awareness only and must not be
used for real-world separation or navigation.
Settings includes an opt-in `Inject online traffic into X-Plane TCAS` control.
When enabled and online targets are available, OpenEFB requests exclusive
multiplayer-aircraft access, publishes up to 63 nearby targets to compatible
cockpit traffic displays, and refreshes the TCAS arrays every frame. OpenEFB
does not take control when another provider owns traffic and releases control
when another plugin requests it. This stage supplies TCAS awareness only; it
does not create exterior 3D aircraft models.
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
