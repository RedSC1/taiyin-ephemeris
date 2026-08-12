# BaZi Extension

BaZi is an optional Chinese-metaphysics layer built on Taiyin's astronomy,
Chinese-calendar, and Ganzhi calculations. Ganzhi and four-pillar calendar
labels remain part of the base `taiyin` runtime; interpretation rules such as
hidden stems, Ten Gods, luck cycles, relations, and Shen Sha live here.

## Build And Library Boundary

Enable both extension switches:

```sh
cmake -S . -B build-bazi \
  -DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON \
  -DTAIYIN_BUILD_MODULAR_C_API=ON
cmake --build build-bazi
ctest --test-dir build-bazi --output-on-failure
```

In a modular install, `taiyin` contains the core, astrology, Chinese calendar,
and Ganzhi APIs. Enabling BaZi adds the optional `taiyin_bazi` shared library,
which depends on `taiyin`. Applications using BaZi load/link both; applications
that only need calendrical Ganzhi use `taiyin` alone.

The native C++ header is `bazi_astrology/include/taiyin/bazi/bazi.h`. The stable
C ABI header is `bazi_astrology/include/taiyin/c/bazi.h`.

## Calculation Pipeline And Ownership

The boundary is intentionally explicit:

```text
NativeCalcContext
        |
        v
ChineseCalendarContext -- calculate_four_pillars() --> GanzhiFourPillars
        |                                                   |
        |                                                   v
        +------ solar-term queries --------------------> BaziContext
                                                            |
                                                            v
                                                        BaziChart
```

`BaziContext` owns only BaZi configuration. It does not own an ephemeris or
Chinese-calendar context. Most rule operations need only a completed chart.
The two time-dependent operations, Qi-Yun and instantaneous Renyuan-Siling,
receive a `ChineseCalendarContext` explicitly so they use the caller's chosen
ephemeris route and civil-day profile.

A normal sequence is:

1. initialize the Taiyin runtime and a native calculation context;
2. initialize one `ChineseCalendarContext` with the intended UTC offset or
   meridian profile;
3. calculate the birth instant's `GanzhiFourPillars`;
4. initialize `BaziContext`, then call `calculate_chart()`;
5. reuse the chart for relations, Shen Sha, Xiao-Yun, Qi-Yun, and Da-Yun.

Callers remain responsible for resolving the wall clock, time zone, and any
true/mean-solar-time policy before four-pillar calculation. BaZi does not infer
a legal time zone from longitude.

## Chart And Rule Results

`BaziChart` contains:

- natal year, month, day, and hour pillars;
- Ming-Gong, Shen-Gong, Tai-Yuan, and Tai-Xi;
- hidden stems and their counts for each natal pillar;
- visible and hidden Ten-God IDs;
- twelve-life-stage IDs and NaYin IDs.

Pure helpers expose Kong-Wang, Ten Gods, hidden stems, pair/triple stem-branch
relations, life stages, and flow year/month/day/hour pillars. IDs and masks are
stable numeric data; localized names and presentation belong in bindings or
applications.

## Luck Cycles

Qi-Yun direction currently follows year-stem polarity and gender. The context
selects one of three start-time models:

- traditional calendar components (three days per year, decomposed into
  360-day years and 30-day months);
- continuous 365.25-day Julian years;
- continuous Taiyin mean tropical years.

Da-Yun boundaries can likewise use civil, Julian, or tropical ten-year steps.
`fill_dayun()` returns half-open astronomical boundaries
`[start_jd_ut, end_jd_ut)` plus inclusive traditional virtual-age display
ranges. Xiao-Yun uses one-based virtual ages and an explicit direction.

## Relations, Shen Sha, And Renyuan-Siling

`collect_chart_relations()` returns a merged interaction graph selected by a
pillar mask and a relation-kind mask. Primary and extra pillars can be included
independently. Combination results carry a combined five-element ID when the
rule defines one.

Shen Sha results are a 66-ID bitset. Use
`collect_target_shen_sha_with_gender()` when the legacy gender-dependent rules
are required; the gender-neutral function remains available for callers
without a gender profile. Stable IDs are documented in
`bazi_astrology/shen_sha_ids.md`.

Renyuan-Siling exposes both the `San Ming Tong Hui` table and a compatibility
common table. A result may measure continuous elapsed 24-hour days since the
previous Jie or crossed local civil-day boundaries. The latter uses the
supplied Chinese-calendar context's day-boundary policy.

## C ABI Array Convention

Variable-size C results use the standard two-call pattern: call with
`out = NULL` and `capacity = 0` to obtain `out_count`, allocate that many
records/words, initialize every versioned struct, then call again. This applies
to Xiao-Yun, Da-Yun, relations, Shen Sha words, and Renyuan-Siling segments.

## Validation

The native migration is covered by rule tests, C/C++ API tests, modular symbol
and install-smoke checks, calendar-integration tests, and 10,000-chart benchmark
and Qi-Yun record generators. Differential Qi-Yun verification covers both
genders and compares split-JD/civil-time fields as well as direction and age
components. The archived Pascal implementation remains under `legacy/` as a
maintainer reference and is not linked into the active runtime.
