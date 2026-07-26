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

Version 1.0.0 RC2 is the integrated acceptance build. It hardens X-Plane raster
display for map tiles and in-app PDF pages, adds repeatable release packaging,
and adds explicit compatibility for X-Plane's Vulkan/Zink OpenGL bridge. It is
gated by the complete simulator checklist in `docs/acceptance-test.md`.
It also adds X-Plane `.fms` route import and departure-destination named
exports plus persistent
high-contrast and comfort-size display preferences. It also generates each airport
briefing as an in-app PDF, reports FAA chart
download status inside the CHART list, and enables current Windows TLS for FAA
downloads. Map and PDF page pixels are forced opaque and use a bounded
geometry-based compatibility raster above the normal texture on graphics bridges
that fail to composite uploaded plugin textures.

Fuel flow is displayed in US gallons per hour using the standard avgas density
of 6.0 lb per US gallon. Remaining fuel mass stays visible in kilograms and
pounds because X-Plane reports fuel internally by mass.

Home now contains a large bordered live-map panel without taking over the full
page. On Windows, it loads OpenStreetMap Street tiles by default and offers a
one-click OpenTopoMap Topo view. It overlays range rings, aircraft heading, the
programmed route, the active leg, key waypoint labels, destination distance and
ETE, and supports mouse-wheel zoom from 5 to 320 nautical miles. Visible tiles
are loaded in the background, cached locally, and credited inside the map;
aircraft and fuel summaries remain visible below the panel. A source label shows
whether the visible basemap came from the network, cache, or vector fallback.
Internet access is required for tiles not already cached. The native tile adapter
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

Home now has independent WX, APT, NAV, and AIR switches. The installed X-Plane
navigation database supplies nearby airport, VOR, NDB, and fix symbols in addition
to the active route. Airport symbols are selectable; OpenEFB asks for confirmation
before replacing the current FMS route with a direct route to that airport. Any
visible Street/Topo attraction or map location can also be
selected for a confirmed coordinate direct-to without requiring a proprietary
points-of-interest database. WX highlights route endpoints that have current
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
