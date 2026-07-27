# Product roadmap

OpenEFB targets the complete workflow of a modern simulator EFB, inspired by
Microsoft Flight Simulator 2024 while remaining native to X-Plane 12 and using
original open-source code and visuals.

## Delivered foundation

- M1-M2: plugin window, navigation, tablet shell, and saved geometry
- M3: live aircraft telemetry
- M4: active X-Plane FMS route
- M5: departure and destination METARs
- M6: route distance, bearing, and ETE
- M7: fuel quantity, GPH, endurance, and estimated range
- M8: bordered Home moving map with OpenStreetMap Street/Topo basemaps,
  route overlay, and zoom
- M9: interactive flight-plan creation and editing with X-Plane waypoint
  lookup, add/remove/reorder controls, draft safety, and explicit FMS Apply
- M10: offline airport browsing with runways, surfaces, COM frequencies, and
  installed SID, STAR, and approach names
- M11: switchable Home map overlays for endpoint weather, route airports and
  navaids, and installed OpenAIR polygons, circles, and arcs
- M12: live aircraft weight-and-balance summary, adjustable fuel reserve,
  trip-fuel margin, predicted landing weight, and dispatch outlook
- M13: combined flight briefing, interactive checklist, persistent notes, and
  route-driven airport archives with generated documents, FAA charts, and a
  user-provided local chart and aircraft-document library; revised with stable
  DEP/DEST filters, online-first weather fallbacks, installed navigation symbols,
  and confirmed map-to-FMS airport navigation
- M14: X-Plane `.fms` route import/export with departure-destination filenames,
  persistent high-contrast text, and comfort-size window preferences

## Active release hardening

- M15: shared raster display state, power-of-two PDF canvases, and visible map
  tile upload diagnostics across X-Plane graphics backends
- RC1: compatibility raster fallback, repeatable packaging, cross-platform core
  CI, and one complete simulator acceptance test before final release
- RC4: shared CPU-rendered BGRA surfaces presented through X-Plane-managed
  texture binding and fixed-function drawing under the Vulkan/Zink bridge
- RC5: flight-facing UI cleanup, separate Map/Live Data connection indicators,
  overlap-safe sizing, and scrollable width-fitted in-app PDF pages
- RC6: OpenStreetMap POI layers, hover identification, FMS route insertion,
  drag/pointer-centered deep zoom, aircraft recentering, opaque content layers,
  corrected FAA chart retrieval, and an all-PDF visible document workflow
- RC7: tile-safe high-contrast labels, consistent proportional map typography,
  one POI switch, compact airport/place information and route-action cards,
  zoom/departure/arrival/aircraft map controls, filled tactical buttons,
  bracket reticles, shared hover feedback, and a mini-airplane position symbol
- RC8: guaranteed opaque post-tile overlays, true airport-detail tile scaling,
  a Sky4Sim-inspired filter panel, optional aircraft/ALT-GS-HDG/route/label
  displays, and removal of unnecessary center guide lines
- RC9: Filter/Street/Topo-only map header and a unified XP-Career-inspired
  charcoal, green, cyan, yellow, filled-control, proportional-type visual system
- RC10: graphics-state-isolated shell/page/overlay/modal layers, contextual map
  actions, airport-detail handoff, map-to-FMS plotting, and collision-aware labels
- RC11: blend-disabled opaque control fills, state-strip buttons, filled sidebar
  navigation, and removal of thin outer framing that read as skeleton UI
- RC12: X-Plane texture-backed solid UI fills for deterministic button/card/page
  backgrounds across the Vulkan/Zink graphics bridge

## Planned operational workflow

- Cross-platform packaging, signed release artifacts, and final simulator validation

## Release hardening blockers

- Confirm raster OpenStreetMap/Topo tiles on all X-Plane graphics backends.
- Confirm the white in-app PDF page canvas and page contents on all supported platforms.

Licensed chart products and proprietary Microsoft assets will not be copied.
OpenEFB will use compatible public, user-provided, or separately licensed data
sources while preserving the same practical pilot workflow.
