# Ephemeris Runtime Architecture

Status: Current
Last reviewed: 2026-08-12
Primary headers: `include/taiyin/runtime/runtime.h`, `include/taiyin/runtime/ephemeris_engine.h`, `include/taiyin/runtime/native_context.h`, `include/taiyin/internal/ephemeris_segment_cache.h`

This document explains how the current Taiyin runtime discovers data, chooses an evaluation route, loads the segment cache, and completes one ephemeris calculation.

## Data Flow

```text
source paths / packaged OPC / custom descriptor
        |
        v
EphemerisBlockCatalog
        |
        v
BodyRegistry
direct bodies + composite fallback rules
        |
        v
NativeCalcContext route_rule_id
        |
        v
EphemerisRouteRuleTable  ----->  EphemerisEngine
                                         |
                                         v
                                  EphemerisSegmentCache
                                         |
                                         v
                             StorageEphemerisBlock -> eval_compiled_ephemeris_block()
```

`EphemerisBlockCatalog` stores descriptors discovered from OPC files, source directories, and user-added descriptors. It is the local data inventory. It is not a method-priority policy and not a runtime cache. The catalog builds internal indexes for descriptors so each request does not linearly scan the full inventory; see [Catalog And Cache Model](./catalog_cache_model.md) for index and segment-cache details.

An OPC entry caches SPK segment metadata, not an immutable product identity.
When an OPC catalog is loaded, SPK descriptors are reclassified from their
current paths using the runtime's current filename rules. This lets catalogs
written before named DE/satellite products were introduced acquire the current
source priorities and anti-mixing rules without rescanning each SPK kernel.
The independent `OPC_DISCOVERY_VERSION` also invalidates a catalog whenever a
discovery rule changes the descriptor set itself (for example, synthesized
Mars-center satellite routes); a rejected catalog is rebuilt by directory
discovery. See [OPC Catalog Format](./opc_catalog_format.md) for the version
and fingerprint checks.

`BodyRegistry` owns body-level routing capability:

```text
direct-capable bodies      discovered from catalog descriptors
fallback/composite rules   built-in rules, such as Earth/Moon from EMB + Moon/Earth
```

After catalog initialization or descriptor insertion, the runtime first marks direct-capable bodies in `BodyRegistry`, then registers built-in fallback/composite rules. This makes "can this body be computed directly?" and "does this body require composition?" explicit before data-source selection begins.

`EphemerisRouteRuleTable` owns direct-data method preference. Each rule has a numeric priority; higher priority is tried first, and equal priority preserves registration order. The current AUTO default is:

```text
440  JPL DE442 SPK
435  Taiyin DE442-rebuilt OPM2 (source id 2)
430  JPL DE441 SPK
420  JPL DE440 SPK
419..398  recognized historical JPL DE SPKs, from DE438 through DE102
397..390  recognized JPL satellite SPK fallbacks
389  other SPK
300  Taiyin prerelease OPM2 (source id 1)
290  other OPM2
250  built-in semi-analytical model
200  TKC1
100  Kepler file
```

Built-in route-rule ids cover:

```text
AUTO          automatic selection by default priority
SPK-only      SPK only
OPM2-only     OPM2 only
semi-analytical-only  built-in semi-analytical model only
```

Users can register their own route-rule table during setup and select it through `NativeCalcContext`. `NativeCalcContext` stores the resolved table pointer, so route rules should be treated as read-only after initialization. If a different strategy is needed, register a new route-rule id instead of mutating a table that a context may already hold.

Custom methods and custom file methods enter the catalog after registration and are inserted into the AUTO route rule with their supplied priority. Extensions do not need to bypass the runtime: as long as descriptor target/center/method/source semantics are correct, the method can participate in routing together with built-in SPK, OPM2, and semi-analytical sources.

When `EphemerisEngine` evaluates a direct request, it resolves method ids, queries the catalog for descriptors matching method/route/JD according to the route rule, then selects a descriptor. The engine checks the segment cache first; on cache miss, it loads the descriptor bucket and evaluates the compiled block.

### Source Product Identity

Container format and source product are deliberately separate. A descriptor's
format says how to load it (`SPK` or `OPM2`); its `source_id` keeps named JPL
products distinct, preventing an AUTO composite state from silently combining,
for example, Earth from DE441 with Moon from DE442.

For raw JPL SPK files, Taiyin recognizes a deliberately finite set of named
JPL model products: historical `DE` models (including DE102 through DE442),
MAR099, JUP347/348/349/365, SAT415/441/455–459/480, URA117/182/184,
NEP097/098/104/105, and PLU060. Product suffixes such as `s`, `xl`, and
`_part-1` remain part of the same model identity, so `nep098_part-2.bsp` is
recognized as NEP098. This is intentionally a filename convention, not file
authentication: callers who rename data take responsibility for that
declaration.

OPM2 does not infer provenance from its filename. Its little-endian container
header stores a `uint32 source_id` at bytes 24–27:

```text
0  undefined; old version-1 files map to prerelease for compatibility
1  TAIYIN_PRERELEASE
2  TAIYIN_DE442_REBUILT
```

Future Taiyin OPM2 products receive increasing ids. Within the OPM2-only
provider, the greater id is preferred. Unknown positive ids remain usable; an
application that installs arbitrary third-party ids is responsible for their
ordering and may override individual files explicitly.

Any unrecognized SPK or OPM2 product remains usable through the generic
`SPK-only` or `OPM2-only` route, and is also considered by AUTO below the
recognized built-in products.
Named JPL SPK rules also accept non-DE SPK files as **satellite auxiliary**
routes only: this permits a satellite kernel such as `mar099.bsp` to compose
with a named DE planetary kernel, while a named DE descriptor remains preferred
when it exists for the same edge. A successful composite under a named-DE rule
must use at least one descriptor from that named DE source; a self-contained
satellite kernel cannot impersonate an absent higher-priority DE product.
Other recognized DE products are never
treated as auxiliaries, so the runtime cannot silently mix DE441 and DE442.
Below all named-DE rules, each recognized satellite product has an exact-source
fallback. This keeps direct product edges such as `505 -> 599` usable when no
DE composition is needed, without introducing a wildcard that could mix two DE
products.
OPM2 products never use this auxiliary exception.
No new OPM container version or catalog format is needed for the convention.

### Selecting Multiple Files In One Route

When more than one file covers the same direct route, Taiyin first applies the
route-rule product/method priority above. It then applies the provider's model
policy. SPK uses a finite, hard-coded model table. Its principal choices are
DE442 > DE441 > DE440 > …,
MAR099 for Mars satellites, JUP365 for the regular Jovian system, SAT441 for
the regular Saturnian system, URA182 for the main Uranian system, NEP098 for
the current Neptune product, and PLU060 for Pluto. The other named satellite
solutions are retained as lower-priority alternatives; an irregular satellite
only present in one product naturally selects that product by coverage.

Taiyin intentionally does not apply a generic `max(number)` rule. For example,
`jup365.bsp` and `jup349.bsp` are distinct production lines rather than a
single monotonically numbered release sequence, while `2000001.bsp` is an
asteroid identifier. Unknown small-body/Horizons SPKs keep discovery order
unless the application overrides them explicitly. OPM2 instead compares its
header `source_id` numerically, so a newly assigned product does not require a
filename rule.

Applications can override this provider-level product choice at setup time with
`taiyin_runtime_set_ephemeris_source_priority(path_or_basename, priority)`.
An exact path wins over a basename rule, and higher explicit values win. An
explicit value replaces that file's provider-default numeric value on the same
scale: OPM2 defaults to its header `source_id`, while named SPK products use the
documented internal model table (for example JUP365 is 800 and JUP349 is 700).
This permits both promotion and demotion. Unknown unconfigured files default to
zero. AUTO applies the override across source-specific product rules belonging
to the same provider, while the route table still owns provider/method slots
and fallback boundaries; an SPK override therefore cannot select an SPK while
an OPM2-only route is active. Rules are read at selection time, so updating one
affects already discovered catalog entries on their next calculation; configure
them at setup time rather than concurrently with calculation. Exact paths and
basenames follow the host filesystem's usual case convention: comparisons are
case-insensitive on Windows and case-sensitive on POSIX systems, so two
case-distinct POSIX paths can be overridden separately.

```c
/* Promote JUP349 over its JUP365=800 default competitor. */
taiyin_runtime_set_ephemeris_source_priority("jup349.bsp", 900);

/* Demote it, then remove the override and restore the provider default. */
taiyin_runtime_set_ephemeris_source_priority("jup349.bsp", 600);
taiyin_runtime_clear_ephemeris_source_priority("jup349.bsp");
```

`taiyin_runtime_clear_all_ephemeris_source_priorities()` removes the complete
user overlay. Setting a priority to zero is not the same as clearing it: zero
is an explicit priority and is useful for placing a file below positive
built-in products.

The runtime intentionally does not retain a shared exact-JD numeric-result cache.
Event searches and route products usually evaluate distinct JDs, so a global
state/matrix cache would churn low-reuse entries under a writer lock. Reuse is
instead limited to loaded source segments and request-local same-JD evaluation
state.

## Runtime State

`Runtime` owns:

```text
EphemerisBlockCatalog
EphemerisSegmentCache
EphemerisBodyRegistry
EphemerisRouteRule tables
EphemerisEngine
EarthOrientationTable
TLL1 lunar-limb mapping
```

Global runtime configuration and mutation are protected by the runtime reader/writer lock. EOP snapshot lookup uses the read side of that lock, and a replaced EOP snapshot remains alive until the owning `Runtime` is destroyed so a previously borrowed immutable pointer cannot dangle. Callers must still treat EOP/TLL1 replacement and runtime reinitialization as setup-time operations rather than run them concurrently with calculations. Descriptor loading uses `RouteInflightMap` to avoid repeating loads for the same `EphemerisSegmentCacheKey`.

`EphemerisRuntimeConfig` controls global runtime initialization:

```text
segment_cache_max_entries
source_paths / source_path_count
data_root
eop_path / load_builtin_eop
lunar_limb_path
load_packaged_data
strict_discovery
```

`initialize_global_ephemeris_runtime()` rebuilds the catalog, segment cache, body registry, and engine binding, and replaces global EOP/TLL1 data. The leap-second table is immutable process-wide built-in data. User `NativeCalcContext` objects do not own raw pointers to these tables. `add_global_ephemeris_source_path()` appends discovered descriptors and rebuilds the body registry. `clear_global_ephemeris_cache()` clears compiled segments; this also releases their OPM2 source mappings. The next OPM2 cache miss maps its source again on demand.

`get_global_registered_data_sources()` exposes the successfully published
inventory as structured records. Ephemeris descriptors are aggregated by
physical source and format, while EOP and TLL1 appear as their own source
kinds. The reported JD range is the source's coverage envelope rather than a
promise that every route covers every instant inside it. Built-in and in-memory
sources use stable labels and flags instead of pretending to have file paths.
Because runtime replacement is transactional, a failed initialization leaves
both the prior catalog and its reported inventory intact.

`eop_path` names a finals2000A text file and takes precedence over
`load_builtin_eop`. When neither is set, precise UTC has no EOP and
`TimeScaleAuto` may fall back to Delta T under its existing policy. An empty
`lunar_limb_path` loads no limb model. Setup code may also install a deep EOP
copy through `set_global_earth_orientation_table()` or replace the limb mapping
through `load_global_lunar_limb_model()`; neither operation may run concurrently
with calculations.

## Calculation Path

```text
user request: body / center / JD / flags
        |
        v
NativeCalcContext supplies route-rule id and observer/model settings
        |
        v
BodyRegistry decides direct vs composite
        |
        v
direct request looks up descriptor by route rule
        |
        v
segment cache / source payload
        |
        v
raw Cartesian state
        |
        v
apparent / observed / output frame transform
```

Method priority is resolved before catalog lookup. Cache is an accelerator after a catalog entry has been selected; it is not the route selector.

## `center_id` And `observer_id`

`NativeCalcContext` contains both `center_id` and `observer_id`. These fields should not be read as the same "viewing center":

```text
center_id    common origin used to evaluate target and observer states
observer_id  body or observer location from which the target is finally seen
```

Ordinary apparent/native position calculation first obtains, in the same `center_id` frame:

```text
target -> center
observer -> center
```

The apparent pipeline then forms:

```text
observer -> target = (target -> center) - (observer -> center)
```

So `center_id` is the ephemeris evaluation origin, route-composition origin, or intermediate calculation origin. It is not the final observer. The final viewing origin is determined by `observer_id` and any topocentric observer offset.

For example:

```text
center=Sun, observer=Earth, target=Mercury
```

means the runtime may evaluate `Mercury/Sun` and `Earth/Sun`, then derive the apparent `Earth -> Mercury` vector. Similarly:

```text
center=Sun, observer=Earth, target=Sun
```

has an intermediate `Sun/Sun` zero state, but the final apparent vector is still:

```text
Sun/Sun - Earth/Sun = Earth -> Sun
```

This is why `center_id` may differ from `observer_id`. In exact arithmetic, if every body can be transformed through every origin, `center=Sun`, `center=SSB`, or another common origin would yield the same observer-target line of sight. In practice, data availability, route fallback, coverage, and performance differ. The built-in semi-analytical model naturally provides Sun-centered planetary states, so some high-level events deliberately choose a Sun-centered evaluation origin to use that route reliably.

## Built-In Method Center Conventions

Descriptors preserve the source method's true target/center identity. Body-centered ephemerides, barycenter ephemerides, SSB-centered data, and Sun-centered data are distinguished by their original semantics in the runtime.

SPK descriptors are read from SPK segment summaries. `target_id` and `center_id` are the values declared by the BSP segment. Discovery can also expose derived same-center relative routes when a preferred center exists in the same kernel, for example `target/SSB - Sun/SSB => target/Sun`.

Packaged OPM2 major-body descriptors currently use mixed centers:

```text
Sun / SSB
Mercury barycenter / Sun
Venus barycenter / Sun
EMB / Sun
Moon / Earth
Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / SSB
COB slices such as Uranus body / Uranus barycenter
```

Body fallback maps user-facing NAIF body ids onto these routes: Mercury/Venus body ids alias to their barycenters; Earth/Moon use EMB plus the Moon/Earth mass ratio; Mars through Pluto body ids need body/barycenter COB offsets when the selected method only provides barycenter data. Natural-satellite IDs in NAIF's permanent `PNN`, extended permanent `P0NNN`, and provisional `P5NNN` forms compose their satellite-to-planet edge through the corresponding physical `P99` primary, including satellites without named Taiyin constants. Native position calls stay strict by default when those COB offsets are missing; `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` is the explicit opt-in that lets Mars through Pluto retry the matching barycenter as an approximation, with `component_target_id` recording the barycenter used.

That approximation is intentionally strict-first. Each call first attempts the requested physical body route, then retries the barycenter only after a route, coverage, or component failure. This keeps future body/COB data from being bypassed silently. If barycenter approximation becomes a hot path for event search or dense tables, the likely optimization is a route-level approximation decision cache or a separate direct-barycenter mode; the current API does not cache this decision.

The built-in semi-analytical model is registered into the runtime AUTO route table when packaged data loading is enabled. Its center convention is:

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto / Sun
Sun / SSB
Moon / Earth
Earth body / Sun
```

The Earth block is not SSB-centered. The implementation accepts `Earth/Sun` (`399/10`) and constructs it from `EMB/Sun` plus the mass-ratio-scaled `Moon/Earth` vector. Runtime descriptors use the same target/center ids. The explicit route id is `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`.

## Current Boundaries

OPM2 cache bucket selection follows its natural Chebyshev segment grid. `compile_opm2_ephemeris_data_for_range()` slices the loaded grid to the selected segment range. The current implementation parses the source payload before slicing; this preserves correct semantics and leaves room for later IO optimization.

File residency is format-specific. TLL1, TSC1, TKC1, persistent OPC catalogs,
and uncompressed OPM2 sources use the internal read-only `MappedFile`
abstraction when the platform mapping APIs succeed. OPM2 source indexes keep a
weak reference, while compiled segments sharing that source keep the mapping
alive. The final segment eviction, failed fallback attempt, explicit cache
clear, or runtime replacement therefore releases the mapping. Gzip input and
mapping failures use an owned whole-file buffer only while the requested
compiled segment is built. Compiled OPM2 segments copy the selected
coefficients and do not borrow bytes from the source mapping. SPK file sources retain the path and read requested ranges into
temporary buffers, while their index and selected compiled blocks remain
resident. Thus “mmap-backed runtime” is not a property of every ephemeris
format. A mapped view is demand-paged by the operating system and should not be
interpreted as immediately resident RSS for the complete file.

SPK, TKC1, and custom file methods use descriptor/cache policies produced by discovery. Future work can refine bucket granularity for specific formats, but those policies should not be folded back into catalog ordering or route rules.

TSC1/TSF1 star catalogs do not use this main ephemeris route selector. High-level star APIs use a separate star provider/store. This boundary is intentional: star ids, name lookup, and solar-system route policy should not be mixed.
