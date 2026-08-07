# C ABI

Status: Current

Taiyin exposes a versioned C99 ABI for FFI bindings and applications that do
not consume the C++ API. Include the umbrella header:

```c
#include <taiyin/c/taiyin.h>
```

The installed shared library is named `taiyin` on platforms with versioned
SONAMEs (`libtaiyin.so` or `libtaiyin.dylib`). Windows includes the ABI in the
runtime and import-library name, for example `taiyin-5.dll` and `taiyin-5.lib`.
Query `taiyin_get_c_abi_version()` before
using a dynamically discovered library. `taiyin_get_library_version()` reports
the independent semantic library version; the current core baseline is
`1.0.0`. `taiyin_get_library_codename()` reports the major-release codename;
Taiyin `1.x.x` is **Singularity**. The returned version and codename strings
have static library lifetime and must not be freed. `taiyin_get_capabilities()`
reports the functional modules and feature-level extensions present in the
loaded build. In particular,
`TAIYIN_CAPABILITY_SPLIT_TIME` identifies libraries that export the
split-Julian-Date time API.

BaZi is an optional extension and is intentionally not part of the umbrella
header. Configure with `-DTAIYIN_BUILD_BAZI_EXTENSION=ON` and include it
explicitly:

```c
#include <taiyin/c/bazi.h>
```

An enabled build installs that header, exports `taiyin_bazi_*`, and advertises
`TAIYIN_CAPABILITY_BAZI`. A disabled build does none of those; bindings should
use the capability bit when selecting among separately packaged binaries.

`taiyin_format_ephemeris_diagnostic()` produces a stable human-readable log
line. Call it first with a null buffer and zero capacity to obtain the required
size (including the trailing NUL), then provide a caller-owned buffer.

## Build And Install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target taiyin_c
cmake --install build --prefix /your/prefix
```

This installs the enabled C ABI headers under `include/taiyin/c/` and the
shared library. Source-tree builds also provide the `taiyin_c_static` target
for statically linked applications;
the target propagates `TAIYIN_C_STATIC` and its internal archive dependencies.
The install also includes `LICENSE` and `NOTICE`. The C ABI requires C99 or
newer. The headers are also valid C++.

The C ABI version is `5`. Version 3 replaced scalar Julian-day arguments and
time-bearing result fields with `taiyin_split_julian_date`. Version 5 enlarges
`taiyin_bazi_context_config` for qi-yun and da-yun policy; callers compiled
against version 4 or earlier must be rebuilt. Taiyin follows semantic versioning
for the library: compatible additions retain the ABI major, while removing or
changing an existing C symbol or structure contract requires a new ABI major.
Shared-library physical filenames use `ABI.0.0` independently of the package
semantic version, so installing a new ABI does not overwrite the binary behind
an older SONAME. Windows, which does not encode `VERSION` in DLL filenames,
uses `taiyin-<ABI>` as the output name instead.
The C++ API is an implementation-facing interface and is not promised as a
stable binary ABI. A codename identifies a semantic-version major release and
does not participate in version ordering or ABI compatibility checks.

## Lifetime And Ownership

- `taiyin_context_create()` allocates an opaque context. Release it with
  `taiyin_context_destroy()`.
- Strings, source-path arrays, output buffers, and callback `user_data` remain
  caller-owned unless a function explicitly says otherwise.
- Runtime and catalog registration functions copy or open the required runtime
  data internally. Global setup should finish before concurrent calculations.
- `taiyin_context_set_deflectors()` copies the supplied deflector records into
  the context. The caller may release its input array after the call returns.
- Every caller-supplied top-level result or option structure with `struct_size`
  must first be passed to its matching `_init()` function. Element records
  returned inside arrays are initialized by the library.
- Functions returning arrays use `capacity` plus `out_count`. A count-only call
  uses a null output pointer and zero capacity when the declaration permits it.

Callers must treat result values as valid only when a function returns
`TAIYIN_STATUS_OK`. Structured-result wrappers avoid committing failed
temporary results. Diagnostics are updated on both success and failure when
supplied.

## Threading And Callbacks

Independent calculations and immutable contexts may be used concurrently.
Global runtime and registry mutations are setup-time operations.

Custom native targets, ayanamsha models, and house systems accept a C callback
and an opaque `user_data` pointer. Registrations are process-lifetime and cannot
be removed. The callback and its `user_data` must remain valid until process
exit and must tolerate concurrent calls. Exceptions must not cross the C ABI.

## Context Configuration

`taiyin_context` owns the calculation policy used by position and event APIs.
In addition to observer location, atmosphere, time-scale policy, and ephemeris
route, the C ABI exposes:

- `taiyin_astro_model_config` for TDB, precession, nutation, obliquity, and
  frame-route selection;
- `taiyin_apparent_config` for apparent corrections, output frame, light-time
  iteration, aberration, deflection, and derivative-step policy;
- geocentric, explicit-offset, simple topocentric, and EOP-backed precise
  topocentric observer setup;
- celestial-pole offsets, solar or custom deflectors, and Shapiro delay.

Initialize configuration values with their `_init()` functions before changing
individual fields. Topocentric state and deflectors use dedicated setters and
are preserved when `taiyin_context_set_apparent_config()` changes the remaining
apparent options.

Unknown apparent flags, Delta-T model IDs, and ephemeris-family IDs are rejected
instead of silently selecting a fallback.

## Split Julian Dates

The original time functions that accept or return one absolute `double` Julian
date remain available for compatibility and ordinary calculations. Around
modern epochs, adjacent values of that representation are roughly 40
microseconds apart.

Bindings that retain sub-microsecond time coordinates should use
`taiyin_split_julian_date`, which stores an integral day number and a normalized
fraction in `[0, 1)`. The split functions include calendar conversion,
normalization, addition and subtraction, UTC/TAI/TT/UT1 conversion, both TDB
models, and complete precise or estimated time-scale result structures.

```c
taiyin_split_julian_date utc;
taiyin_split_julian_date tt;

taiyin_split_julian_date_from_parts(2460409, 0.25, &utc);
taiyin_utc_to_tt_split(&utc, 37.0, &tt);
```

The split implementation never merges the value while applying offsets.
TT/TDB models evaluate their slowly varying correction from an approximate
absolute JD, then add that correction to the split coordinate without
discarding its low-order fraction. Converting explicitly with
`taiyin_split_julian_date_to_double()` is intentionally precision reducing.

This is a time-coordinate facility, not an ephemeris-core precision claim.
Position, state, event-search, eclipse, occultation, visibility, astrology, and
similar physical-calculation entry points now use `taiyin_split_julian_date`
end to end, including their time-bearing result fields. The remaining scalar-JD
entry points are legacy pure time-conversion helpers. Ephemeris coverage bounds,
catalog reference epochs, and EOP table knots remain `double` because they are
dataset metadata rather than calculation-time coordinates.

## Modules

The umbrella header covers:

- runtime initialization, contexts, time conversion, positions, states, stars,
  observed coordinates, visibility, phenomena, and local solar time;
- longitude, aspect, phase, separation, station, orbital, and transit searches;
- solar and lunar eclipses, route products, occultations, and heliacal events;
- sidereal positions, ayanamsha, houses, lunar nodes, and lunar apsides;
- Chinese lunisolar year calculation and bidirectional civil-date conversion;
- custom native-target, ayanamsha, and house-system callback registration.

Use the module headers under `taiyin/c/` when a binding generator benefits from
smaller translation units.

Fixed-star position entry points use 64-bit flag words. Current native position
flags occupy the low word; unsupported high-word bits are rejected rather than
silently truncated, leaving ABI space for later star-specific options.

Custom Kepler targets use the existing Kepler/TKC1 file formats and are loaded
through `taiyin_runtime_add_source_path()` or the runtime configuration source
paths. Fixed-star astrometry can be supplied as TSC1 memory, TSC1 files, or
TSF1 files. Bindings may keep their own user-facing name-to-ID table; the C ABI
does not expose the internal process-global body-name registry.

## Minimal Example

```c
taiyin_runtime_config config;
taiyin_runtime_config_init(&config);
config.data_root = "/path/to/taiyin-data";

if (taiyin_runtime_initialize(&config) != TAIYIN_STATUS_OK) {
    return 1;
}

taiyin_context* context = NULL;
if (taiyin_context_create(&context) != TAIYIN_STATUS_OK) {
    return 1;
}

double moon[6];
taiyin_status status = taiyin_calc_position_tt(
    context,
    TAIYIN_BODY_MOON,
    2460409.0,
    TAIYIN_POSITION_XYZ,
    moon,
    NULL);

taiyin_context_destroy(context);
return status == TAIYIN_STATUS_OK ? 0 : 1;
```
