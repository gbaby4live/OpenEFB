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

The next milestone is an in-simulator shell: a menu command, window ownership
abstraction, XPLM window lifecycle, and blank EFB surface.
