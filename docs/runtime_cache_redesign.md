# Runtime Cache Design Reference

Status: Maintainer reference
Last reviewed: 2026-08-04
Read current behavior first:

- [Ephemeris Runtime Architecture](./ephemeris_runtime_architecture.md)
- [Catalog And Cache Model](./catalog_cache_model.md)

This document explains why the runtime/cache design converged to its current shape and which optimization directions remain open.

## Background

Early runtime drafts pushed too many responsibilities into one generic block cache:

```text
source discovery
descriptor catalog
method priority
route selection
JD bucket selection
compiled data cache
eviction scoring
diagnostics
```

The problem was both cognitive cost and hot-path cost. A simple position computation could trigger route selection, file lookup, bucket slicing, data loading, eviction management, and diagnostics assembly at once. The later refactor split those responsibilities so each layer does one job.

## Current Layers

The current implementation separates responsibilities into these layers:

```text
BodyRegistry
  Determines whether a body can be computed directly or needs composite/fallback rules.

EphemerisRouteRuleTable
  Owns direct-data method priority, such as AUTO / SPK-only / OPM2-only / semi-analytical-only.

EphemerisBlockCatalog
  Local data inventory. Answers whether a descriptor covers target/center/method/frame/JD.

EphemerisSegmentCache
  Caches loaded and compiled data segments for source formats.

```

Typical computation path:

```text
NativeCalcContext
  -> route-rule id / model / observer settings
  -> BodyRegistry direct or composite
  -> EphemerisRouteRuleTable method order
  -> EphemerisBlockCatalog descriptor candidates
  -> cache_policy selects bucket for JD
  -> EphemerisSegmentCache hit/load
  -> eval compiled block
  -> apparent / observed / frame pipeline
```

The catalog is still on the active runtime lookup path, but it is now an indexed data inventory. It no longer owns method priority and is not a substitute for cache.

## Design Evolution

The current implementation has moved away from the early large block-cache plan. Main changes include:

- runtime dispatch moved from a generic `EphemerisBlockCache` to a combination of route rules, catalog, and segment cache;
- cache capacity policy moved from mixed byte-size/reload-cost/priority scoring to fixed entry counts and simple eviction;
- method priority moved from catalog insertion order to explicit `EphemerisRouteRuleTable`;
- route selection and cache hit are separate; cache only means a data segment is already loaded;
- diagnostics were removed from the normal hot path, so ordinary computation uses lightweight lookup first;
- cache keys keep structured identity, with hash only as a lookup accelerator;
- catalog queries return descriptor copies, avoiding dangling pointers when concurrent catalog growth reallocates vectors.

Current code uses descriptor-copy mode:

```text
catalog.get(index, out)
catalog.find_method_candidates(query, method, out_vector)
EphemerisSelectionResult stores a descriptor copy
```

This prevents computation paths from holding dangling pointers if the catalog grows or descriptors are appended concurrently.

## Segment Cache

`EphemerisSegmentCache` keys are structured identities, not single hashes:

```text
kind
target_id
center_id
method_id
frame
source_key
item_id
```

The hash accelerates lookup; equality uses the full key. Even if a hash collision occurs, the cache will not return the wrong data.

Segment-cache values are compiled runtime artifacts, for example:

```text
OPM2 compiled segment
SPK segment/kernel-backed compiled block
Kepler elements block
TSC1 star provider internal compiled evaluator
```

The cache uses a fixed entry count rather than a complex byte budget. Eviction stays simple, and reads/writes are protected by a writer-preferred lock. Callers access payloads through `with_data()` while a read lock is held, avoiding continued use of raw pointers after eviction.

## Cache Policy

Each descriptor's `cache_policy` explains how to cut a source descriptor into buckets:

```text
CacheWholeEntry
CacheFixedSpan
CacheNaturalSegment
```

OPM2 uses its natural Chebyshev segment grid:

```text
origin_jd
span_days
first_index
count
```

Given a JD, the runtime computes a bucket descriptor with `make_cache_bucket_descriptor_for_jd()`. OPM2 currently selects buckets according to the source file's Chebyshev segment grid.

SPK, TKC1, and TKE1/custom Kepler files use cache policies written by their discoverers. Format-native segmentation belongs in discovery/loader layers. Route rules and catalog priority only select data routes.

## Numeric Evaluation Reuse

Taiyin does not retain a shared exact-JD numeric-result cache. Such keys have
low reuse during searches and route generation, but every miss contends on a
global writer lock. Source-segment reuse remains in `EphemerisSegmentCache`;
individual solver/request objects may retain same-JD values locally where that
is demonstrably useful.

## Source Index And File Data

The catalog also has `source_indexes_`. It lets multiple descriptors from one source file share parsed source payload, for example:

```text
multiple route/bucket descriptors from the same OPM2 file
multiple segment descriptors from the same SPK kernel
multiple object descriptors from the same TKC1 catalog
```

This is not a route selector and not a global file cache. Whether file pages are resident is left to the OS page cache. Taiyin only caches parsed data structures that the runtime actually uses.

The current implementation has an important format distinction: TLL1, TSC1,
TKC1, persistent OPC catalogs, and uncompressed OPM2 sources use `MappedFile`
where available. The OPM2 catalog keeps only weak references to mapped source
views; cached segments hold the strong owner while any segment from that source
remains resident. Owned or decompressed fallback buffers are transient and are
released after a requested segment has been compiled. Compiled OPM2 blocks own
their selected coefficients, so they do not borrow from the mapping. Final
segment eviction, failed route fallback, explicit cache clear, and runtime
replacement release the corresponding mapping; a later miss maps the source
again. SPK file sources continue to use range reads from the path. The
segment cache therefore does not imply that every source file is mapped or
that a mapped file is fully resident in physical memory.

## Route Rules And Custom Methods

Method priority is expressed by `EphemerisRouteRuleTable`. Built-in route-rule ids include:

```text
AUTO
SPK-only
OPM2-only
semi-analytical-only
```

Users can register new route-rule tables during setup and select one from `NativeCalcContext`. `NativeCalcContext` stores a resolved table pointer, so route-rule tables should be treated as read-only after registration. Register a new id when a new strategy is needed.

Custom methods and custom file methods enter the catalog after registration and can be inserted into AUTO with a priority. Extension methods need to clearly declare target/center/method/source identity so catalog lookup and segment-cache reuse remain consistent.

## Optimization Directions

Runtime/cache optimization can continue along these directions:

- avoid parsing the complete OPM2 payload before slicing a segment;
- add component-aware OPM2 evaluation so SPEED does not calculate a second derivative;
- bucket SPK more finely by kernel natural segment;
- reduce repeated Sun/Moon/frame evaluation at the same JD inside event search and eclipse solvers;
- add a local cursor for continuous JD scans, with the cursor storing only key/range rather than raw data pointers that may be evicted;
- introduce mmap or persistent kernel handles for selected formats to reduce repeated source open/parse work;
- add finer performance diagnostics while keeping the route rule, catalog, and segment-cache split clear;
- provide clearer cache-capacity guidance for long-range searches and batch chart computations.

## Related Documents

For current implementation details, read:

```text
ephemeris_runtime_architecture.md
catalog_cache_model.md
opc_catalog_format.md
```

This document explains design background and optimization directions. Concrete runtime behavior is defined by the current documents above and the source code.
