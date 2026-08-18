# Taiyin Ziwei core

This optional C++11 module is the rule engine for Ziwei Doushu. It depends on
Taiyin's Chinese-calendar/Ganzhi layer, but not on the optional BaZi runtime.

The implementation deliberately separates three stages:

1. Taiyin resolves the physical instant, local civil-time policy, lunar date,
   and the solar-term/lunar-boundary pillars into `CalendarFacts`.
2. `compute_anchors()` produces the stable 31 numeric anchors. Body palace is
   returned as chart metadata because it is not one of those 31 anchors.
3. `ZiweiDataCatalog` parses a TOML profile once. One or more lightweight
   `ZiweiContext` values select independent placement, twelve-life-stage,
   brightness, Si-Hua, and master options from that immutable catalog snapshot. Every official
   placement is already flattened into a final 1D, 2D, or fixed 3D answer
   table. Runtime placement only computes a row-major index and reads the
   resulting branch.

The bundled default rules are migrated from the author's MIT-licensed Dart
`ziwei_core` oracle. They contain 115 natal stars, 44 flow stars, brightness
tables, 命主/身主 tables, and the default ten-stem Si-Hua table. All old offset,
direction, and pipeline operations are evaluated by the offline migration
tool; none of those operations exists in the runtime schema.

The twelve life stages are a coherent option dimension: `option1` uses the
water/earth-shared Changsheng convention (the default); `option2` uses the
fire/earth-shared convention. Selecting `option2` changes only the earth-five
bureau's Changsheng through Yang placement, never the principal stars or
Si-Hua.

## Build

```sh
cmake -S . -B build \
  -DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON \
  -DTAIYIN_BUILD_ZIWEI_EXTENSION=ON
cmake --build build -j
ctest --test-dir build -L ziwei --output-on-failure
```

`taiyin_ziwei_extension` remains the build-tree C++ static target and
`taiyin::ziwei` is its namespaced alias. With
`TAIYIN_BUILD_MODULAR_C_API=ON`, CMake additionally builds the independently
deployable `taiyin_ziwei` shared library (`taiyin_ziwei.dll`,
`libtaiyin_ziwei.so`, or `libtaiyin_ziwei.dylib`) on top of the base `taiyin`
runtime. Without modular mode, the same Ziwei C ABI is included in the
aggregate `taiyin` library. The installed C surface is
`<taiyin/c/ziwei.h>`; it deliberately exposes opaque handles instead of a
native C++ ABI. The TOML files are installed under `share/taiyin/ziwei`.

The public C++ surface is summarized in [`docs/api.md`](docs/api.md). Complete
charts and resolved flow coordinates also have a versioned, label-free
numeric representation documented in
[`docs/numeric-dump.md`](docs/numeric-dump.md).

The C ABI mirrors the native ownership boundary:

```c
taiyin_ziwei_data_catalog* catalog = NULL;
taiyin_ziwei_context* ziwei = NULL;

taiyin_ziwei_data_catalog_create(profile_path, &catalog);
taiyin_ziwei_context_create(catalog, NULL, 0, &ziwei);
```

The catalog parses and validates all TOML variants once. Lightweight contexts
choose independent options, and opaque charts retain the resolved birth facts
needed by later flow calculations. `taiyin_ziwei_data_catalog_reload()`
publishes a new immutable generation without changing existing contexts.

## Minimal C++ path

The complete, compiled example is
[`examples/basic_chart.cpp`](examples/basic_chart.cpp). Its essential path is:

```cpp
using namespace taiyin;
using namespace taiyin::ziwei;

ZiweiDataCatalog data_catalog("default.toml");
const ZiweiContext ziwei_context = data_catalog.create_context();
const CompiledRules& tables = ziwei_context.compiled_tables();
const BirthResolutionOptions options = default_birth_resolution_options();
runtime::EphemerisEvalDiagnostic diagnostic;

ResolvedBirth birth;
Status status = resolve_birth_from_calendar(
    &calendar,       // caller-owned ChineseCalendarContext
    instant_utc,     // the physical instant
    local_time,      // the already-resolved wall/virtual clock
    Gender::Male,
    options,
    &birth,
    &diagnostic);

NatalChart natal;
status = make_natal_chart(
    birth.facts,
    birth.anchors,
    birth.body_palace,
    options.anchor_options.rules,
    tables,
    &natal);
```

The caller deliberately owns `ChineseCalendarContext`. Consequently the same
historical-China versus local-astronomical calendar policy, ephemeris route,
and time handling used by the rest of Taiyin also supplies Ziwei; the Ziwei
engine does not implement a second lunar calendar.

After a natal chart exists, a target instant can produce the complete dynamic
stack without the caller manually calculating leap-month sequence, physical
day stem, or hour branch:

```cpp
Chart chart;
chart.natal = natal;

ResolvedFlow flow;
status = set_flow_stack_from_calendar(
    &calendar,
    birth,
    target_instant_utc,
    target_local_time,
    default_flow_resolution_options(),
    tables,
    &chart,
    &flow,
    &diagnostic);
```

The resulting stack is always Decade, Year, Month, Day, Hour. Small limit is
returned as parallel annual metadata in `ResolvedFlow`. The default uses lunar
boundaries; `FlowResolutionOptions::boundary = PillarBoundary::SolarTerm`
selects the Jie-based year/month/day policy. When a historical reform interval
contains a fourteenth or later structural month, the Ziwei compatibility layer
keeps the date chartable by collapsing the overflow occurrence to sequence 13
as leap month twelve. The Chinese-calendar layer still retains its exact
historical month identity for reversible conversion.

`set_flow_stack_through_from_calendar()` installs only through a requested
deepest level while preserving the contiguous dependency chain. For example,
stopping at `FlowLevel::Month` produces Decade, Year, Month; a middle layer is
never removed while retaining children that depend on it.

`step_flow_hour_target()` and `step_flow_day_target()` provide stateless
previous/next navigation. Split Rat-hour schools expose thirteen logical
slots—Early Zi, Chou through Hai, and Late Zi—so the hour sequence is:

```text
Hai -> Late Zi -> next-day Early Zi -> Chou
```

The resolved `RatHourSegment` remains available on `ResolvedFlow` and
`FlowHourLimit`; a Zi branch is no longer ambiguous in bindings.

## Stable concepts

- `Anchors`: exactly 31 scalar inputs (two pillar sets, bureau, Ziwei, Tianfu,
  and twelve palace-role positions).
- `NatalChart`: anchors, body palace, gender, twelve branch-indexed palace
  bitsets, plus a chart-level transformation overlay. The overlay holds the
  natal Si-Hua target set and one compact 12-bit transformation mask per
  `StarId` (birth-year, self/centrifugal, and centripetal marks).
- `FlowLayer`: the single representation shared by decade, year, month, day,
  and hour layers. Small limit remains parallel annual metadata rather than a
  sixth layer.
- `StarId`: declaration-order `uint16_t`; strings exist only in the registry
  and TOML loading boundary.

The encodings are zero-based so stems, branches, palaces, and finite rule-table
domains index arrays directly. `DynamicBitset` is used because rule resources
may add stars beyond a fixed compile-time count. TOML is only the editable
answer-table format; `CompiledRules` is the validated, option-selected runtime
format:

```text
default.toml profile -> ZiweiDataCatalog (parse every option once)
                     -> ZiweiContext (select options; missing means option1)
                     -> CompiledRules -> NatalChart / FlowLayer
```

`ZiweiDataCatalog::reload()` parses and validates a complete replacement before
publishing it. Contexts created before reload retain the old immutable
snapshot; contexts created afterward see the replacement. Consequently chart
calculation takes no catalog mutex. Selecting another option creates another
lightweight context from the same parsed data and never rereads TOML.

No arithmetic instruction, TOML node, string lookup, or `unordered_map` lookup
occurs in chart placement. See [`rules/README.md`](rules/README.md) for the
resource split, provenance, and option policy.

Brightness values are likewise numeric runtime data. Use
`brightness_at()` to query the typed `Brightness` enum; localization such as
“庙/旺/得/利/平/不/陷” belongs in a binding or presentation layer.

`resolve_birth_from_calendar()` now adapts a caller-owned Taiyin
`ChineseCalendarContext` into both pillar sets, the lunar date, the solar-day
index, and all anchors. `make_natal_chart_from_calendar()` is the direct
calendar-to-chart convenience entry point. This preserves the calendar
context's historical/local policy instead of duplicating calendar rules in
the Ziwei module.

For reverse birth-time lookup, `reverse_lookup_tier1_from_calendar()` accepts
the same calendar context and a paired UTC/local-clock interval. It filters by
the traditional key-star placements (禄存、红鸾、左辅、右弼、文昌、文曲、三台、八座、紫微)
and verifies every candidate through the normal forward chart path. It returns
logical birth-time slots, not invented minute-level precision; historical
calendar and Rat-hour behavior therefore stay exactly aligned with normal
chart construction.

The ordinary CI differential suite covers 23 natal cases (2,645 natal-star
positions), 23 complete limit cases, the original single-case fixtures, all
129,600 finite anchor states, and all 240 stem/branch/gender flow coordinates.
The fixed corpus includes BCE dates, historical reform periods, both genders,
late Rat-hour clocks, a Li-Chun boundary date, a modern leap month, and the
2033 leap-eleven case.
The calendar-backed flow regression suite additionally locks complete
five-layer charts immediately before and after Li Chun and lunar New Year,
plus the Taichu, Xin, Jingchu, Wu-Zetian, and Tang restoration reform cases.
Every retained boundary chart verifies all Year/Month/Day/Hour coordinates
and all 44 flow stars in each of the five formal layers.
Maintainer builds additionally provide a streaming Dart-to-C++ exhaustive
comparison. The finite-state corpus enumerates exactly 60 year cycles, 12
months, 30 lunar days, 12 hour branches, and both genders: 518,400 charts per
Rat-hour convention. Each record includes anchors, palace stems, every natal
star position, and a per-star 12-bit transformation mask (birth-year,
self/centrifugal, and centripetal).
All three conventions therefore compare 1,555,200
charts without writing a generated corpus to disk. Run:

```sh
python3 ziwei_astrology/tools/compare_exhaustive.py \
  --dart-root /path/to/ziwei_core \
  --cpp build/ziwei_astrology/dump_ziwei_exhaustive \
  --finite --mode 0
```

Modes `0`, `1`, and `2` are respectively no-split, today-Gan, and
tomorrow-Gan. The comparator streams records from both processes, reports the
first mismatch, and does not commit a hundreds-of-megabytes generated corpus.
There is also a substantially slower physical-calendar corpus for occasional
manual boundary and historical-calendar audits. Over 1984 through 2043 it
contains 525,960 no-split charts, or 569,790 charts per split convention. It
is deliberately not registered as a CTest or CI job; the retained CTest
fixtures above cover the historical reforms and calendar boundaries.

The native C++ surface is still subject to review. The opaque C ABI is now
available for bindings, while higher-level Python/Dart object models remain
separate packaging work.

Maintainer builds can enable `TAIYIN_BUILD_MAINTAINER_TOOLS` and run
`benchmark_ziwei [iterations]` or build `dump_ziwei_exhaustive`. The benchmark loads TOML before timing and
measures the anchor plus natal-chart calculation path; it is an engineering
regression tool, not part of correctness testing.
