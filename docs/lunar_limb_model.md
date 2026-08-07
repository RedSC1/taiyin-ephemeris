# Lunar Limb Model

Status: Current

Taiyin can refine eclipse and lunar-occultation contact times with a direction-dependent lunar
silhouette derived from Kaguya/SELENE LALT topography. The model is optional and
explicitly enabled; merely loading it does not alter calculations.

## Loading And Use

```cpp
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/occultation_search.h"
#include "taiyin/runtime/runtime.h"

taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "data";
config.lunar_limb_path = "data/lunar-limb/kaguya_lalt_16ppd.tll1";
taiyin::runtime::initialize_global_ephemeris_runtime(config);

taiyin::runtime::NativeCalcContext context;

const uint64_t flags =
    taiyin::runtime::TAIYIN_ECLIPSE_INCLUDE_CONTACTS |
    taiyin::runtime::TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;

const uint64_t occultation_flags =
    taiyin::runtime::TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION;
```

The global runtime owns the TLL1 mapping and its lifetime. User contexts do not
store a model pointer and do not unmap it. Once initialization completes, the
model remains immutable while calculations run; do not reinitialize or replace
global data concurrently with calculations.

When `TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION` is set without a globally loaded model,
the calculation returns `TAIYIN_ERROR_UNSUPPORTED`; it does not silently fall
back to the circular limb. Without the flag, loading a model has no effect.

For lunar occultations, the corresponding opt-in flag is
`TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION`. It is accepted by the lunar
fixed-star and body `search_next_*` entries. Smooth geometry locates the event;
TLL1 residuals then bracket and polish contacts with safeguarded
Newton/secant/bisection. Occultation `where` surface limits do not yet consume
this flag.

## Corrected Geometry

The smooth circular solution remains the event seed. Contact roots are then
polished against the lunar radius at the actual position angle and optical
libration:

- local solar C1/C4 use the limb facing the Sun;
- total-eclipse C2/C3 use the limb facing the Sun;
- annular-eclipse C2/C3 use the opposite limb;
- global solar P1/P4 use the limb facing the tangent observer on Earth;
- lunar P1/P4 and U1/U4 use the limb facing the shadow center;
- lunar U2/U3 use the opposite limb.

Global solar P1/P4 do not query the lunar limb at a fixed approximate Earth
tangent point. At every candidate epoch, the solver minimizes the penumbral
margin over the WGS84 ellipsoid. That margin couples the surface-to-axis
distance, the TLL1 radius for the local viewing direction, and penumbral-cone
expansion. A safeguarded secant/bisection solve then finds the epoch where the
minimum margin is zero. The lunar-orientation transform is prepared once per
epoch and reused throughout the inner surface iteration.

Solar central-line begin/end are axis/Earth intersections and therefore do not
depend on the lunar radius. Route APIs also accept `uint64_t flags`. With lunar
limb correction enabled, the core, penumbral, and half-magnitude limits and all
polygons closed from those curves iteratively use the TLL1 radius for their own
viewing directions. Total or annular duration at the route center uses the
smooth solution as a seed and then refines C2/C3 with the same limb model; the
center line remains unchanged. Without the flag, route geometry continues to
use the smooth Moon-radius model selected by the context.

## Bundled TLL1 Data

The bundled `kaguya_lalt_16ppd.tll1` file is generated from the official
Kaguya LALT 1/16-degree global DEM in the Mean Earth/Polar Axis frame. It
contains signed metre offsets from the 1737.4 km reference sphere:

| Axis | Coverage | Step |
| --- | ---: | ---: |
| libration longitude | -9 to +9 deg | 0.5 deg |
| libration latitude | -8 to +8 deg | 0.5 deg |
| position angle | 0 to 359.8 deg | 0.2 deg |

The file is 4,395,792 bytes and is memory-mapped. Each query reads eight int16
samples and performs trilinear interpolation. A deterministic 250-point direct
DEM comparison measured 100.4 m RMS, 209.5 m P95 absolute error, and 376.9 m
maximum absolute error. This is suitable for sub-second to few-second contact
corrections; it is not a dedicated Baily's-beads profile.

The source attribution and regeneration command are recorded in
`data/lunar-limb/README.md`.
