# 600-year major-body OPM2 files

This directory contains the packaged DE441-derived 600-year major-body OPM2
product used for reproducibility and as a fallback outside the DE442 package's
coverage.

## Product identity

These files carry OPM2 source id `1` (`OPM2_SOURCE_TAIYIN_PRERELEASE`). The
runtime therefore selects the DE442-derived source id `2` where both products
cover a request, while retaining this product as a compatible lower-priority
route.

## Range

Requested product range:

```text
JD 2378496.5 .. 2597641.5
1800-01-01 .. 2400-01-01
```

Each file's segment-aligned coverage may be slightly wider than the requested range.

## Files

```text
sun.opm2
mercury.opm2
venus.opm2
emb.opm2
moon.opm2
mars.opm2
jupiter.opm2
saturn.opm2
uranus.opm2
neptune.opm2
pluto.opm2
MANIFEST.json
```

## Notes

The data are derived from JPL DE441. Discovery/load coverage and the DE442
AUTO preference are exercised by `test_opm2_staged_data`.
