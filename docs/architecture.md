# Architecture

OpenEFB is split into a simulator-independent core and a thin X-Plane adapter.
The core owns application state and business rules. The adapter owns XPLM entry
points, simulator messages, and logging through `XPLMDebugString`.

## Invariants

- XPLM functions are called only from the simulator's main thread.
- The core does not include X-Plane SDK headers.
- Plugin entry points do not contain product logic.
- Owned simulator resources are released during disable or stop.
- Lifecycle transitions are explicit and safe to repeat where X-Plane may do so.

## Mobile 2.0: paired flight deck

- `XPlaneMobileServer` samples normalized model snapshots from an XPLM flight
  loop on the simulator main thread. The network worker receives only an
  immutable JSON copy and never calls XPLM or reads mutable core models.
- A bounded Winsock HTTPS server listens on port 8383 while the plugin is enabled
  and closes before telemetry, traffic, route, or weather adapters stop.
- Static mobile assets ship inside the plugin package. The browser client trades
  the per-session six-digit pairing code for a random 256-bit session token and
  polls versioned JSON endpoints while rendering its own OpenStreetMap canvas.
- The network worker validates and queues bounded commands but never calls XPLM.
  The simulator flight loop executes route, airport, procedure, and library
  commands on the main thread and publishes asynchronous command results.
- Route writes require the exact model revision used to create the phone draft.
  A conflicting desktop or cockpit change is rejected instead of overwritten.
- Library files are served only by an authenticated numeric entry selected from
  the model-published allowlist; client paths and traversal strings are never
  accepted as document locations.
- Schannel uses a persistent CNG private key and an OpenEFB-owned public
  certificate under the Windows user's local application data. The About page
  shows a stable six-digit certificate identity code separately from the random
  per-session pairing code.

### Native iOS shell

- SwiftUI owns connection state and retains only the validated OpenEFB computer
  address in user defaults; pairing credentials remain inside WebKit's protected
  website data store.
- Address validation accepts only private IPv4, local/private IPv6, localhost,
  and `.local` hosts over HTTPS. Credentials, public websites, unsupported schemes, query
  strings, and fragments are rejected or stripped before navigation.
- WebKit accepts the local self-signed certificate only when its SHA-256-derived
  identity code matches the value shown inside X-Plane, then saves the complete
  fingerprint in the iOS Keychain. A changed certificate requires explicit
  re-pairing; silent trust-on-first-use is not allowed.
- `WKNavigationDelegate` confines embedded navigation and streamed documents to
  the paired origin. User-selected external links are handed to Safari instead
  of receiving the OpenEFB web session.
- Foreground lifecycle events reload from the original server while preserving
  cookies and local app data. Process termination and network errors have visible
  recovery behavior.
- The Xcode project is generated from `mobile/ios/project.yml`; macOS CI compiles
  the app and unit-test bundle without signing. No Apple credential is stored in
  the repository.

The first implementation milestone is an in-simulator shell: a menu command,
window ownership abstraction, XPLM window lifecycle, and blank EFB surface.

## M1: in-simulator shell

- `WindowController` owns the window through the simulator-independent
  `WindowSurface` interface and creates it lazily on the first menu selection.
- `XPlaneWindow` owns exactly one `XPLMWindowID` and releases it in its
  destructor. Disable and stop reset the controller, so no simulator handle can
  outlive the enabled plugin session.
- `XPlaneMenu` registers under X-Plane's Plugins menu during `XPluginStart` and
  unregisters during `XPluginStop`.
- All XPLM creation, drawing, input, visibility, and destruction calls remain in
  the X-Plane adapter.

## M2: UI foundation

- `UiModel` owns navigation state and hit testing without depending on XPLM or
  graphics APIs.
- `XPlaneWindow` translates global desktop mouse coordinates to local EFB
  coordinates, delegates navigation to `UiModel`, and renders the resulting
  page from its XPLM window callback.
- `XPlanePreferences` stores validated window bounds in the X-Plane preferences
  directory. Geometry is restored when the EFB window is recreated.
- OpenGL calls occur only during the documented XPLM window drawing callback;
  application state changes remain outside rendering.

## M3: live aircraft telemetry

- `TelemetryModel` owns a simulator-independent, normalized snapshot of the
  active aircraft's identity, position, altitude, speed, heading, and vertical
  speed.
- `XPlaneTelemetry` resolves built-in datarefs only after plugin enable and
  samples them at 5 Hz from an after-flight-model callback.
- The flight loop is destroyed before disable or stop completes, ensuring no
  callback can outlive its owning runtime.
- Rendering reads the latest snapshot but never performs dataref access or
  changes application state.

## M4: active flight plan

- `FlightPlanModel` owns a normalized view of the FMS route and marks
  the leg X-Plane is currently flying toward.
- `XPlaneFlightPlan` samples the documented FMS entry API once per second and
  caps input at X-Plane's 100-entry route limit.
- Flight-plan sampling has its own after-flight-model callback so motion data
  can remain responsive without repeatedly scanning the route.
- The Flight Plan page shows route endpoints and a window around the active leg.
  Explicit user actions can apply a validated route or a selected installed CIFP
  approach through the documented FMS APIs. Procedure parsing stays in the
  simulator-independent core; navaid resolution and all FMS writes remain on the
  X-Plane main thread. OpenEFB never writes during preview or chart viewing.
- Destination Apply serializes an X-Plane 1100 flight plan with `DESRWY`, `APP`,
  and optional `APPTRANS`, then uses `XPLMLoadFMSFlightPlan` so stock avionics
  receive procedure leg types and altitude constraints for VPATH. The legacy
  entry writer remains a bounded fallback and never claims to configure a
  third-party aircraft's private FMC state.

## M5: route weather

- `WeatherModel` owns simulator-independent departure and destination METAR
  snapshots, their source, and the flight-plan revision used to select airports.
- `XPlaneWeather` selects the first and last airport entries in the active route
  and requests worldwide reports from AviationWeather.gov on a bounded worker.
- The main-thread callback reads X-Plane's last-downloaded METAR as an immediate
  fallback. Successful internet reports are cached per airport; source priority
  is online, simulator, then saved cache, and no XPLM call leaves the main thread.
- The Weather page labels each report ONLINE, X-PLANE, or SAVED CACHE and explains
  empty reports only after all three sources are unavailable.

## M6: route progress

- `RouteProgressModel` derives great-circle distance, initial true bearing, and
  groundspeed-based ETE from simulator-independent telemetry and route snapshots.
- `XPlaneRouteProgress` refreshes derived progress once per second without
  resolving additional datarefs or performing calculations in the draw callback.
- ETE is withheld below one knot to avoid misleading estimates while parked;
  distance and bearing remain available whenever route coordinates are valid.
- The Progress page separates the active waypoint from the final destination
  and states that estimates use direct distance and current groundspeed.

## M7: fuel monitoring

- `FuelModel` normalizes remaining fuel and total engine burn, then derives
  endurance and groundspeed-based range without depending on XPLM.
- `XPlaneFuel` samples total fuel and the eight-element engine fuel-flow array
  once per second, converting X-Plane's kilograms-per-second flow to kg/hour.
- Endurance is withheld when burn is effectively zero, and range is withheld
  below one knot so parked-aircraft estimates remain honest.
- The Fuel page reports remaining mass in kilograms and pounds. Fuel flow is
  converted to US GPH using a clearly labeled 6.0 lb/US gal avgas basis, while
  endurance calculations remain in X-Plane's native mass units.

## M8: moving-map foundation

- `MovingMapModel` owns discrete map ranges and a dateline-safe local
  latitude/longitude projection expressed in nautical miles, plus the selected
  Street or Topo presentation.
- Home uses a large, bordered north-up map panel while retaining summary cards
  below it. `MovingMapModel` maintains either a live-aircraft center or an
  independent dragged center and uses a heading-oriented aircraft symbol.
- The renderer clips route segments to the map viewport and distinguishes the
  active leg, route waypoints, endpoints, and destination progress.
- Mouse-wheel zoom ranges from 320 NM to 0.005 NM and preserves the coordinate
  beneath the pointer. Dragging pans without simulator writes and HOME restores
  live-aircraft tracking. Projection math remains in the core and is unit tested.
- `XPlaneMapTiles` requests only the raster tiles visible in the current panel.
  Network access and PNG decoding happen on a worker thread; OpenGL texture
  creation remains on X-Plane's drawing thread.
- The Windows adapter uses WinHTTP and Windows Imaging Component, requiring no
  extra runtime DLLs. Street and Topo tiles are cached for at least seven days,
  failed requests back off, and the in-memory texture set is bounded.
- The panel always shows the provider attribution required by OpenStreetMap and
  OpenTopoMap. Non-Windows adapters retain the vector fallback until native HTTP
  and image-decoding implementations are added.
- A visible source indicator distinguishes online tiles, cached tiles, and the
  vector-only fallback instead of presenting an empty panel as a complete map.
- `XPlaneMapPois` requests a bounded active-view query from Overpass only inside
  a 40 NM range. The core parser classifies named OpenStreetMap results into
  independent Food, Golf, and Sights layers. Results are de-cluttered before
  drawing and remain available during a temporary service failure.
- Hover cards and FMS confirmation dialogs are fully opaque. Confirming a place
  inserts its coordinate after the current active leg, selects the new entry for
  navigation, and preserves the remaining FMS route.
- Decoded tiles also retain a bounded 64-by-64 opaque color grid. It is drawn
  beneath the normal texture so the basemap remains visible on an X-Plane graphics
  bridge that accepts plugin geometry but does not composite the uploaded texture.

## M9: interactive flight-plan builder

- `FlightPlanEditor` owns a simulator-independent draft copied from the complete
  live route. X-Plane's separately exposed native approach legs are merged ahead
  of the destination, while non-navigable vector/discontinuity records stay out
  of the editable sequence. Adding, removing, selecting, and reordering legs
  never changes the FMS until the user explicitly chooses Apply.
- Draft input accepts normalized waypoint identifiers and caps routes at
  X-Plane's documented 100-entry limit. Core tests cover editing operations,
  identifier normalization, cancellation, and external-change detection.
- Departure and destination are explicit airport assignments rather than
  implicit generic entries. Enroute additions are kept ahead of the assigned
  destination, and endpoint labels remain visible while the draft is edited.
- `XPlaneFlightPlan` resolves identifiers through X-Plane's navigation database
  and pre-validates every draft leg before making any FMS write.
- Apply is rejected if the live route changed since the draft was opened. A
  successful write preserves the nearest valid displayed and active-leg indices,
  clears obsolete trailing entries and any superseded native approach layer, and
  immediately refreshes the live model. The overview uses the same merged route,
  so Builder, Flight Plan, and FMS cannot present different leg sets.
- Keyboard focus and mouse hit testing stay in the window adapter. Typing and
  Enter add identifiers, arrow keys or the wheel select rows, and dedicated
  controls reorder or remove the selected leg.

## M10: airport information

- `AirportInfoModel` owns a thread-safe snapshot because airport searches parse
  installed scenery away from X-Plane's main thread.
- The simulator-independent parser reads X-Plane 12 `apt.dat` airport blocks,
  deriving runway lengths from geodesic endpoint coordinates and preferring
  modern 8.33 kHz COM records over their legacy equivalents.
- CIFP parsing lists unique procedure identifiers from SID, STAR, and APPCH
  records without modifying X-Plane's navigation files.
- `XPlaneAirportData` discovers airport files beneath the simulator root,
  checks custom scenery before built-in Global Airports, and gives Custom Data
  procedures priority over the default navdata cycle.
- The Airports page displays loading, not-found, and error states explicitly;
  filesystem work never runs inside an XPLM draw or input callback.

## M11: operational map overlays

- `MovingMapModel` owns four independent visibility switches for weather,
  airports, navaids, and airspace; style and zoom remain independent.
- `AirspaceModel` publishes immutable, thread-safe snapshots of parsed OpenAIR
  zones. The parser supports polygon points, circles, and clockwise or
  counter-clockwise arcs while placing hard limits on malformed input.
- `XPlaneAirspace` reads Custom Data before default data on a worker thread and
  never calls XPLM outside construction on the simulator thread.
- The renderer clips every boundary segment, caps per-frame overlay work, and
  keeps the route and aircraft above the optional operational layers.
- WX currently indicates METAR availability at route endpoint airports; it is
  intentionally not presented as precipitation radar.
- `XPlaneNavigationDatabase` incrementally scans installed airports, VORs, NDBs,
  and fixes on X-Plane's main thread and publishes immutable snapshots without a
  long single-frame pause. Nearby symbols therefore remain available offline.
- Selectable airport and place symbols create a pending insertion action. An
  opaque modal requires explicit confirmation before `XPlaneFlightPlan` inserts
  the coordinate after the active leg and selects it without deleting the route.

## M12: aircraft planning

- `PlanningModel` combines live aircraft loading, fuel flow, destination ETE,
  and a user-selected reserve in simulator-independent calculations.
- `XPlanePlanning` samples public aircraft weight and CG datarefs once per
  second on X-Plane's flight-loop thread; no planning calculation writes to the
  aircraft.
- Gross-weight margin uses the loaded aircraft's own maximum weight. Predicted
  landing weight and fuel margin use current burn and route ETE, and clearly
  remain unavailable until both inputs are meaningful.
- The page does not synthesize takeoff distance, landing distance, or V-speeds.
  Those require a future aircraft performance profile backed by an AFM/POH or
  aircraft-provided data.

## M13: briefing workspace

- `BriefingModel` owns tab selection, the immutable-facing local library list,
  selected airport filter, stable selected path, interactive checklist state,
  and bounded multiline notes without depending on XPLM or the filesystem.
- Summary composes existing telemetry, route, weather, progress, and planning
  snapshots rather than creating a second source of flight truth.
- `XPlaneBriefingLibrary` scans only the two dedicated user-library folders,
  accepts a bounded set of safe document extensions, caps entry count and text
  size, and performs no filesystem work from draw callbacks.
- TXT and Markdown source files are exposed as generated PDF entries so every
  visible library item opens through the same in-app workflow. On Windows, `XPlanePdfViewer`
  renders PDF pages on a worker through `Windows.Data.Pdf`, decodes the resulting
  page image with WIC, and creates its OpenGL texture only on X-Plane's draw thread.
  Previous, Next, and Close remain explicit pilot actions.
- DEP and DEST filters are derived from the current route and remain selected
  across asynchronous refreshes. There is no combined All view, keeping each
  airport's generated briefing and charts together.
- Completed worker results are consumed exactly once before their optional slot
  is cleared, preventing a moved-from empty list from replacing visible entries.
- Visible Up/Down controls mirror mouse-wheel list navigation for cockpit setups
  where precise wheel input is inconvenient.
- Notes use a separate preference file so saving them cannot corrupt window
  geometry. They are bounded to 2,000 characters and saved when the window is
  hidden or destroyed.
- The library adapter observes route endpoints on X-Plane's flight-loop thread,
  copies the required snapshots, and queues all folder, document, catalog, and
  chart work to a background thread.
- Every endpoint receives ICAO-specific Charts and Documents folders plus a
  latest generated PDF briefing that opens in the in-app viewer. FAA d-TPP XML is parsed in the core, cached once
  per 28-day cycle, and used only to download charts belonging to that airport.
- PDF downloads are size-bounded, signature-checked, written through temporary
  files, and marked by cycle only after the airport set completes. Existing
  cycle markers suppress redundant network traffic.
- Automatic chart caching is limited to official FAA coverage. Subscription
  services whose terms prohibit offline caching are not scraped or archived.
- A chart-status PDF remains visible when the FAA catalog, airport record, or
  individual downloads are unavailable, so an empty chart folder is explained
  without relying on an external text viewer.
- The GeoPDF adapter reads FAA `/VP`, `/BBox`, `/GPTS`, `/LPTS`, and `/MediaBox`
  calibration arrays in the core. The PDF viewer projects live simulator
  coordinates into page space after raster scaling and scroll offset, then draws
  a blue ownship and altitude badge only while that position is inside the
  published chart extent. Ordinary PDFs never receive a guessed overlay.

## 1.0 raster presentation

- `XPlaneGpuImage` owns the shared map/PDF texture path. It binds texture IDs
  through XPLM, uploads complete CPU-rendered BGRA/RGBA surfaces, fully declares
  the required XPLM graphics state immediately before drawing, and relies on
  X-Plane's panel-coordinate transform rather than installing a plugin shader.
- Tile networking, PNG decoding, and Windows PDF rasterization remain off the
  simulator thread. Texture allocation, upload, and drawing remain inside the
  XPLM window drawing callback.
- Street and Topo tiles remain unmodified provider imagery. Route, aircraft,
  airport, navaid, weather, airspace, and place interaction are independent
  aviation overlays drawn above the basemap.
- A bounded software color grid is retained when a texture cannot be created;
  it does not cover successfully presented provider tiles.
- Raster diagnostics remain internal. The header exposes only the user-facing
  `MAP ONLINE`, `MAP CACHED`, or `MAP LOADING` state beside simulator live data.
- PDF pages fit the white document viewport by width. Vertical cropping is
  expressed through texture coordinates so the page cannot cover the viewer
  chrome, while mouse-wheel input advances a bounded source-page offset and a
  scrollbar reports the current position.

## Online traffic resilience

- Live adsb.lol requests execute on the traffic worker and are bounded to the
  persisted 25–200 NM user range. Returned positions are independently checked
  against the selected radius before entering the traffic model.
- Successful requests are spaced by at least 15 seconds. Failures back off to
  15, 30, 60, then 120 seconds; HTTP 429 responses immediately use the maximum
  interval to respect the provider's dynamic rate limits.
- A failed refresh never clears a still-recent target set. The UI marks that set
  degraded, expires it after 90 seconds, and then relies on simulator TCAS.
- Route metadata is queued separately, deduplicated, cached per session, and
  capped at eight pending callsigns so map interaction cannot create an
  unbounded request backlog.
