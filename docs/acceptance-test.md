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

# RC26 traffic range and provider resilience

1. Open Home > Filter and scroll to `ONLINE RANGE`. Use `-` and `+` to verify
   the value moves in 25-NM steps and remains bounded between 25 and 200 NM.
2. Select 25 NM, restart X-Plane, and confirm the saved range returns. Repeat at
   100 NM before continuing normal testing.
3. With internet access, confirm the Source row reports `ADSB.LOL ONLINE` and
   the configured radius while the Traffic row reports ONLINE or ONLINE+TCAS.
4. Disconnect the network. Confirm the filter reports a retry/degraded state,
   retains only the recent online picture temporarily, and does not freeze the
   simulator.
5. Keep the network disconnected for longer than 90 seconds. Confirm stale
   online aircraft disappear and available X-Plane TCAS traffic remains.
6. Reconnect the network and allow the displayed retry interval to elapse.
   Confirm ONLINE service and normal 15-second refresh recover automatically.
7. Rapidly open and close several traffic cards and change range repeatedly.
   Confirm requests remain bounded, the UI stays responsive, and TCAS ownership
   behavior from RC24 remains unchanged.

# RC27 route, forecast, traffic-alert, and responsive UI

1. Add two map coordinates. Confirm both stay before the destination and the
   X-Plane FMS advances through them in order.
2. Select an added waypoint again, choose Remove, and confirm navigation
   continues to the next waypoint. Confirm route endpoints show `ROUTE END`.
3. Select an airport marker and confirm its code and full name appear.
4. Switch Weather between CURRENT and FORECAST. Confirm the view visibly changes
   between current METAR and the published TAF, with separate source labels.
5. Compare the `ALT` and `HDG` values with the pilot cockpit instruments.
6. On X-Plane 12.4.1 or newer with an equipped aircraft, enable injection and
   confirm nearby Mode 7 targets can generate native TCAS traffic advisories.
7. Resize OpenEFB to minimum height and mouse-wheel every non-map page. Confirm
   hidden content is reachable, remains clipped inside the EFB, and does not overlap.
8. Confirm compact Briefing Library controls reflow into two rows and chart
   availability text remains readable.

# RC28 exterior traffic, FAA charts, and settings

1. Enable traffic injection and `Moving exterior 3D traffic` in Settings. With
   online targets nearby, confirm up to 12 recognizable regional-jet models move in the
   exterior world within 25 NM and cockpit TCAS remains operational. Confirm the
   Settings status reports the active model count, model source, and nearest target
   distance. If X-Plane's installed model is unavailable, confirm the OpenEFB
   lightweight fallback still appears.
2. Disable exterior 3D traffic and confirm the models disappear while map and
   cockpit traffic continue. Re-enable it and confirm the models return.
3. Disable TCAS injection or allow another cooperative plugin to take ownership.
   Confirm OpenEFB's enabled exterior models remain visible while its cockpit
   injection status changes independently.
4. Confirm the Home aircraft strip reads `ALT`, `GS`, and `HDG` without `IND`,
   and no characters extend outside its dark panel at minimum width.
5. Change aircraft-strip visibility, route visibility, map labels, and marker
   size in Settings. Restart X-Plane and confirm each choice persists.
6. Load a U.S. departure and destination, select Briefing > Library, and press
   Refresh. Confirm current-cycle FAA chart PDFs appear for airports published
   in d-TPP and the old status PDF disappears.
7. If an FAA download fails, open `Chart Download Status.pdf` and confirm it
   identifies the airport, cycle, and HTTP/download result rather than claiming
   only that coverage is U.S.-limited.
8. Confirm non-ICAO domestic identifiers can match FAA records and deletion
   placeholder records are not downloaded as charts.
9. Drag `Magenta route thickness` from 2 px to 12 px. Confirm the route updates
   immediately, remains readable over the map, and restores after restarting X-Plane.
10. Place or observe airborne traffic within 3 NM and 1,200 vertical feet. Confirm
    OpenEFB displays a traffic callout and, when X-Plane text-to-speech is enabled,
    speaks clock position, distance, and relative altitude. Confirm it does not
    repeat continuously. Treat this as an OpenEFB warning, not an ATC clearance.

# RC29 approach control and plates

1. Load a route whose first and last airport entries have installed X-Plane
   CIFP data. Select Flight Plan > Edit, then choose Approach inside Flight Plan
   Builder and confirm Destination loads by default.
2. Switch between DEP and DEST. Confirm each airport name/code and the complete
   installed approach list appear, and use the mouse wheel to reach every entry.
3. Select an approach. Confirm the opaque premium panel shows procedure type,
   runway, common fixes, and a vectors/common selection without changing the FMS.
4. Double-click an approach and confirm its transition picker opens immediately.
   Use the wheel to reach every published transition, choose one, and confirm the
   picker closes with the selected transition in the preview. Confirm there is no
   Transition button at the bottom.
5. Select Plate at a U.S. airport. If the plate is not local, wait for the refresh
   message and select Plate again. Confirm the matching approach PDF opens inside
   OpenEFB and scrolls with the mouse wheel.
6. Select Apply while parked. Confirm X-Plane shows the selected approach,
   transition, and runway as a procedure rather than only loose waypoints.
7. Repeat while airborne. On compatible stock avionics, activate the approach
   and confirm its altitude constraints are available to VPATH. Do not expect
   OpenEFB to control a third-party aircraft's private FMC modes.
8. Apply a different approach without externally editing the route. Confirm the
   previous OpenEFB approach sequence is replaced instead of duplicated.
9. Attempt a procedure containing an unresolved fix or one that would exceed
   100 FMS entries. Confirm Apply refuses the change and shows a useful message.
10. Resize OpenEFB to its minimum dimensions. Confirm the approach list, selected
    state, controls, and messages remain clipped inside the opaque panel.
11. Load a route with more than five FMS entries. Confirm Flight Plan immediately
    lists the complete route and use the page wheel/scroll rail to reach every leg.
12. With the intentionally truncated FAA catalog from an older build present,
    select Refresh. Confirm OpenEFB discards it, downloads a complete catalog,
    and replaces `Chart Download Status.pdf` with the airport's actual plates.
13. Open a downloaded FAA approach plate while the aircraft is inside its
    published geographic extent. Confirm a blue airplane follows live position
    and true heading on the chart while the mouse wheel scrolls the page. Confirm
    the badge shows aircraft altitude and the active approach constraint when
    X-Plane publishes one. Open a non-georeferenced PDF and confirm no aircraft
    marker is guessed.
14. Resize OpenEFB to minimum height. Confirm the Briefing page rail reaches the
    bottom of every tab, and the chart list has its own visible scroll thumb.
15. Confirm the compact top control reads `Menu`, and Planning plus generated
    briefing documents show weight and fuel mass only in pounds.
16. Click one or more procedure fixes in the preview and confirm they change to
    `REMOVED`; click again to restore one. Apply and confirm both OpenEFB and the
    X-Plane FMS reflect the edited sequence and continue to the next retained fix.
17. Apply an unedited procedure and confirm X-Plane no longer warns that the plan
    was saved with AIRAC cycle 0. Inspect the generated header if needed and
    confirm it matches X-Plane's active navigation cycle.
18. Open Briefing > Checklist. Select Preflight, Takeoff/Cruise, and
    Descent/Landing; verify each has its own offline items and displays the current
    aircraft name. Confirm Reset affects the active phase.
19. Open Logbook and click a completed flight. Confirm a solid detail panel shows
    route, aircraft, completion time, airborne duration, distance, maximum
    altitude/speed, climb/descent rates, landing rate, and track-point count.
20. On a white FAA chart, confirm the altitude/constraint badge uses dark legible
    text and remains inside its box. Confirm the blue ownship silhouette has no
    shifted color behind its wings or fuselage.
21. At KSAN, choose ILS RWY 09 Y or Z and the MZB transition. Confirm the preview
    contains MZB, both sequence-distinct GATTO legs, SARGS, and the final legs.
    Apply and confirm X-Plane accepts `RW09` without reporting a nonexistent
    `RW09-Y` or `RW09-Z` runway.
22. After applying the approach, confirm Approach Control closes back to Flight
    Plan Builder and every navigable transition/final fix appears before the
    destination. Select a fix, choose Remove, and Apply the Builder again.
23. Confirm the removed fix disappears from both the X-Plane FMS and OpenEFB.
    Select `Flight Plan` (formerly Cancel) and confirm the overview shows every
    remaining route and approach leg, with mouse-wheel scrolling for long plans.
