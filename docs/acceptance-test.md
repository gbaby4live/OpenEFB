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
- Exercise every overlay and zoom from 320 NM down to 0.02 NM at an airport.
- Drag the map, zoom around an off-center pointer, and confirm HOME returns to the aircraft.
- Hover Food, Golf, and Sights icons and confirm the opaque card identifies each category.
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
