# Catalog And Segment Cache Model

Status: Current
Last reviewed: 2026-07-01
Primary headers:
`include/taiyin/internal/ephemeris_catalog.h`,
`include/taiyin/internal/ephemeris_segment_cache.h`,
`include/taiyin/runtime/ephemeris_engine.h`

This document describes the boundaries between the catalog and segment cache in the Taiyin runtime:

```text
Catalog            what data exists locally, where it is, and what time span it covers
SegmentCache       whether a natural segment from a source has been compiled into memory
```

```text
OPC / directory discovery  ->  EphemerisBlockCatalog
disk index or file scan        descriptor/path records

EphemerisBlockCatalog      ->  BodyRegistry
descriptor inventory           direct-capable body marks

NativeCalcContext          ->  EphemerisRouteRuleTable
route_rule_id                  resolved immutable route-rule table

EphemerisEngine           ->  Catalog method page       -> descriptor copy
request evaluation            route/method lookup          source inventory

EphemerisEngine           ->  EphemerisSegmentCache    -> compiled storage blocks
selected descriptor            fixed-entry cache          format artifacts

```

## Descriptor Catalog

`EphemerisBlockCatalog` stores source descriptors:

```text
target_id
center_id
method_id
frame
format
jd_tdb_start / jd_tdb_end
source_key
path
route_key
cache_policy
```

The catalog is the source-of-truth map for returning to a file or registered source. If runtime cached data is evicted, the descriptor still contains the information needed to reload that segment.

The catalog also has two internal indexes:

```text
method_pages_:
  hash(target_id, center_id, frame, method_id)
    -> one or more MethodPage
       -> descriptor indexes

source_indexes_:
  source_key
    -> EphemerisSourceIndex(path / format / optional parsed payload)
```

`find_method_candidates()` uses method pages to narrow the candidate set, filters by JD coverage, and returns descriptor copies. Callers do not hold raw pointers into catalog vectors, so catalog writes that grow a vector cannot leave an already-started reader with a dangling address.

Catalog order is not method policy. The current split is:

```text
BodyRegistry             -> which bodies are direct-capable, which need fallback/composite rules
EphemerisRouteRuleTable  -> direct method preference, selected by context
Catalog                  -> local data inventory
SegmentCache             -> loaded runtime segments
```

Design background is in the maintainer reference `runtime_cache_redesign.md`.

## Route Rules And Context

`NativeCalcContext` stores a `route_rule_id` and a pointer to the resolved `route_rules` table. Route-rule tables are built during runtime initialization or registration and are used as immutable tables during computation.

Built-in route rules include:

```text
AUTO     SPK -> OPM2 -> semi-analytical -> TKC1 Kepler -> Taiyin Kepler file
OPM2     OPM2 only
SPK      SPK only
SEMI     built-in semi-analytical only
```

If the context does not specify a route rule, `EphemerisEngine` uses the runtime's default route-rule table. When a non-AUTO rule is selected, only methods in that rule table are tried; there is no cross-method fallback.

## Segment Cache

`EphemerisSegmentCache` stores loaded runtime artifacts:

```text
EphemerisSegmentCacheKey {
  kind,
  target_id,
  center_id,
  method_id,
  frame,
  source_key,
  item_id
}
EphemerisSegmentCacheData { void* data, destroy_fn }
```

The cache has a fixed maximum entry count. It does not track byte budgets, reload cost, frequency weights, or route-rule priority. Current eviction uses a simple clock policy.

In the ephemeris-engine path, the cache key `kind` comes from descriptor `format`, and `item_id` comes from the bucket descriptor's `route_key.bucket_id`:

```text
CacheWholeEntry      bucket_id = 0
CacheNaturalSegment  bucket_id = natural segment index
CacheFixedSpan       bucket_id = floor((jd - origin) / span)
```

OPM2 uses the natural segment policy, corresponding to the Chebyshev segment index. SPK, TKC1/Kepler, custom method, and similar formats use the `cache_policy` produced by their discoverers. The TSC1 star provider also reuses the `EphemerisSegmentCache` type, but it does not use the main ephemeris descriptor catalog. It uses the provider's internal star runtime id as its cache identity.

Callers evaluate cached entries through `with_data()`. It holds a read lock while the cached pointer is in use, preventing the data from being evicted during computation.

On a segment-cache miss, `EphemerisEngine` follows this path:

```text
descriptor
  -> make_cache_bucket_descriptor_for_jd()
  -> make_method_cache_key()
  -> SegmentCache.with_data()
       hit: eval
       miss:
         RouteInflightMap acquire
           loader: load_descriptor_ephemeris_block()
                   SegmentCache.insert()
                   with_data() eval
           follower: wait / retry with_data()
```

`RouteInflightMap` prevents multiple threads from loading the same segment at the same time. It does not change cache keys and does not participate in route selection.

## What Is Not Cached Here

`SegmentCache` does not own the persistent catalog, source paths, OPC contents, or discovery results. It only owns compiled runtime data that can be destroyed and reloaded from a descriptor.

The catalog does not cache compiled segments or final position results. It only describes which sources are locally available and how to return to those sources.
