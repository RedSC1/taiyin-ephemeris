# Packaged asteroid OPM2 shard

This directory contains shard 25 of the long-range asteroid OPM2 product. It
is the 600-Julian-year shard containing J2000 and is the default small package
installed by the runtime tests.

## Shard grid

All long-range asteroid products use the same outer shard grid as the major
body release:

```text
first shard JD:  -3092455
shard length:    219150 days (600 Julian years)
this shard:      25
requested range: JD 2386295 .. 2605445
```

Each OPM2 file retains its body's selected internal segment origin and period.
Its recorded coverage therefore extends slightly beyond the requested shard
range to include complete 100, 365.25, or 500-day OPM segments.

## Product identity

The files are source-agnostic runtime products. Their canonical source layer
uses official JPL SPK/Horizons samples where available and self-integrated
DE441 extensions outside those intervals. `MANIFEST.json` records the source
intervals and dense source-fit validation for every body.

User-provided BSP or OPM2 routes may override these packaged defaults through
normal catalog priority.

## Files

```text
ceres.opm2
pallas.opm2
juno.opm2
vesta.opm2
eros.opm2
chiron.opm2
pholus.opm2
nessus.opm2
lilith_1181.opm2
MANIFEST.json
```

Chiron uses a 100-day direct-coordinate grid. The other bodies use their
selected 365.25-day or 500-day fixed-frame grids.

## Validation

The generated files were read back and compared with the canonical NPZ source
on a one-day grid over the requested shard. All nine bodies pass:

```text
p99 < 0.01 arcsec
max  < 0.05 arcsec
```

These values measure OPM2 fit/readback error against the canonical source. They
are not an independent accuracy claim for the self-integrated tails.
