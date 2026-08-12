# Archived sxwnl reference code

This directory contains the former Taiyin ports and frozen regression material
derived from the Shouxing Astronomical Calendar (`sxwnl`). It is retained for
historical comparison and is not linked into the production Taiyin runtime.

- `eclipse/` contains the archived two-dimensional lunar-eclipse and legacy
  solar-eclipse route/local implementations.
- `occultation/` contains the former occultation adapter around the legacy
  solar geometry.
- `calendar/` contains the optional black-box generator used to freeze the
  historical UTC+08 civil-day compatibility profile.
- `oracles/` contains frozen fixtures and optional generators that require a
  separately obtained upstream sxwnl source tree.

The active eclipse and occultation implementations live under `src/runtime/`
and use Taiyin's own three-dimensional shadow, ellipsoid-intersection, route,
and local-observer geometry. The archived sources remain subject to the
upstream copyright and usage statement reproduced in the repository
[`NOTICE`](../../NOTICE); they are not covered by Taiyin's MPL-2.0 grant.

The archived C++ sources are compiled only by explicit regression/comparison
test targets. The old route fixture is no longer a CTest requirement because
the current route sampler intentionally has different point placement and
adaptive subdivision.
