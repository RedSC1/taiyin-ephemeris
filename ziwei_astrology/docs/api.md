# Ziwei C++ API

The module is a C++11 value-oriented rule core. Include the umbrella header:

```cpp
#include <taiyin/ziwei/ziweicore.h>
```

Inside a Taiyin source build, link the `taiyin::ziwei` CMake target. Installed
language bindings should use the opaque C ABI in `<taiyin/c/ziwei.h>`. Modular
builds place that ABI in the separate `taiyin_ziwei` shared library, which
depends on the base `taiyin` runtime; aggregate builds include it in `taiyin`.

## TOML data and calculation contexts

`ZiweiDataCatalog(profile_path)` parses a profile and all variants in its
independent star, placement, twelve-life-stage, brightness, Si-Hua, and
optional master resources.
It is the heavy, reloadable file-owning layer. Keep it alive and call
`create_context()` for the default profile selections or
`create_context(selection)` for overrides. Context creation does not reread
TOML.

`ZiweiContext` is the lightweight immutable calculation view. Access its
`star_registry()` for metadata and its `compiled_tables()` for chart and flow
functions. An empty `ZiweiOptionSelection` inherits the profile; missing
profile selections resolve to `option1`. Placement, twelve-life-stage,
brightness, and Si-Hua options remain independent. `longevity` is intentionally
one whole-table option rather than twelve per-star overrides: it selects a
coherent Changsheng-through-Yang sequence. The bundled `option1` is water/earth
shared Changsheng; `option2` is fire/earth shared Changsheng and changes only
the earth-five bureau sequence.

`ZiweiDataCatalog::reload()` has a strong exception guarantee and atomically
publishes a new immutable snapshot. Existing contexts continue using their old
snapshot without locks. The old `load_rules_from_toml()` remains as a
compatibility one-shot loader but should not be used per chart.

## Removable JSON option modules

`ZiweiConfigLoader::compile_json()` accepts the former Dart `stars`,
`flow_stars`, brightness, Si-Hua, and master JSON shapes and compiles them once
into an immutable `ZiweiRuleModule`. `constant`, `anchor_offset`, `lookup`,
`lookup_offset`, and `pipeline` exist only at this loading boundary: their
bounded input domains are enumerated immediately and the module retains flat
answer tables.

Each module label is the option name contributed by that whole module across
placement, brightness, Si-Hua, flow-star, and master tables. Modules only add
options: they never replace or remove an option loaded from TOML. A label that
matches any catalog option or another user module is rejected instead of using
last-write-wins behavior. Unknown star keys declared by a module receive
ruleset-local IDs after the bundled range, while the built-in IDs remain
unchanged. Persist the stable star key rather than a local ID.
Numeric JSON references may address bundled catalog IDs only. References to
user-added stars must use their stable string keys because removing another
module may otherwise renumber ruleset-local IDs.
Component-wide placement and brightness defaults apply to catalog stars. A
module-owned star retains its module option unless the caller explicitly
selects another option for that star key.
In a composite registry, `natal_star_count` is a count, not an ID boundary:
custom natal stars are appended after all bundled IDs. Inspect
`StarMetadata::natal` (or `taiyin_ziwei_star_is_natal()`) instead of testing
`id < natal_star_count`.

```cpp
ZiweiJsonRuleModuleInput input;
input.label = "my-rules";
input.stars_json = R"json([
  {"key":"custom_star","type":"minor",
   "rule":{"type":"anchor_offset","anchor":"ziwei","offset":2}}
])json";

ZiweiRuleset ruleset = ZiweiConfigLoader::add_json_module(
    ZiweiConfigLoader::get_default(), input);
ZiweiOptionSelection selection;
selection.placement["custom_star"] = "my-rules";
ZiweiContext context = catalog.create_context(
    selection, ruleset);

// Removes every placement, brightness, Si-Hua, flow-star, master table, and
// new star contributed by this user module. It cannot remove TOML options.
ruleset = ruleset.remove_module("my-rules");
```

Contexts own an immutable composite registry and compiled table snapshot.
Creating a customized context does not mutate the catalog defaults or an
already-created context. Removing a module affects only contexts created from
the resulting ruleset; existing contexts retain their prior snapshot. Custom
natal and flow stars participate in the same
dynamic palace bitsets and transformation overlays as bundled stars.
For `masters_json`, an explicit `boundary` of `lunar` or `solar` selects that
year branch; when omitted, the bundled semantics remain unchanged (life master
uses the life palace and body master uses the context's selected year boundary).
Custom `brightness_labels` are presentation/localization text and are not
stored by the C++ core; bindings may preserve them separately.

The C ABI exposes the same lifecycle with `taiyin_ziwei_ruleset_create()`,
`taiyin_ziwei_ruleset_add_json_module()`, and
`taiyin_ziwei_ruleset_remove_module()`, plus
`taiyin_ziwei_context_create_with_ruleset()`.

Bundled placement resources contain only final finite mappings: `inputs`, their exact
`shape`, and a row-major `positions` array. They cannot express offsets,
directions, pipelines, conditions, or arbitrary expressions. Loading validates
all dimensions and compiles row-major strides; chart construction only indexes
the selected answer table.

`StarRegistry::find()` is intended for loading, presentation, and inspection,
not chart calculation. `brightness_at(rules, star, branch, out)` returns the
typed `Brightness` value (`None`, `Xian`, `Bu`, `Ping`, `Li`, `De`, `Wang`, or
`Miao`) for a star in a physical branch.

## Birth and natal chart

For real dates, use `resolve_birth_from_calendar()`. It accepts a caller-owned
`ChineseCalendarContext`, one physical UTC instant, the already-resolved local
or virtual wall clock, gender, and `BirthResolutionOptions`. The result
contains calendar facts, the stable 31 anchors, and body-palace metadata.

`make_natal_chart_from_calendar()` is the direct convenience entry point.
`compute_anchors()` and `make_natal_chart()` remain available when a binding or
test already owns normalized `CalendarFacts`.

`instant_utc` is the authoritative continuous time coordinate. The paired
`virtual_time` is interpreted using one fixed civil offset (or a selected
mean-solar meridian); the native module has no named time-zone or DST rules.
The adapter canonicalizes only this final charting clock at exact civil-hour
JD boundaries before deriving lunar labels, Ganzhi, natal facts, or flow
facts. It stores the canonical value in the natal result; `instant_utc` and
astronomical Jie boundaries remain untouched.
For a daylight-saving transition, it cannot independently derive the legal
wall-clock time of an earlier Jie or new moon. Applications must supply a
consistent fixed-offset interpretation and must not treat lunar-day labels near
such transitions as a reproduction of local legal time.

Important policies are explicit:

- `rat_hour_mode` is passed to Taiyin Ganzhi calculation;
- `leap_month_strategy` controls whether a leap month belongs to the previous
  month, next month, or splits after day fifteen;
- `chart_mode` selects Tian Pan, Di Pan, or Ren Pan;
- `rules.wu_hu_dun_year_boundary` selects the year stem used by 五虎遁;
- `rules.sihua_year_boundary` independently selects the natal Si-Hua stem;
- `rules.body_master_year_boundary` independently selects the body-master
  year branch.

All three boundary policies default to `PillarBoundary::Lunar`. They are
runtime calendar-source policies and remain independent from `option1`,
`option2`, and other answer-table variants.

`NatalChart::transformations` is a chart-level overlay, separate from the
palace placement bitsets. Its `birth_year` `TransformSet` names the four
birth-year targets, while `marks_by_star[StarId]` is a compact 12-bit
annotation: bits 0--3 are birth-year Lu/Quan/Ke/Ji, bits 4--7 are
`Centrifugal{Lu,Quan,Ke,Ji}` (自化, own palace stem), and bits 8--11 are
`Centripetal{Lu,Quan,Ke,Ji}` (向心, opposite palace stem).

The C ABI exposes the same normalized shape. `get_summary()` returns the four
birth-year targets; use one mask query for the complete per-star annotation:

```c
#include <stdbool.h>

uint16_t mask = 0;
taiyin_ziwei_chart_get_star_transformation_mask(chart, star_id, &mask);

bool birth_year_lu = (mask & (1u << TAIYIN_ZIWEI_BIRTH_YEAR_LU)) != 0;
bool self_ji = (mask & (1u << TAIYIN_ZIWEI_CENTRIFUGAL_JI)) != 0;
bool inward_ke = (mask & (1u << TAIYIN_ZIWEI_CENTRIPETAL_KE)) != 0;
```

`taiyin_ziwei_chart_has_star_transform_mark()` is the equivalent convenience
query when only one mark is needed.

## Reverse birth-time lookup

`reverse_lookup_tier1_from_calendar()` takes the same caller-owned calendar
context used for forward charts, a paired physical UTC instant and virtual
clock at the beginning of the search interval, and a `Tier1ReverseQuery`.
Its optional filters are the traditional key-star placements: Lu Cun, Hong
Luan, Zuo Fu, You Bi, Wen Chang, Wen Qu, San Tai, Ba Zuo, and Ziwei.

It enumerates logical hour slots through the interval and verifies every hit
by the ordinary `resolve_birth_from_calendar()` plus `make_natal_chart()`
path. A returned candidate is therefore a matching logical birth-time slot,
not a fabricated minute-precise reconstruction. Historical calendar policy,
leap-month handling, and all three Rat-hour conventions are inherited rather
than reimplemented. The opaque C ABI offers the same operation through
`taiyin_ziwei_reverse_lookup_tier1()` with a query-then-fill result buffer.

## Limits and flow stack

`make_decade_by_index()`, `make_decade_for_year()`, `make_small_limit()`, and
the `make_flow_*()` functions expose individual finite limit coordinates.
`make_limit_flow_layer()` materializes any formal limit as the common
`FlowLayer` representation.

`push_flow_layer()` enforces contiguous Decade, Year, Month, Day, Hour order.
`truncate_flow_stack()` removes one level and every more-specific level, so a
partial stack is a supported first-class state.

For a physical target time, `resolve_flow_from_calendar()` calculates decade,
small limit, year, month, day, and hour coordinates together.
`set_flow_stack_from_calendar()` atomically replaces a chart's formal
five-layer stack. Small limit remains parallel annual metadata rather than a
sixth layer.

Lunar flow preserves four distinct month facts: the written month,
`sequence` among physical months in the historical calendar year,
`effective_month` used by Wu-Hu-Dun/Si-Hua/flow stars, and the calendar's
`month_building_branch`. The default `FlowMonthPalaceStrategy::PhysicalSequence`
advances the flow palace for every physical month; `EffectiveMonth` makes a
leap-month segment follow its selected previous/next effective month instead.
Historical reform years are represented directly and may have sequences up to
15; they are not collapsed into a fabricated leap month twelve.

`set_flow_stack_through_from_calendar()` performs the same resolution but
installs only through a requested deepest level. It never creates a stack with
a missing parent layer.

`step_flow_hour_target()` and `step_flow_day_target()` move the paired physical
instant and virtual clock without retaining mutable manager state. In split
Rat-hour modes an hour carries `RatHourSegment::Early` or `Late`, producing 13
logical hour entries. `TOMORROW_GAN` preserves today's day pillar while using
tomorrow's stem for the late-Zi hour; the adapter passes that resolved stem
through instead of reconstructing it.

Hour stepping preserves the minute/second phase of the resolved virtual clock.
It moves one hour for forward targets in `[22:00, 01:00)` and backward targets
in `[23:00, 02:00)`, and two hours elsewhere. Consequently civil, local mean
solar, and local apparent solar configurations each use their actual charting
clock rather than an unrelated civil input.

## Numeric differential output

`flatten_anchors()`, `dump_natal_star_positions()`, and
`dump_flow_star_positions()` are the compact component-level forms.
`dump_chart_numeric()` and `dump_resolved_flow_numeric()` provide complete,
versioned records for bulk differential testing and language bindings. See
[numeric-dump.md](numeric-dump.md) for the stable field order.

Maintainers can build `dump_ziwei_exhaustive` and use
`tools/compare_exhaustive.py` with the Dart oracle. `--finite` enumerates
exactly 518,400 charts per Rat-hour convention
(`60 * 12 * 30 * 12 * 2`). Its record includes every natal position and one
twelve-bit transformation mask per natal star (birth-year, centrifugal/self,
and centripetal). The
physical-calendar 1984--2043 window remains
available for historical and boundary behavior; it contains 525,960 no-split
charts, or 569,790 charts per split convention. It is an occasional manual
audit only, never a CTest or CI job; targeted historical-reform and boundary
fixtures provide the normal regression coverage.

All status-returning functions validate inputs before replacing caller-owned
outputs. TOML parsing errors use `RuleLoadError` because they contain detailed
filename/key/schema diagnostics and occur outside the `noexcept` calculation
path.
