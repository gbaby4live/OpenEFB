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

- `FlightPlanModel` owns a normalized, read-only view of the FMS route and marks
  the leg X-Plane is currently flying toward.
- `XPlaneFlightPlan` samples the documented FMS entry API once per second and
  caps input at X-Plane's 100-entry route limit.
- Flight-plan sampling has its own after-flight-model callback so motion data
  can remain responsive without repeatedly scanning the route.
- The Flight Plan page shows route endpoints and a window around the active leg;
  OpenEFB does not modify or program the simulator FMS.

## M5: route weather

- `WeatherModel` owns simulator-independent departure and destination METAR
  snapshots, including the flight-plan revision used to select the airports.
- `XPlaneWeather` selects the first and last airport entries in the active route
  and reads X-Plane's last-downloaded METARs every 15 seconds.
- Weather access runs only from a before-flight-model callback as required by
  the X-Plane SDK and remains strictly read-only.
- The Weather page wraps reports into high-contrast route cards and explains
  empty reports when Real Weather data is unavailable.

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
  below it. The map stays centered on live aircraft position and uses a
  heading-oriented aircraft symbol.
- The renderer clips route segments to the map viewport and distinguishes the
  active leg, route waypoints, endpoints, and destination progress.
- Mouse-wheel zoom changes range without simulator data writes. Map projection
  math remains in the core and is covered by unit tests.
- `XPlaneMapTiles` requests only the raster tiles visible in the current panel.
  Network access and PNG decoding happen on a worker thread; OpenGL texture
  creation remains on X-Plane's drawing thread.
- The Windows adapter uses WinHTTP and Windows Imaging Component, requiring no
  extra runtime DLLs. Street and Topo tiles are cached for at least seven days,
  failed requests back off, and the in-memory texture set is bounded.
- The panel always shows the provider attribution required by OpenStreetMap and
  OpenTopoMap. Non-Windows adapters retain the vector fallback until native HTTP
  and image-decoding implementations are added.

## M9: interactive flight-plan builder

- `FlightPlanEditor` owns a simulator-independent draft copied from the live
  route. Adding, removing, selecting, and reordering legs never changes the FMS
  until the user explicitly chooses Apply.
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
  clears obsolete trailing entries, and immediately refreshes the live model.
- Keyboard focus and mouse hit testing stay in the window adapter. Typing and
  Enter add identifiers, arrow keys or the wheel select rows, and dedicated
  controls reorder or remove the selected leg.
