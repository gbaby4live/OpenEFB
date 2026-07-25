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

The M2 shell provides a dark tablet layout with a live UTC status bar and four
mouse-selectable pages: Home, Aircraft, Settings, and About. Window position and
size are saved in X-Plane's preferences directory. Its window resource is
created on first use and released whenever the plugin is disabled or stopped.

See [docs/architecture.md](docs/architecture.md) for architectural boundaries
and threading invariants.
