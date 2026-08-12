# DE442 full-coverage major-body OPM2 package

This is the packaged major-body OPM2 product rebuilt from JPL DE442. It uses
one segment-aligned file per stored vector, rather than calendar-range shards.
The files carry source id `2` (`OPM2_SOURCE_TAIYIN_DE442_REBUILT`), so the
runtime prefers them over the retained prerelease OPM2 product where coverage
overlaps.

Every stored vector then received post-quantization polishing in its native
coordinate convention, with 512 Chebyshev nodes per segment and a
same-density shifted guard grid. This is one continuous package: all files
share the original phase-aligned segment grids rather than being independently
restarted calendar-range shards.

## Coverage

DE442's common source interval is JD 2287184.5 through 2688976.5 (about 1100
years).  Each body keeps only complete source-safe affine segments, so its
individual first and last timestamps are recorded in `MANIFEST.json` and vary
slightly by model.

## Validation

The generation settings and raw-fit metrics are recorded per file in
`MANIFEST.json`. A 512-node-per-segment geocentric report against DE442 gives
a worst p99 of 1.689 mas and a worst maximum of 3.164 mas (both Venus).
The Moon is 0.194 mas at p99 and 0.251 mas maximum. The validation grid is
the same Chebyshev density as the active polishing grid; the shifted guard
grid is also evaluated during polishing, so these are release-quality
consistency measurements rather than a wholly held-out phase sweep.

The detailed report is maintained with the OPM2 reference tooling in
`opm-python-demo/docs/opm2-de442-full-accuracy.md`.
