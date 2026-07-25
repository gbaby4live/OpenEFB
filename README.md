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

The M7 shell provides a dark tablet layout with a live UTC status bar and eight
mouse-selectable pages: Home, Flight Plan, Progress, Weather, Fuel, Aircraft,
Settings, and About. Window position and size are saved in X-Plane's
preferences directory. Home and Aircraft display
live aircraft identity, position, altitude, ground speed, heading, and vertical
speed sampled from X-Plane at 5 Hz. High-contrast text and an enforced minimum
window size keep telemetry cards legible without overlapping. Flight Plan reads
the active X-Plane FMS route once per second and highlights the leg currently
being flown. Weather displays the latest downloaded METAR for the first and last
airports in that route, refreshing every 15 seconds. Progress calculates direct
distance, true bearing, and estimated time to the active waypoint and destination
from live aircraft groundspeed. Fuel shows remaining mass, total engine burn,
endurance, and estimated range from X-Plane's live fuel system. The window
resource is created on first use and released whenever the plugin is disabled
or stopped.

Fuel flow is displayed in US gallons per hour using the standard avgas density
of 6.0 lb per US gallon. Remaining fuel mass stays visible in kilograms and
pounds because X-Plane reports fuel internally by mass.

See [docs/architecture.md](docs/architecture.md) for architectural boundaries
and threading invariants.
