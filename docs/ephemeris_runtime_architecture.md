# Ephemeris Runtime Architecture

This document describes the current MVP architecture for `taiyin-ephemeris` ephemeris sources, descriptor catalog, global runtime, cache, route selection, concurrent loading, and custom source lifecycle.

The core rule is:

```text
source files / source registries are the source of truth
catalog descriptors point to that source of truth
cache entries are reloadable runtime artifacts
```

The cache may evict entries at any time. Eviction must not lose source data; later requests reload from the descriptor's source path or source registry.

## High-level data flow

```text
explicit source paths / custom sources
              │
              ▼
       descriptor discovery
              │
              ▼
   Global EphemerisBlockCatalog
              │
              ▼
   EphemerisPriorityRegistry
              │
              ▼
        EphemerisService
              │
              ├── cache hit ───────────────► EphemerisBlockCache.eval_*
              │                                  │
              │                                  ▼
              │                         EphemerisResult
              │
              └── cache miss
                    │
                    ▼
              RouteInflightMap
                    │
                    ├── same route key waits
                    └── one loader
                          │
                          ▼
                  descriptor_loader
                          │
                          ▼
                  EphemerisBlockCache.insert
                          │
                          ▼
                  EphemerisBlockCache.eval_*
                          │
                          ▼
                  EphemerisResult
```

The global runtime path is:

```text
eval_global_ephemeris_state(request)
        │
        ▼
acquire global runtime read lock
        │
        ▼
EphemerisService::eval_state(request)
        │
        ▼
select_calculation_route(request)
        │
        ▼
cache / singleflight / load / eval
        │
        ▼
release global runtime read lock
```

## Components

| Component | Files | Role |
| --- | --- | --- |
| `TaiyinRuntime` | `include/taiyin/runtime/taiyin_runtime.h`, `src/runtime/taiyin_runtime.cpp` | Owns the global catalog, priority registry, cache, and `EphemerisService`. |
| `EphemerisService` | `include/taiyin/runtime/ephemeris_service.h`, `src/runtime/ephemeris_service.cpp` | Converts an `EphemerisRequest` into a selected descriptor/cache route and evaluates it. |
| `EphemerisBlockCatalog` | `include/taiyin/internal/ephemeris_catalog.h`, `src/ephemeris_catalog.cpp` | Stores `EphemerisBlockDescriptor` records. |
| `EphemerisPriorityRegistry` | `include/taiyin/internal/ephemeris_catalog.h`, `src/ephemeris_catalog.cpp` | Stores global and target-specific method priorities. |
| `EphemerisBlockCache` | `include/taiyin/internal/ephemeris_cache.h`, `src/ephemeris_cache.cpp` | Global shared runtime cache keyed by `EphemerisRouteKey`. |
| `RouteInflightMap` | `include/taiyin/internal/route_inflight_map.h`, `src/route_inflight_map.cpp` | Same-key cache miss singleflight for descriptor loading. |
| `descriptor_loader` | `include/taiyin/internal/descriptor_loader.h`, `src/descriptor_loader.cpp` | Loads or compiles a descriptor into a `StorageEphemerisBlock` and inserts it into cache. |
| `custom_ephemeris_source_registry` | `include/taiyin/internal/custom_ephemeris_source_registry.h`, `src/custom_ephemeris_source_registry.cpp` | Stores memory-backed and file-backed custom ephemeris source definitions. |
| `WriterPreferredRwLock` | `include/taiyin/internal/writer_preferred_rwlock.h` | C++11 writer-preferred readers-writer lock for global runtime topology. |

## Source paths and descriptors

The runtime currently uses explicit source paths. It does not scan default directories, environment variables, or `~/.taiyin` automatically.

```cpp
EphemerisRuntimeConfig config;
config.source_paths = paths;
config.source_path_count = count;
initialize_global_ephemeris_runtime(config);
```

or:

```cpp
add_global_ephemeris_source_path(path);
```

Discovery converts files into `EphemerisBlockDescriptor` records. A descriptor records where data lives and what it covers:

```text
EphemerisBlockDescriptor
  route_key       target/center/method/bucket
  source_key      source identity and generation
  target_id
  center_id
  method_id
  frame
  format
  jd_tdb_start
  jd_tdb_end
  path
```

Descriptors do not store a fully flattened copy of source data. They point back to source files or source registries.

## Route selection

`EphemerisService::select_calculation_route` performs selection:

```text
EphemerisRequest
  │
  ▼
Catalog candidates covering target/center/frame/JD
  │
  ▼
PriorityRegistry ranks methods
  │
  ▼
source descriptor → bucket descriptor
  │
  ▼
cache hit check
  │
  ├── hit: return selected route
  └── miss: RouteInflightMap + descriptor_loader
```

Default method priorities are initialized in `src/runtime/taiyin_runtime.cpp`:

```text
SPK                         100
OPM4                         90
TKC1 Kepler catalog          70
Taiyin Kepler file           60
TSC1 star catalog            50
```

Target-specific priorities can override global method priorities.

## Cache architecture

`EphemerisBlockCache` is keyed by:

```text
EphemerisRouteKey(target_id, center_id, method_id, bucket_id)
```

Internally it maintains:

```text
route_to_cache_id_   EphemerisRouteKey -> cache_id
cache_id_to_route_   cache_id -> EphemerisRouteKey
blocks_              cache_id -> shared_ptr<StorageEphemerisBlock>
stats_               cache_id -> EphemerisCacheStats
coverage_            cache_id -> [jd_tdb_start, jd_tdb_end)
```

`StorageEphemerisBlock` owns compiled block data and destroy callbacks. `CompiledEphemerisBlock` is a lightweight borrowed view with function pointers and a raw data pointer.

Cache entries are stored as `shared_ptr<StorageEphemerisBlock>`. During a cache-hit eval, the cache copies a `shared_ptr` under the mutex, builds a `CompiledEphemerisBlock`, updates hit stats, releases the mutex, and then calls the ephemeris callback.

```text
lock cache mutex
  find route
  copy shared_ptr<StorageEphemerisBlock>
  check coverage
  copy CompiledEphemerisBlock
  touch stats
unlock cache mutex

run position/velocity/acceleration callback outside mutex
```

This means cache-hit evals can overlap. If another thread evicts or clears the same cache entry while the callback is active, the local `shared_ptr` keeps the data alive until eval returns.

## Weighted eviction

Eviction selects the lowest keep score:

```text
keep_score =
    recency_weight      * recency_score
  + frequency_weight    * log(1 + hit_count)
  + priority_weight     * priority_bias
  + reload_cost_weight  * compile_cost_us
  - size_penalty_weight * bytes
```

The cache tracks:

```text
last_access
hit_count
compile_cost_us
priority_bias
coverage
```

Cache eviction removes runtime compiled blocks only. It does not remove descriptors or source files.

## Cache miss singleflight

`RouteInflightMap` deduplicates concurrent loads for the same `EphemerisRouteKey`.

```text
Thread A misses route K
  -> becomes loader
  -> load_descriptor_into_cache(K)

Thread B misses route K while A is loading
  -> waits
  -> wakes after A releases
  -> re-checks cache

Thread C misses route L
  -> independent loader because L != K
```

Failed loads also wake waiters. Waiters do not trust a success flag; they re-check the cache. If still missing, `EphemerisService` tries the next ranked descriptor.

## Global runtime locking

Global helper APIs use a writer-preferred RW lock around the singleton runtime.

Read-locked operations:

```text
eval_global_ephemeris_state
find_global_ephemeris_descriptor
global_ephemeris_catalog_size
global_ephemeris_cache_contains
global_ephemeris_cache_entry_count
global_ephemeris_cache_total_bytes
global_ephemeris_cache_max_bytes
```

Write-locked operations:

```text
initialize_global_ephemeris_runtime
add_global_ephemeris_source_path
add_global_ephemeris_descriptor
set_global_ephemeris_method_priority
set_global_ephemeris_target_method_priority
clear_global_ephemeris_cache
```

Raw global accessors remain for low-level compatibility and single-threaded binding checks, but they bypass the global runtime lock:

```text
global_ephemeris_catalog
global_ephemeris_priorities
global_ephemeris_cache
global_ephemeris_service
```

Concurrent code should prefer the locked helper APIs.

## Custom source lifecycle

Custom sources are process-global registry entries protected by an internal mutex.

### Memory-backed custom source

```text
register_custom_ephemeris_source
  -> clone user data into registry
  -> registry owns that clone

make_custom_ephemeris_descriptor
  -> descriptor source_key points to registry source id
  -> descriptor path is empty

compile_custom_ephemeris_source
  -> copy registry fields under mutex
  -> clone registry data into a cache block
  -> cache owns the cache clone
```

A cached block is independent from the registry clone. Unregistering the source destroys the registry clone but does not invalidate cache clones that already exist.

After eviction, reload requires the registry source to still exist. If the source was unregistered or the registry was cleared, reload fails.

### File-backed custom source

```text
register_custom_ephemeris_file_source
  -> store path + load callback + eval callbacks
  -> no file is read at registration

make_custom_ephemeris_descriptor
  -> descriptor.path stores the source path

compile_custom_ephemeris_source
  -> copy callbacks/path under registry mutex
  -> call load(descriptor.path)
  -> cache owns parsed data
```

File-backed sources read the file each time a cache entry is loaded. If the file is modified between eviction and reload, reload observes the new file contents. If the file is deleted or unreadable, reload fails.

Unregistering a file-backed source removes callbacks and registry metadata. Existing cached blocks remain valid, but reload after eviction fails because the registry source is gone.

## Invariants

```text
1. Source files and registries are source of truth.
2. Descriptors are catalog records pointing at source of truth.
3. Cache entries are runtime artifacts and may be evicted.
4. Eviction must not corrupt descriptors or source data.
5. Same-key cache misses are singleflighted.
6. Different-key cache misses can load independently.
7. Cache-hit callbacks run outside the cache mutex.
8. Active evals keep cache storage alive through shared_ptr ownership.
9. Global runtime topology changes take the global write lock.
10. Concurrent global evals take the global read lock.
```

## Test coverage

Important regression tests:

```text
tests/test_global_ephemeris_runtime.cpp
  global explicit path initialization, shared cache, eviction/reload, custom VSOP87 Mercury, global concurrency

tests/test_ephemeris_mvp_end_to_end.cpp
  VSOP87 Mercury full path: custom file source, global runtime, singleflight first miss, cache-hit concurrency, eviction/reload

tests/test_ephemeris_cache.cpp
  weighted eviction, cache ownership, cache-hit callback overlap, clear during active eval

tests/test_ephemeris_singleflight.cpp
  service/global singleflight, fallback after preferred load failure

tests/test_custom_ephemeris_source_registry.cpp
  custom source cache reload and file-backed reload

tests/test_custom_source_lifecycle.cpp
  custom source unregister/clear/delete lifecycle semantics

tests/test_writer_preferred_rwlock.cpp
  global runtime lock primitive behavior
```

## Current MVP scope boundaries

The MVP runtime intentionally keeps these out of scope for now:

```text
- no default source directory scan;
- no persistent index/cache metadata files;
- no automatic cache save/restore;
- no public long-lived block lease API;
- no full upper-layer astrology pipeline manager;
- no guarantee that raw global accessors are thread-safe;
- no local TaiyinRuntime-level lock unless callers use the global helper API.
```

These can be added later without changing the core source/catalog/cache/runtime model.
