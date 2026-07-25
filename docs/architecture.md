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
