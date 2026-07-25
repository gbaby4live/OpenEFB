# OpenEFB

OpenEFB is an open-source electronic flight bag plugin for X-Plane 12.

## Build the core and tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Build the X-Plane plugin

Point CMake at an extracted X-Plane SDK:

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

See [docs/architecture.md](docs/architecture.md) for architectural boundaries
and threading invariants.
