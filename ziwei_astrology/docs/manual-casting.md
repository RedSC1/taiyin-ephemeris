# Manual placement and casting charts

[中文说明](manual-casting.zh-CN.md)

These C++11 APIs are declared in `<taiyin/ziwei/placement.h>` and included by
`<taiyin/ziwei/ziweicore.h>`. Link `taiyin::ziwei`. The complete runnable example is
[`manual_casting.cpp`](../examples/manual_casting.cpp).

## Direct placement, without a date

```cpp
ZiweiDataCatalog catalog("ziwei_astrology/rules/default.toml");
const ZiweiContext ctx = catalog.create_context();
const CompiledRules& tables = ctx.compiled_tables();
PlacementInput input;
input.year_stem = 9;   // Gui; Jia = 0
input.year_branch = 7; // Wei; Zi = 0
input.month = 2; input.day = 30; input.hour_branch = 6;
PlacementResult result;
Status status = arrange_ziwei_stars(input, Gender::Male,
    ZiweiChartMode::TianPan, tables, &result);
// Use result only when status == TAIYIN_STATUS_OK.
```

Inputs are already-normalized placement parameters: month 1..12, day 1..30,
hour branch 0..11, year stem 0..9, year branch 0..11. They need not identify a
real lunar date. This entry does not convert leap months, early/late Zi hours,
UTC, or solar clocks; those belong before placement in the calendar path.
An optional final `const Bureau*` fixes the bureau instead of deriving it.

Unpaired year stem/branch values are allowed. Void-star inputs that cannot be
derived are recorded in `omitted_placements`, with the affected `StarId` and
missing `RuleInputSource` values. A rule requiring a day Ganzhi or hour stem
also remains unplaced when no real date is supplied. Missing/flow-only stars
have position `0xff`; this is not branch Zi. Other stars still compute normally.

`PlacementResult` contains physical star positions, palace bitsets, palace stems,
masters, a year-stem transformation set, and all twelve per-star transformation
bits (year 0..3, centrifugal/self 4..7, centripetal 8..11). Brightness remains a
rule-table lookup: `brightness_at(tables, star, branch, &brightness)`.

## Edit a birth chart without rewriting its birth

```cpp
PlacementPatch patch;
patch.month = 3;
patch.update_bureau = 1;
NatalChart edited, moved, restored;
Status status = modify_natal_chart(natal, patch, birth_options.anchor_options,
    tables, &edited);
if (status == TAIYIN_STATUS_OK)
    status = shift_natal_life_palace(edited, 1, &moved);
if (status == TAIYIN_STATUS_OK)
    status = reset_natal_chart(moved, &restored);
```

Here `natal` and `birth_options` come from the ordinary birth-chart path.
Use the same anchor options and compiled rules that produced the original chart.
Patch fields default to `-1` (leave unchanged); repeated edits accumulate.
`update_bureau` is `-1` to inherit the previous choice, `0` to retain the
**original** bureau, and `1` to derive it from the edited parameters. The initial
choice is false. Changing the bureau changes both star placement and the decade
start age/year; there is no separate hidden bureau used for limits.

| Preserved | Recomputed |
| --- | --- |
| Birth instant, virtual time, lunar date and actual solar/lunar pillars | Star positions using explicit overrides |
| Original life/body frame, palace stems, life/body masters | Year transformations if the year stem is explicitly overridden |
| Flow direction and independently resolved target dates | Self/centripetal transformations from new positions and original palace stems |
| Birth year used for limit chronology | Bureau and decade start ages/years only when requested |

An explicit year component overrides both solar and lunar rule inputs; absent
components retain their respective boundary-specific facts. A month override
recomputes month branch/stem/index; changing the year stem also updates the month
stem. An overridden day changes the placement day index, **not** the real day
Ganzhi. An overridden hour branch derives hour stem from the original day stem.

`shift_natal_life_palace()` changes palace roles and the life-relative decade/
childhood locations, not stars, body palace, palace stems, bureau, or dates.
Independent annual/small-limit physical branches do not rotate. Subsequent edits
retain the shift and still use the original unshifted life/body frame for rules.
Reset restores the original, not the immediately preceding edit.

Each function returns a value through `out`, leaving its source unchanged.
In-place `out == &source` is supported. The implementation retains one immutable
original snapshot, not an edit chain or an owning astronomy/calendar context.
Do not concurrently write the same output object; independent outputs can share
immutable tables and source charts. These are library-created chart values, not
an input format for arbitrary hand-mutated `NatalChart` structures.

Re-resolve flows from the **original** `ResolvedBirth` and the edited `NatalChart`.
Do not overwrite `ResolvedBirth::anchors` with edited anchors. When using a
`Chart`, replace its natal component and clear its old `flow_stack` before
rebuilding layers. The C API does this automatically for edit/shift/reset outputs.

## Reported numbers and random casting

`CastingChart` is independent of `NatalChart`: it has no birth facts or real-date
flow API. It does not invent a birth instant. Direct input, numbered input, and
random input all use the same placement engine and selected tables.

```cpp
CastingChart reported, sampled, replay;
Status status = casting_chart_from_number("123456", Gender::Male,
    ZiweiChartMode::TianPan, tables, &reported);
if (status == TAIYIN_STATUS_OK)
    status = random_casting_chart(Gender::Male, ZiweiChartMode::TianPan,
        tables, &sampled);
if (status == TAIYIN_STATUS_OK)
    status = casting_chart_from_index(sampled.index, Gender::Male,
        ZiweiChartMode::TianPan, tables, &replay);
```

- `make_casting_chart()` accepts manual `PlacementInput`, with an optional fixed bureau.
- `casting_chart_from_index()` accepts `0..259199` (`index-v1`). Hour varies fastest,
  then day, month, and sexagenary year. There are `60 × 12 × 30 × 12 = 259200`
  combinations for a fixed gender/mode/ruleset; gender and rules are not randomized.
- `casting_chart_from_number()` accepts an ASCII nonnegative decimal string,
  including values too large for 64-bit integers. Leading zeros are removed.
  JS `number-v1` uses FNV-1a over `ziwei-casting-number-v1:` plus canonical text,
  Mulberry32 candidates, then rejection sampling. `123456` maps to index **209225**.
  This is a library-defined mapping, not a claimed traditional school rule.
- `random_casting_chart()` uses unbiased rejection sampling, at most 128 draws.
  The default source is Windows BCrypt or `/dev/urandom` on Unix-like platforms.
  An unavailable source returns an error; it never silently falls back to a PRNG.
  The optional synchronous `CastingRandomUint32` source makes testing/replay possible.
  The callback is not retained and must return a status plus a uniform uint32 value.
  A caller-provided source shared across threads needs its own synchronization.

Uniform **input combinations** do not imply uniform distinct final plates. Number
mapping permits collisions; it does not turn a user's choice into fresh entropy.
Save the index **and** gender, chart mode, selected rules/version to reproduce a plate.

`modify_casting_chart()`, `shift_casting_life_palace()` and `reset_casting_chart()`
have the same frame/bureau semantics as birth-chart edits. Reset retains the
original random draw, index and canonical reported number; it does not draw again.

## C ABI and tests

`taiyin_ziwei_casting_chart*` is a separate opaque handle, not a natal chart handle.
Creation methods are `*_create`, `*_from_index`, `*_from_number` and `*_random`.
Use initialized `taiyin_ziwei_casting_options`/`taiyin_ziwei_placement_input`
structures. The summary reports current/original input, current/original bureau,
palace frame, masters, transformations, modifications and source index.
Bulk getters expose positions/masks and omissions; pass a NULL buffer to query
size. The brightness getter verifies context identity; the number getter returns
the canonical original decimal text. Destroy each returned handle explicitly.

Natal `taiyin_ziwei_chart_modify`, `*_shift_life_palace`, `*_reset` return new
handles; `*_get_placement` and `*_get_omitted_placements` expose edit metadata.
These rule-only calls use the existing packed call result with zero execution flags.
The random callback returns a plain `taiyin_status`, **not** a packed call result.

`ziwei_placement` checks 427 JS oracle records across both genders, all three
chart modes, all sixty year pairs, edits, shifts, restoration and number mapping.
The digest includes all positions and transformation masks, not only the Ziwei star.
Index decoding is checked exhaustively, without building 259200 charts in CI.
C ABI tests cover both shared and static builds, invalid widths, context mismatch,
callback rejection, retained metadata and real-calendar flow reconstruction.
