# Ephemeris Data Packages

Status: Current  
Last reviewed: 2026-08-24

Taiyin keeps large ephemeris products outside the native library and language
bindings. Small products used by ordinary contemporary calculations remain in
the repository or wrapper wheel; long-range products are independent GitHub
Release assets.

## Available major-body products

| Product | Delivery | Common coverage | Intended use |
| --- | --- | --- | --- |
| DE441-derived 600-year OPM2 | Repository and wrapper data bundle | 1800-01-01 through 2400-01-01 | Contemporary compatibility |
| DE442-derived OPM2 | Repository and wrapper data bundle | Approximately 1550 through 2650 | Default contemporary major-body source |
| Full-range DE441-derived OPM2 | Optional GitHub Release asset | JD `-3096455.499990447` through `7996074.500009106` | Long-range calculations and historical stress tests |
| Raw NASA/JPL SPK | User-supplied | Depends on the kernel | Direct JPL reference data or targets absent from an OPM2 package |

The full-range archive is named `taiyin-opm2-de441-full-v1.zip`. It is about
85.3 MiB and expands to 87.98 MiB of OPM2 payload plus manifests and the OPC
catalog. It contains 51 approximately 600-year shards and 561 OPM2 files for
the Sun, Moon, Mercury, Venus, Earth-Moon barycenter, Mars, Jupiter, Saturn,
Uranus, Neptune, and Pluto.

Download it from the project [GitHub Releases](https://github.com/RedSC1/taiyin-ephemeris/releases)
page. The separate `.zip.sha256` asset verifies the archive itself; the
`SHA256SUMS` file inside the archive verifies every extracted file.

## Archive layout

```text
taiyin-opm2-de441-full-v1/
├── README.md
├── README_CN.md
├── LICENSE
├── NOTICE
├── MANIFEST.json
├── VALIDATION.json
├── SHA256SUMS
└── opm2/
    ├── index.opc
    └── shards/
        ├── shard-000-jd-m3096455/
        │   ├── MANIFEST.json
        │   └── *.opm2
        └── ...
```

Point `data_root` at the extracted `opm2` directory. Because its final path
component is `opm2`, the runtime loads `index.opc` first and falls back to a
recursive scan only if the catalog is absent or invalid.

```cpp
taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "/data/taiyin-opm2-de441-full-v1/opm2";

if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
    return 1;
}
```

For a C application, assign the same path to `taiyin_runtime_config.data_root`.
Python and Dart expose the corresponding `data_root` constructor option. An
explicit configured data root is loaded before fallback packaged roots, so the
full-range package supplies overlapping DE441 routes as well as epochs outside
the smaller bundled product.

The full DE441 product uses OPM2 source id `1`, the same DE441-derived product
identity as the bundled 600-year data. DE442-derived OPM2 uses source id `2` and
normally has higher AUTO priority when both products are registered explicitly.
Use a single primary data root when exact route reproducibility matters.

## Full-range validation against DE441

The release workflow validates each of the 51 shards independently. Every
fitted segment is sampled at 512 Chebyshev nodes, and the reconstructed
geocentric direction is compared with NASA/JPL DE441. The table reports the
largest per-shard p99 and the largest individual error observed anywhere in the
full set. “Worst-shard p99” is deliberately not presented as a pooled global
percentile.

| Body | Samples | Worst-shard p99 (arcsec) | Observed maximum (arcsec) |
| --- | ---: | ---: | ---: |
| Sun | 31,547,904 | 0.000538 | 0.000770 |
| Moon | 206,114,304 | 0.001266 | 0.001677 |
| Mercury | 64,542,208 | 0.000649 | 0.001370 |
| Venus | 25,273,344 | 0.001755 | 0.003545 |
| Mars | 8,267,264 | 0.001309 | 0.002787 |
| Jupiter | 1,893,376 | 0.000510 | 0.000705 |
| Saturn | 1,623,040 | 0.000523 | 0.000842 |
| Uranus | 1,420,288 | 0.000345 | 0.000430 |
| Neptune | 1,420,288 | 0.000325 | 0.000381 |
| Pluto | 1,420,288 | 0.000321 | 0.000419 |
| **Total** | **343,522,304** | **0.001755** | **0.003545** |

Earth-Moon barycenter is a reconstruction dependency rather than a geocentric
sky direction, so it is not a row in this angular table. Its generated state is
still checked by the native state validator.

The final ZIP was independently extracted and checked as a user would receive
it: all internal SHA-256 checks passed, `index.opc` reloaded all 561 descriptors,
and 1,122 runtime evaluations covering all 11 stored bodies immediately before
and after every shard boundary completed without a route or coverage failure.

These values measure OPM2 reconstruction against DE441. Final apparent,
topocentric, rise/set, eclipse, or calendar results also depend on time scales,
observer geometry, and the selected correction models.
