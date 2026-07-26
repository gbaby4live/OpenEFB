# OpenEFB 1.0 acceptance test

This checklist is the release gate. A final version must not be published while
any required item is failing.

## Installation and lifecycle

- Install the packaged `OpenEFB` folder under `Resources/plugins`.
- Confirm X-Plane loads the plugin without an error in `Log.txt`.
- Open, close, resize, disable, and re-enable OpenEFB.
- Restart X-Plane and confirm the saved window and display settings return.

## Home and map

- Confirm live aircraft position, motion, fuel, route, and UTC data.
- Confirm Street and Topo basemaps are visibly different from the EFB background.
- Confirm online tiles become cached and remain visible after disconnecting.
- Exercise every overlay, zoom through the full range, and test airport direct-to confirmation.
- Select an attraction symbol or arbitrary map point and confirm coordinate direct-to reaches the FMS.

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
- Exercise Previous, Next, and Close and verify all page content is readable.
- Confirm notes persist and the checklist resets for a new plugin session.

## Release decision

Record X-Plane version, graphics backend, operating system, aircraft, and any
failed step. Map/PDF visibility failures are release blockers, not optional polish.
