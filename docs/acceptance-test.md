# OpenEFB 1.0 acceptance test

This checklist is the release gate. A final version must not be published while
any required item is failing.

## Installation and lifecycle

- Install the packaged `OpenEFB` folder under `Resources/plugins`.
- Confirm the window cannot be resized below the overlap-safe minimum and that
  header labels, map controls, cards, and footer remain separated.
- Open a PDF, place the pointer over the white page, and confirm the mouse wheel
  scrolls the document while the title, toolbar, and scrollbar remain fixed.
- Confirm X-Plane loads the plugin without an error in `Log.txt`.
- Open, close, resize, disable, and re-enable OpenEFB.
- Restart X-Plane and confirm the saved window and display settings return.

## Home and map

- Confirm live aircraft position, motion, fuel, route, and UTC data.
- Confirm Street and Topo basemaps are visibly different from the EFB background.
- Confirm online tiles become cached and remain visible after disconnecting.
- Exercise every overlay and zoom from 320 NM down to 0.005 NM at an airport.
- Confirm airport, waypoint, weather, and airspace labels remain readable over
  both Street and Topo tiles.
- Confirm circular range rings are absent and the live position is a clearly
  recognizable mini-airplane oriented to aircraft heading.
- Confirm map controls have compact deep-navy fills, crisp borders, white shadowed
  labels, and a brighter solid high-contrast hover treatment.
- Confirm the +, -, DEP, ARR, and HOME controls zoom or center the map correctly.
- Drag the map, zoom around an off-center pointer, and confirm HOME returns to the aircraft.
- Toggle POI off and on, then hover Food, Golf, and Sights icons and confirm the
  opaque card identifies each category with clear proportional typography.
- Click an airport or place and confirm its compact light card remains readable
  without unnecessarily obscuring the map.
- Click an arbitrary map point and confirm the response card, notification,
  selected marker label, and both action buttons have fully solid backgrounds.
- Open Map Filters and toggle aircraft, aircraft information, flight plan, map
  labels, weather, airports, navaids, airspace, and points of interest.
- Confirm the top map toolbar contains only Filter, Street, and Topo; WX, APT,
  NAV, AIR, and POI must appear only inside Map Filters.
- Confirm charcoal surfaces, green selected actions, cyan information, yellow
  hover borders, filled buttons, and proportional text are consistent on every page.
- Confirm map clicks create a distinct dimmed overlay and fully opaque modal;
  Center, Airport Details, and Add FMS must operate independently.
- At a dense airport region, zoom from 40 NM inward and confirm labels are
  progressively revealed without overlapping into an unreadable cluster.
- Confirm every normal button and map control has a fully opaque dark or green
  fill with no thin outline-only state; hover must brighten the complete button.
- Select a place or arbitrary map point, approve Add to FMS, and confirm it is
  inserted after the active leg without deleting the remaining route.

## Route workflow

- Create and edit a route with explicit departure and destination airports.
- Apply it to the X-Plane FMS and verify every leg.
- Export it and confirm the filename is `DEPARTURE-DESTINATION.fms`.
- Import the exported plan and verify it round-trips without losing legs.

## Airport, weather, progress, and planning

- Search both a custom-scenery airport and a default airport.
- Confirm runways, frequencies, and installed procedures where available.
- Confirm online METAR, simulator fallback, and saved-cache behavior.
- Confirm active-leg/destination distance, bearing, ETE, fuel, loading, and reserve controls.

## Briefing and documents

- Confirm both DEP and DEST remain visible after a Library refresh.
- Open generated briefing PDFs and saved FAA charts on a white in-app page.
- At a U.S. airport, Refresh and confirm official FAA chart PDFs replace any
  chart-status PDF. Confirm TXT/Markdown sources appear as in-app PDF entries.
- Exercise Previous, Next, and Close and verify all page content is readable.
- Confirm notes persist and the checklist resets for a new plugin session.

## Release decision

Record X-Plane version, graphics backend, operating system, aircraft, and any
failed step. Map/PDF visibility failures are release blockers, not optional polish.
# RC16 compact interface and map markers

1. Resize OpenEFB down to approximately AviTab size (360 x 300).
2. Confirm the sidebar changes into a `PAGES` button and the selected page
   remains readable without being covered by navigation.
3. Open `PAGES`, choose several pages from both columns, then close the menu.
4. Use the compact header `-` and `+` buttons and confirm the window resizes.
5. On Home, confirm the active route guidance is magenta.
6. Open Map Filters and confirm Airports, Navaids, and Points of interest can
   be hidden independently.
7. Change Icon Size from 75 through 150 percent and confirm airport, VOR, NDB,
   fix, food, golf, and landmark image markers resize and remain clickable.
# RC20 map space and automatic briefing synchronization

1. Confirm Home no longer shows the `Live Moving Map` heading and the map uses
   the reclaimed space.
2. Load a route with airport departure and destination endpoints.
3. Confirm Briefing > Library reports that DEP/DEST briefs and charts
   synchronized and both airport filters remain populated.
4. Leave the route active and confirm the latest briefing PDFs are regenerated
   after updated weather arrives or within five minutes.
5. Confirm existing current-cycle FAA charts are retained without duplicate
   files and incomplete downloads are retried.
# RC21 persistent flight history

1. Complete a flight lasting at least 30 airborne seconds and land below
   45 knots groundspeed.
2. Confirm Logbook shows UTC time, aircraft, route, airborne time, distance,
   maximum altitude, and landing rate.
3. Close X-Plane, restart it, and confirm the completed flight remains.
4. Replace the OpenEFB plugin build and confirm history remains because it is
   stored under `Output/preferences/OpenEFB/flight-history.tsv`.
5. Confirm a rejected or very short takeoff does not create a completed entry.

# RC22 live traffic and map symbols

1. Enable X-Plane AI aircraft or a traffic plugin that publishes targets to
   X-Plane's TCAS datarefs.
2. Open Home > Filter and confirm `Traffic` appears as its own filled toggle.
3. With Traffic enabled, confirm nearby targets use yellow aircraft image
   symbols with direction vectors rather than letter-only markers.
4. Confirm an uncrowded target label shows its callsign and either `GND` or
   relative altitude in hundreds of feet with climb, descent, or level trend.
5. Turn Traffic off and confirm all traffic symbols, vectors, and labels
   disappear while route, POI, airport, and navaid layers remain unchanged.
6. Turn Traffic back on and confirm the ownship symbol remains visually
   distinct and the map continues to pan, zoom, and resize smoothly.

# RC22A map alignment and compact filters

1. Open Home > Filter and confirm the Traffic row reports the number of TCAS
   targets OpenEFB currently receives.
2. At a compact window size, place the pointer over Filters and use the mouse
   wheel to reach every filter and the icon-size controls; confirm the scroll
   thumb reflects the current position.
3. Fly on or near the active magenta route and zoom repeatedly from regional
   range down to airport detail. Confirm the aircraft, route, traffic, POIs,
   airspace, and basemap remain geographically aligned at every zoom step.
4. Zoom around an off-center pointer, click map points, and drag the map.
   Confirm the pointer location remains fixed and selected coordinates match
   the visible basemap feature.
5. Press HOME and confirm the map returns to the aircraft without displaying
   a persistent `Map centered on aircraft` message.

# RC23 online ADS-B traffic

1. Start X-Plane with internet access and a valid live aircraft position.
2. Open Home > Filter, enable Traffic, and wait up to 20 seconds for the first
   bounded adsb.lol request.
3. Confirm the Traffic row changes to `ONLINE` or `ONLINE+TCAS` and reports a
   nonzero count when covered live aircraft are within 100 NM.
4. Confirm yellow aircraft symbols, callsigns, relative altitudes, vertical
   trends, and track vectors update without pausing the simulator.
5. Confirm the map footer attributes traffic to `adsb.lol (ODbL)` while the
   online source is active.
6. Disconnect the network and confirm existing X-Plane TCAS traffic remains
   available as fallback without OpenEFB taking ownership of the TCAS system.

# RC24 X-Plane TCAS injection

1. Confirm Settings > `Inject online traffic into X-Plane TCAS` is disabled by
   default, then enable it while online traffic has a nonzero target count.
2. Confirm the Settings card reports `Active` and the number of online targets
   written to X-Plane TCAS.
3. In a TCAS-capable aircraft, confirm nearby targets appear on the cockpit
   traffic display with plausible bearing, range, relative altitude, and ID.
4. Disable injection and confirm OpenEFB immediately releases X-Plane traffic
   ownership while the EFB map traffic layer continues operating.
5. Enable another traffic plugin and confirm OpenEFB waits or yields instead of
   overriding it. After a yield, turn injection off and on to request access again.
6. Restart X-Plane and confirm the saved injection preference returns. Keep the
   feature disabled when using another traffic injector.
7. Confirm this stage does not create exterior 3D aircraft; it supplies cockpit
   TCAS targets only.

# RC25 clickable traffic details

1. Enable Home > Filter > Traffic and wait for yellow traffic aircraft.
2. Click one aircraft and confirm a compact, fully opaque card shows its
   callsign/registration, aircraft name or type, altitude, and speed.
3. With internet access, allow a few seconds for the selected target's route
   lookup. Confirm published departure and destination codes appear, or the
   card clearly reports that they were not published.
4. Click the same aircraft again and confirm the card closes.
5. Click another aircraft and confirm the card switches targets without
   opening a POI dialog or modifying the active FMS route.
6. Resize OpenEFB to its compact minimum and confirm the card stays inside the
   map, remains readable, and does not become transparent.
7. Disconnect the network and confirm traffic inspection remains safe: live
   altitude/speed still display while unavailable route data is not guessed.
