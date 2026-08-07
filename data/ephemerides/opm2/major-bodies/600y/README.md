# 600-year major-body OPM2 files

This directory stages the current polished 600-year major-body OPM2 candidate files for taiyin runtime/catalog wiring.

## Product identity

These files are packaged OPM2 ephemeris data. Build provenance is not a runtime source identity; runtime routing should choose these as packaged defaults only after catalog/runtime wiring is enabled and tested.

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

This directory contains staged data only. Discovery/load coverage is tested by `test_opm2_staged_data`; default runtime route wiring should be done separately with route-priority tests.
