# C ABI

Status: Current

Taiyin exposes a versioned C99 ABI for FFI bindings and applications that do
not consume the C++ API. Include the umbrella header:

```c
#include <taiyin/c/taiyin.h>
```

The installed shared library is named `taiyin` on platforms with versioned
SONAMEs (`libtaiyin.so` or `libtaiyin.dylib`). Windows includes the ABI in the
runtime and import-library name, for example `taiyin-8.dll` and `taiyin-8.lib`.
Query `taiyin_get_c_abi_version()` before
using a dynamically discovered library. `taiyin_get_library_version()` reports
the independent semantic library version; the current preview is
`1.0.0-preview.5`. `taiyin_get_library_codename()` reports the major-release codename;
Taiyin `1.x.x` is **Singularity**. The returned version and codename strings
have static library lifetime and must not be freed. `taiyin_get_capabilities()`
reports the functional modules and feature-level extensions present in the
loaded build. In particular,
`TAIYIN_CAPABILITY_SPLIT_TIME` identifies libraries that export the
split-Julian-Date time API.

BaZi is an optional Chinese-metaphysics extension and is intentionally not part
of the umbrella header. It is disabled unless the policy gate and its own module
option are both enabled:

```sh
cmake -S . -B build \
  -DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON
```

Then include it explicitly:

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

The default build produces the legacy aggregate `taiyin_c` target. A modular build
produces one base shared library and separate DLLs only for enabled extensions:

```sh
cmake -S . -B build-modular \
  -DTAIYIN_BUILD_MODULAR_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-modular
cmake --install build-modular --prefix /your/prefix
```

The modular install always contains `taiyin`, which bundles the core,
astrology, Chinese-calendar, and Ganzhi APIs and native implementation.
Enabling BaZi adds the only optional shared library, `taiyin_bazi`. A BaZi
deployment therefore contains exactly two shared libraries: `taiyin` and
`taiyin_bazi`.
All enabled runtime dependencies are installed beside the facades, with
relocatable loader paths on macOS and ELF platforms. Windows installs the DLLs
and import libraries in the normal CMake runtime/archive destinations.

A modular consumer must load `taiyin` before an extension and must not mix the
legacy aggregate with modular libraries in one process. The base library
exports the native implementation symbols required by the optional extensions;
that native C++ linkage is internal to a matched Taiyin build and is not a
stable third-party C++ ABI.

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

The install and CPack binary archives deliberately do not include OPM2, TSC1,
TLL1, or other runtime data packs. Deploy a selected data root beside the
application, or register separately distributed data files at runtime.

The C ABI version is `8`. Version 3 replaced scalar Julian-day arguments and
time-bearing result fields with `taiyin_split_julian_date`. Version 5 enlarged
`taiyin_bazi_context_config` for qi-yun and da-yun policy. Version 7 adds the
planet-transit flag word and establishes the low-position/high-option layering
for observed flags. Version 8 replaces the three-state time-scale policy with
an explicit UTC out-of-range estimate flag; `*_ut` entry points now have fixed
UT1 semantics. Callers compiled against an earlier ABI must be
rebuilt. Taiyin follows semantic versioning
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
and an opaque `user_data` pointer. Registrations must be explicitly removed or
cleared before the callback or its `user_data` becomes invalid, and callbacks
must tolerate concurrent calls. Exceptions must not cross the C ABI. In a
modular build, astrology remains part of the base `taiyin` library, so
`taiyin_astrology_module_shutdown()` returns `TAIYIN_ERROR_UNSUPPORTED`.

## Context Configuration

`taiyin_context` owns the calculation policy used by position and event APIs.
In addition to observer location, atmosphere, the explicit UTC fallback flag,
and ephemeris route, the C ABI exposes:

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

A newly created context, and a `taiyin_apparent_config` initialized with
`taiyin_apparent_config_init()`, use light-time, annual aberration, and
gravitational deflection by the Sun. The context's built-in deflector list
contains the Sun only; Shapiro delay remains opt-in. Position `SPEED` output is
compatible with this default. Use the per-call `NO_ABERR` or `NO_GDEFL` flags
to suppress one correction, or replace the complete deflector array with
`taiyin_context_set_deflectors()`. When supplying several deflectors, the
solar-deflector index must identify the Sun needed by annual aberration and
solar-specific terms.

Unknown apparent flags, Delta-T model IDs, and ephemeris-family IDs are rejected
instead of silently selecting a fallback.

UTC and UT1 entry points have fixed meanings. A `*_ut` entry interprets its
split Julian date as UT1 and uses the context's Delta-T model to derive TT/TDB;
it does not consult EOP data. A `*_utc` entry interprets a civil calendar
value as UTC and, by default, requires leap-second and EOP coverage. Missing or
out-of-range EOP returns `TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE`; unavailable
leap-second data returns `TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE`. The
diagnostic fallback reason distinguishes a missing EOP table from an
out-of-range table.

Applications that explicitly accept a lower-precision fallback may enable it
during context setup:

```c
taiyin_context_set_allow_utc_out_of_range_estimate(context, 1);
```

When precise UTC resolution is unavailable, this treats the supplied civil
value as approximate UT1 and applies the configured Delta-T model. The flag
never changes the meaning or route of a `*_ut` function.

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
- longitude, aspect, phase, body-body/body-star separation, station, orbital,
  and transit searches;
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

During setup, `taiyin_runtime_set_ephemeris_source_priority()` can override the
choice between files in one provider. For AUTO, it also reorders that
provider's source-specific product rules, so promoting JUP349 above JUP365 or
demoting DE442 below DE441 changes the selected product. It does not cross
provider/method boundaries such as SPK versus OPM2. The function accepts either
an exact loaded path or a bare filename; a repeated call replaces the previous
value for that key. The supplied value replaces the file's numeric provider
default, so it can be greater or smaller than built-in candidates.
Path and basename matching is case-insensitive on Windows and case-sensitive
on POSIX systems.
`taiyin_runtime_clear_ephemeris_source_priority()` removes one override and
restores that file's default; `taiyin_runtime_clear_all_ephemeris_source_priorities()`
removes the whole overlay. Selection reads the table on every route choice, so
changes also affect files discovered before the call without rebuilding the
catalog. Do not mutate it concurrently with calculations.

After initialization, bindings can inspect the successfully registered runtime
data with `taiyin_runtime_registered_data_source_count()` and
`taiyin_runtime_get_registered_data_source()`. Each result reports its kind,
format, descriptor/sample count, coverage envelope, and physical source path.
Built-in sources use stable labels such as `builtin:semi-analytic` and
`builtin:eop`. Multiple descriptors from one physical OPM2, SPK, or TKC1 file
are aggregated into one result. A missing expected path therefore means it was
not registered successfully.

The source string uses the usual two-call buffer contract:

```c
size_t count = taiyin_runtime_registered_data_source_count();
for (size_t i = 0; i < count; ++i) {
    taiyin_runtime_registered_data_source info;
    char source[2048];
    size_t required = 0;
    taiyin_runtime_registered_data_source_init(&info);
    if (taiyin_runtime_get_registered_data_source(
            i, &info, source, sizeof(source), &required)
            == TAIYIN_STATUS_OK) {
        printf("%s: %llu items\n", source,
               (unsigned long long)info.item_count);
    }
}
```

TSC1/TSF1 catalogs remain in the separate star-catalog store and are queried
through the star-catalog API; they are intentionally not mixed into this
ephemeris-runtime inventory.

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
