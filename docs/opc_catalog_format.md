# OPC1 Catalog Format

Status: Current
Last reviewed: 2026-07-01
Primary header: `include/taiyin/internal/opc_catalog_persistent.h`

`OPC1` is Taiyin's persistent ephemeris-source index format. Its file magic is `OPC1`; the current schema version is `OPC_VERSION = 2`.

The OPC catalog accelerates discovery when the runtime starts or when a data directory is added. Real ephemeris data still lives in source files such as:

```text
*.opm2
*.bsp / *.spk
*.tke1
*.tkc1
```

The OPC file stores only the runtime descriptor metadata for those source files. It is not an ephemeris data file and not a second source of truth.

## Runtime Role

When the runtime loads a data directory, it can take two paths:

```text
load index.opc
  |
  +-- valid   -> restore the EphemerisBlockDescriptor list directly
  |
  +-- invalid -> recursively scan source files and regenerate index.opc
```

OPC is therefore a removable and rebuildable index. If source files are added, removed, renamed, or changed in size, the fingerprint no longer matches; the old OPC is rejected and the runtime falls back to directory discovery.

## File Layout

`OPC1` is a packed little-endian binary:

```text
[OpcHeader]
[OpcDescriptorRecord * N]
[string table]
```

The current implementation loads this format only on native little-endian environments.

## Header

`OpcHeader` is fixed at 128 bytes:

```text
magic = "OPC1"
version = OPC_VERSION
flags
descriptor_count
descriptor_records_offset
string_table_offset
string_table_size
fingerprint
source_id
source_version
generation
reserved
```

The current writer uses:

```text
source_id = 0
source_version = OPC_VERSION
generation = OPC_VERSION
```

## Descriptor Record

`OpcDescriptorRecord` is currently fixed at 128 bytes. Each record corresponds to one runtime ephemeris descriptor. A source file can contribute multiple records; for example, a `TKC1` catalog can generate descriptors for many small bodies, and an SPK kernel can expose multiple target/center routes.

Fields:

```text
target_id
center_id
method_id
frame_id
jd_tdb_start
jd_tdb_end
source_id
block_id
generation
purpose
bucket_id
format
path_offset
cache_policy_kind
file_size
file_mtime_sec
file_mtime_nsec
cache_origin_jd
cache_span_days
cache_first_index
cache_count
```

`source_id/block_id/generation/purpose` is restored as an `EphemerisBlockKey`, which locates the source-file payload. `target_id/center_id/method_id/bucket_id` is restored as an `EphemerisRouteKey`, which is used for route lookup and segment cache keys.

`cache_policy_*` is the key addition in v2. It describes how a source descriptor is divided into loadable buckets:

```text
CacheWholeEntry      the whole descriptor is one cache entry
CacheFixedSpan       bucket by fixed time span
CacheNaturalSegment  bucket by the source format's natural segment
```

OPM2 usually uses the natural Chebyshev segment grid. SPK, TKC1, and custom Kepler files write the appropriate cache policy from their discoverers.

## String Table And Paths

The first byte of the string table must be `NUL`. Each descriptor's `path_offset` points to a `NUL`-terminated relative path.

OPC does not store absolute paths. The loader joins each relative path with the catalog root:

```text
major-bodies/600y/mars.opm2
asteroids/600y/ceres.opm2
cob/full/jupiter_cob.opm2
kepler/sbdb/sbdb-tier0-core.tkc1
```

Absolute paths, empty paths, and out-of-range strings cause catalog loading to fail.

## Fingerprint

The fingerprint is computed from indexed source files. Current indexed source suffixes are:

```text
.opm2
.bsp
.spk
.tke1
.tkc1
```

Hash input includes:

```text
relative path
file size
```

Paths are sorted before hashing. mtime is written to each record for diagnostics, but does not participate in the fingerprint. This avoids invalidating packaged catalogs just because a checkout, archive extraction, or installer changed modification times.

The current fingerprint is not a content hash. If a source file is replaced at the same relative path with the same byte size, OPC fingerprinting does not guarantee invalidation; regenerate the OPC for that case, or add content-level validation before relying on automatic detection.

## Validation Rules

OPC loading succeeds only when:

- magic is `OPC1`;
- version equals the current `OPC_VERSION`;
- header offsets and ranges are valid;
- descriptor count is nonzero;
- fingerprint matches indexed source files under the current root;
- every record has valid target, center, method, frame, and time range;
- format is a persistent runtime ephemeris format;
- source key is nonzero and generation is nonzero;
- cache policy is valid;
- path is a nonempty relative path.

Current persistent ephemeris descriptor formats include:

```text
OPM2
SPK
Kepler/TKE1
TKC1
```

TSC1/TSF1 star catalogs do not enter this ephemeris descriptor OPC path. Star data is loaded separately through the star provider/store.

## Loader Behavior

Core entry point:

```cpp
collect_ephemeris_descriptors_from_catalog_or_directory(
    root,
    catalog_path,
    discoverers,
    options,
    out);
```

Behavior:

1. If `catalog_path` is nonempty, first try `load_opc_persistent_catalog(catalog_path, root, out)`.
2. If the OPC is missing, stale, or invalid, run directory discovery.
3. If directory discovery succeeds and `catalog_path` was provided, try to rewrite the OPC.

Failure to rewrite the OPC does not change discovery results; it only affects next-start load speed.

## Packaged OPC Files

The repository currently contains two packaged indexes:

```text
data/index.opc
data/ephemerides/opm2/index.opc
```

`data/index.opc` is the top-level packaged data index. It covers packaged OPM2 data and the TKC1 catalog under `data/kepler/sbdb/`. The default packaged runtime prefers it.

`data/ephemerides/opm2/index.opc` is the OPM2 subtree index, useful when `data/ephemerides/opm2` is used as the data root.

The runtime tries `index.opc` for roots whose final path component is `data`, `opm2`, or `sbdb`. If the index is missing or invalid, it scans the directory.

## Generator

Generation command:

```text
generate_ephemeris_catalog <ephemeris-root> [catalog-path]
```

If `catalog-path` is omitted, the tool writes:

```text
<ephemeris-root>/index.opc
```

The tool runs directory discovery, writes the OPC, and reloads it once to verify the descriptor count.

## Related Files

- `include/taiyin/internal/opc_catalog_persistent.h`
- `src/opc_catalog_persistent.cpp`
- `tests/test_opc_catalog_persistent.cpp`
- `tools/generate_ephemeris_catalog.cpp`
