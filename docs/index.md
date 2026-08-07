# Taiyin Documentation Index

Status: Current
Last reviewed: 2026-07-18

These documents describe Taiyin's current runtime capabilities, data formats, event search, and eclipse search APIs. Prefer documents marked as current when checking behavior. Maintainer-reference documents explain design background and known boundaries.

## Current Runtime Docs

These documents describe the implementation on `main`.

| Topic | Document | Code entry points |
| --- | --- | --- |
| Runtime architecture | [`ephemeris_runtime_architecture.md`](ephemeris_runtime_architecture.md) | `include/taiyin/runtime/runtime.h`, `include/taiyin/runtime/ephemeris_engine.h`, `src/runtime/` |
| Built-in semi-analytical ephemeris | [`semi_analytic_ephemeris.md`](semi_analytic_ephemeris.md) | `include/taiyin/internal/semi_analytic.h`, `src/semi_analytic.cpp` |
| C ABI and FFI entry points | [`c_api.md`](c_api.md) | `include/taiyin/c/taiyin.h`, `src/c_api/` |
| Catalog and segment cache | [`catalog_cache_model.md`](catalog_cache_model.md) | `include/taiyin/internal/ephemeris_catalog.h`, `include/taiyin/internal/ephemeris_segment_cache.h` |
| OPC persistent catalog | [`opc_catalog_format.md`](opc_catalog_format.md) | `include/taiyin/internal/opc_catalog_persistent.h`, `src/opc_catalog_persistent.cpp` |
| Event search | [`event_search.md`](event_search.md) | `include/taiyin/runtime/event_search.h`, `src/runtime/event_search.cpp` |
| Orbital events | [`orbital_events.md`](orbital_events.md) | `include/taiyin/runtime/orbital_events.h`, `src/runtime/events/orbital_events.cpp` |
| Equation of time and local solar time | [`solar_time.md`](solar_time.md) | `include/taiyin/runtime/solar_time.h`, `src/runtime/events/solar_time.cpp` |
| Chinese lunisolar calendar | [`chinese_calendar.md`](chinese_calendar.md) | `include/taiyin/chinese_calendar/calendar.h`, `include/taiyin/c/chinese_calendar.h`, `src/chinese_calendar/` |
| Sidereal zodiac and ayanamsha | [`astrology_sidereal.md`](astrology_sidereal.md) | `include/taiyin/astrology/sidereal.h`, `src/astrology/ayanamsha_models.cpp` |
| Astrology house foundations | [`astrology_houses.md`](astrology_houses.md) | `include/taiyin/astrology/houses.h`, `src/astrology/houses.cpp` |
| Lunar nodes and apsides | [`astrology_lunar_points.md`](astrology_lunar_points.md) | `include/taiyin/astrology/lunar_points.h`, `include/taiyin/astrology/targets.h`, `src/astrology/lunar_points.cpp` |
| Body phenomena | [`phenomena.md`](phenomena.md) | `include/taiyin/runtime/phenomena.h`, `src/runtime/phenomena.cpp` |
| Body magnitude models | [`phenomena_magnitude_models.md`](phenomena_magnitude_models.md) | `src/runtime/phenomena.cpp` |
| Eclipse search | [`eclipse_search.md`](eclipse_search.md) | `include/taiyin/runtime/eclipse_search.h`, `src/runtime/*eclipse*` |
| Lunar limb model | [`lunar_limb_model.md`](lunar_limb_model.md) | `include/taiyin/lunar_limb_tll1.h`, `src/runtime/lunar_limb.cpp` |
| Occultation search | [`occultation_search.md`](occultation_search.md) | `include/taiyin/runtime/occultation_search.h`, `src/runtime/occultation_search.cpp` |

## Audit And Limitation Notes

These documents describe validation coverage, precision boundaries, or intentional limits for specific modules.

| Topic | Document | Notes |
| --- | --- | --- |
| TSC1 v1 limits | [`tsc1_v1_known_limitations.md`](tsc1_v1_known_limitations.md) | Intentional limits of the current star-catalog format. |
| Project limitations | [`current_limitations.md`](current_limitations.md) | Current status and rescued planning context; verify against code before treating it as authoritative. |

## Maintainer Reference

These documents explain how the current design was reached. They are useful for understanding trade-offs, but current runtime docs and source code remain the source of truth for behavior.

| Topic | Document | Current reference |
| --- | --- | --- |
| Runtime cache design reference | [`runtime_cache_redesign.md`](runtime_cache_redesign.md) | Maintainer background; read `ephemeris_runtime_architecture.md` and `catalog_cache_model.md` first for current behavior. |

Older planning material lives in `plans/` and `plans/legacy/`. Treat it as historical background unless a current document links to it explicitly.

## Documentation Rules

- Public API changes should update the corresponding topic document.
- Runtime data, routing, or cache changes should update `ephemeris_runtime_architecture.md` or `catalog_cache_model.md`.
- Eclipse behavior changes should update `eclipse_search.md`.
- Temporary experiments, performance investigations, and internal comparison notes should stay out of user-facing documentation.
