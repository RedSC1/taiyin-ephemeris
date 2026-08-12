# Chinese Lunisolar Calendar

The `taiyin_chinese_calendar_extension` target converts between civil solar
dates and Chinese lunisolar dates. Its year calculation follows a
winter-solstice-year structure:

1. find the winter solstice at or before the requested civil day;
2. calculate 25 solar terms through the following winter solstice;
3. calculate 15 geocentric astronomical new moons;
4. assign month lengths and apply the no-major-term leap-month rule;
5. apply the selected civil-day and historical calendar rules.

The extension has no global calendar singleton. Every operation receives a
`ChineseCalendarContext`, or the corresponding opaque C
`taiyin_chinese_calendar_context`.

## Runnable Examples

Two standalone C++ programs cover the calendar layers without enabling BaZi:

- [`examples/chinese_calendar_han.cpp`](../examples/chinese_calendar_han.cpp)
  converts 104 BCE-01-20 across the modeled Taichu reform in the historical
  China profile, converts the result back to the same solar date, and queries
  the adjacent solar terms;
- [`examples/ganzhi_bce.cpp`](../examples/ganzhi_bce.cpp) calculates the four
  Ganzhi pillars and NaYin for 1046 BCE-01-20 06:30 at UTC+08. It produces
  `Jia-Wu / Ding-Chou / Jia-Zi / Ding-Mao` (甲午、丁丑、甲子、丁卯).

Build and run them from the repository root:

```sh
cmake -S . -B build
cmake --build build --target example_chinese_calendar_han example_ganzhi_bce
./build/example_chinese_calendar_han /path/to/taiyin/data
./build/example_ganzhi_bce /path/to/taiyin/data
```

The argument is optional; both programs fall back to `TAIYIN_DATA_ROOT` and
then `./data`. Taiyin uses astronomical year numbering in API structures, so
1046 BCE is year `-1045` and 104 BCE is year `-103`.

These examples deliberately use different epochs. The Ganzhi result at the
late-Shang date is a solar-term and sexagenary-cycle calculation. The
lunisolar conversion example stays inside the historical calendar profile's
Han-era coverage instead of presenting a proleptic lunar month as a recorded
late-Shang calendar date.

[`examples/muye_jupiter.cpp`](../examples/muye_jupiter.cpp) reuses the
1046 BCE date for a neutral observed-sky demonstration at a Muye candidate
site (`35.50 N, 114.10 E`). With the built-in semi-analytic route it reports
Jupiter at about `78.19 deg` altitude at midnight, culminating at about
`00:13:44 UTC+08` and `78.57 deg`, and below the horizon at the calculated
`07:41:36 UTC+08` sunrise. The example requests physical Jupiter and explicitly
allows the Jupiter-system barycenter when the historical semi-analytic route
does not provide a separate body-center state. It does not attach a historical
interpretation to the computed positions.

[`examples/tang_833_antares.cpp`](../examples/tang_833_antares.cpp) combines
the historical calendar, Ganzhi, named-star catalog, body-star minimum-
separation search, and longitude-station search. It resolves the New Book of
Tang record “Taihe 7, fifth month, Jia-Chen: Mars guarded Xin's central star”
to `833-06-09` in the modeled Julian/UTC+08 civil-day convention. The built-in
semi-analytic route places the surrounding Mars-Antares minimum at about
`0.8694 deg` on `833-06-30`, with a Mars longitude station about six days
earlier. Build and run it with:

```sh
cmake --build build --target example_tang_833_antares
./build/example_tang_833_antares /path/to/taiyin/data
```

## Time And Civil-Day Semantics

`SolarTermEvent::jd_ut` and `NewMoonEvent::jd_ut` are astronomical UT
split-Julian dates. `calcY()` and its C ABI counterpart likewise accept a
split-Julian UT value. These instants do not change with the civil-day policy.

For modern legal calendars, use an explicit fixed UTC offset. For example,
`fixed_utc_offset_config(480)` uses UTC+8 and
`fixed_utc_offset_config(420)` uses UTC+7. Taiyin does not contain a time-zone
database and does not infer daylight-saving or historical legal time rules.

Mean-solar meridian mode is a separate opt-in policy. It derives the civil-day
offset from the selected reference meridian:

```text
civil offset hours = calendar_meridian_deg / 15
```

This is useful for proleptic local-mean-time experiments, not as a replacement
for a legal time zone. Neither policy is topocentric: latitude and observer
height are intentionally absent, and Taiyin calculates the same geocentric
Sun-Moon event instant in every profile.

## Rule Profiles

`historical_china_config()` is the C++ default. It uses a generated historical
profile of fixed UTC+08 civil-day assignments, early calendar year starts, and
exceptional month names. The profile values are calendar day numbers, not
astronomical event instants in UT. Its civil-day boundary is fixed UTC+8.
Because the profile encodes Chinese civil-day assignments, this rule profile
rejects any other day-boundary policy.

`fixed_utc_offset_config(offset_minutes)` uses Taiyin astronomical solar terms
and new moons for every era and assigns them to an explicit fixed civil offset.
`fixed_meridian_config(longitude_deg)` selects the same astronomical rules but
uses a local-mean-solar day boundary. These profiles do not apply historical
calendar corrections.

The generated profile is used only inside its historical coverage interval and
is never extrapolated across `-13000..17000`. Before the profile starts and
after it ends, event instants and civil-day assignment use Taiyin's
astronomical event results. The precise event instant remains available through
the event API's `jd_ut` field even when the historical profile supplies its
`civil_day_number`.

Within the early historical profile, `LunarDate::year` follows the profile's
calendar-year starts. In the Zhuanxu/Qin-Han winter-year branch, the year label
is anchored to the civil year in which that winter year starts. It is not
derived from the midpoint of whichever winter-solstice window happened to be
used by `calcY()`. This keeps `fromSolar()` and `fromLunar()` bijective across
the modeled Taichu reform, where the old and new year-start conventions meet.

The C ABI exposes the same distinction through
`taiyin_chinese_calendar_config_init()`,
`taiyin_chinese_calendar_config_init_utc_offset()`, and
`taiyin_chinese_calendar_config_init_meridian()`. It deliberately uses
explicit rule and day-boundary modes instead of overloading a numeric longitude
sentinel.

## PMO 2026 Comparison

The independent oracle is the Chinese Academy of Sciences Purple Mountain
Observatory's "2026 Calendar Data". PMO publishes Beijing time rounded to the
minute; the Taiyin values in the tables use the packaged OPM2
`major-bodies/600y` route, the UTC+8 profile, and millisecond output. The
rounded PMO minute is shown alongside the result, not treated as a second-level
truth from which to report a difference.

| Solar term | PMO Beijing time | Taiyin Beijing time |
| --- | --- | --- |
| Minor Cold | 2026-01-05 16:23 | 2026-01-05 16:23:09.529 |
| Major Cold | 2026-01-20 09:45 | 2026-01-20 09:44:56.144 |
| Start of Spring | 2026-02-04 04:02 | 2026-02-04 04:02:08.007 |
| Rain Water | 2026-02-18 23:52 | 2026-02-18 23:51:55.582 |
| Awakening of Insects | 2026-03-05 21:59 | 2026-03-05 21:58:59.177 |
| Spring Equinox | 2026-03-20 22:46 | 2026-03-20 22:45:57.523 |
| Pure Brightness | 2026-04-05 02:40 | 2026-04-05 02:39:59.263 |
| Grain Rain | 2026-04-20 09:39 | 2026-04-20 09:39:06.570 |
| Start of Summer | 2026-05-05 19:49 | 2026-05-05 19:48:43.742 |
| Grain Full | 2026-05-21 08:37 | 2026-05-21 08:36:44.404 |
| Grain in Ear | 2026-06-05 23:48 | 2026-06-05 23:48:21.968 |
| Summer Solstice | 2026-06-21 16:25 | 2026-06-21 16:24:30.423 |
| Minor Heat | 2026-07-07 09:57 | 2026-07-07 09:56:57.451 |
| Major Heat | 2026-07-23 03:13 | 2026-07-23 03:13:05.300 |
| Start of Autumn | 2026-08-07 19:43 | 2026-08-07 19:42:44.618 |
| End of Heat | 2026-08-23 10:19 | 2026-08-23 10:18:48.509 |
| White Dew | 2026-09-07 22:41 | 2026-09-07 22:41:17.322 |
| Autumn Equinox | 2026-09-23 08:05 | 2026-09-23 08:05:13.248 |
| Cold Dew | 2026-10-08 14:29 | 2026-10-08 14:29:17.579 |
| Frost Descent | 2026-10-23 17:38 | 2026-10-23 17:37:56.380 |
| Start of Winter | 2026-11-07 17:52 | 2026-11-07 17:52:04.424 |
| Minor Snow | 2026-11-22 15:23 | 2026-11-22 15:23:20.643 |
| Major Snow | 2026-12-07 10:53 | 2026-12-07 10:52:31.555 |
| Winter Solstice | 2026-12-22 04:50 | 2026-12-22 04:50:14.258 |

All 50 principal lunar phases use the same minute-level comparison rule.

| Phase | PMO Beijing time | Taiyin Beijing time |
| --- | --- | --- |
| Full | 2026-01-03 18:03 | 2026-01-03 18:02:54.592 |
| Last quarter | 2026-01-10 23:48 | 2026-01-10 23:48:23.634 |
| New | 2026-01-19 03:52 | 2026-01-19 03:51:58.992 |
| First quarter | 2026-01-26 12:47 | 2026-01-26 12:47:23.563 |
| Full | 2026-02-02 06:09 | 2026-02-02 06:09:14.769 |
| Last quarter | 2026-02-09 20:43 | 2026-02-09 20:43:06.518 |
| New | 2026-02-17 20:01 | 2026-02-17 20:01:09.140 |
| First quarter | 2026-02-24 20:28 | 2026-02-24 20:27:36.788 |
| Full | 2026-03-03 19:38 | 2026-03-03 19:37:53.804 |
| Last quarter | 2026-03-11 17:39 | 2026-03-11 17:38:30.747 |
| New | 2026-03-19 09:23 | 2026-03-19 09:23:28.802 |
| First quarter | 2026-03-26 03:18 | 2026-03-26 03:17:42.799 |
| Full | 2026-04-02 10:12 | 2026-04-02 10:11:58.071 |
| Last quarter | 2026-04-10 12:52 | 2026-04-10 12:51:39.159 |
| New | 2026-04-17 19:52 | 2026-04-17 19:51:48.314 |
| First quarter | 2026-04-24 10:32 | 2026-04-24 10:31:45.375 |
| Full | 2026-05-02 01:23 | 2026-05-02 01:23:10.778 |
| Last quarter | 2026-05-10 05:10 | 2026-05-10 05:10:27.942 |
| New | 2026-05-17 04:01 | 2026-05-17 04:01:02.875 |
| First quarter | 2026-05-23 19:11 | 2026-05-23 19:10:57.273 |
| Full | 2026-05-31 16:45 | 2026-05-31 16:45:12.548 |
| Last quarter | 2026-06-08 18:01 | 2026-06-08 18:00:31.535 |
| New | 2026-06-15 10:54 | 2026-06-15 10:54:10.191 |
| First quarter | 2026-06-22 05:55 | 2026-06-22 05:55:24.593 |
| Full | 2026-06-30 07:57 | 2026-06-30 07:56:41.272 |
| Last quarter | 2026-07-08 03:29 | 2026-07-08 03:28:59.596 |
| New | 2026-07-14 17:44 | 2026-07-14 17:43:37.007 |
| First quarter | 2026-07-21 19:06 | 2026-07-21 19:05:36.159 |
| Full | 2026-07-29 22:36 | 2026-07-29 22:35:43.525 |
| Last quarter | 2026-08-06 10:21 | 2026-08-06 10:21:29.397 |
| New | 2026-08-13 01:37 | 2026-08-13 01:36:45.065 |
| First quarter | 2026-08-20 10:46 | 2026-08-20 10:46:20.834 |
| Full | 2026-08-28 12:19 | 2026-08-28 12:18:32.129 |
| Last quarter | 2026-09-04 15:51 | 2026-09-04 15:51:13.844 |
| New | 2026-09-11 11:27 | 2026-09-11 11:27:00.004 |
| First quarter | 2026-09-19 04:44 | 2026-09-19 04:43:46.995 |
| Full | 2026-09-27 00:49 | 2026-09-27 00:49:02.497 |
| Last quarter | 2026-10-03 21:25 | 2026-10-03 21:25:03.689 |
| New | 2026-10-10 23:50 | 2026-10-10 23:50:05.148 |
| First quarter | 2026-10-19 00:13 | 2026-10-19 00:12:41.212 |
| Full | 2026-10-26 12:12 | 2026-10-26 12:11:48.857 |
| Last quarter | 2026-11-02 04:28 | 2026-11-02 04:28:27.151 |
| New | 2026-11-09 15:02 | 2026-11-09 15:02:06.998 |
| First quarter | 2026-11-17 19:48 | 2026-11-17 19:47:49.725 |
| Full | 2026-11-24 22:54 | 2026-11-24 22:53:33.799 |
| Last quarter | 2026-12-01 14:09 | 2026-12-01 14:08:39.953 |
| New | 2026-12-09 08:52 | 2026-12-09 08:51:51.191 |
| First quarter | 2026-12-17 13:43 | 2026-12-17 13:42:40.155 |
| Full | 2026-12-24 09:28 | 2026-12-24 09:28:14.194 |
| Last quarter | 2026-12-31 02:59 | 2026-12-31 02:59:29.710 |

The same regression data is also run through the built-in semi-analytic route.
Its maximum deviation from the printed minute is about `49.200s` for solar
terms and `31.701s` for lunar phases, still within the test's `60s` minute-level
tolerance. The detailed tables intentionally show the OPM2 route.

## Long-Range Ephemeris Coverage

The calendar layer does not extend the coverage of the configured ephemeris.
Calculations across `-13000..17000` require DE441 or complete OPM data that
covers that interval; the built-in semi-analytic route remains limited to its
documented range. Missing coverage returns an ephemeris or event-search error
instead of silently falling back to an unconfigured historical approximation.

An opt-in DE441 stress test samples 12 representative years from `-12999`
through `16999` and checks Sun/Moon coverage, ordered solar terms, synodic
intervals, and 29/30-day lunar months. Enable it with
`TAIYIN_CHINESE_CALENDAR_LONG_RANGE=1` and point
`TAIYIN_CHINESE_CALENDAR_DATA_ROOT` at the DE441 data directory.

## Year Result

`calcY()` and `taiyin_chinese_calendar_calc_year_ut()` return:

- 25 solar terms, from one winter solstice through the next;
- 15 new moons;
- up to 14 month records, including the look-ahead month required to determine
  the final month length;
- the leap-month index and the two winter-solstice civil days.

The last look-ahead month may be relabeled when the following
winter-solstice-year is calculated. Date conversion functions therefore use
only months beginning before the second winter solstice.

## Single Solar-Term Queries

`getSpecificJieQi(context, civil_year, term_index_from_vernal_equinox, ...)`
calculates one term directly. Its index follows a spring-based seasonal cycle:
`0` is the spring equinox and `18` is the winter solstice
in `civil_year`; `19` through `23` are Xiaohan through Jingzhe earlier in the
same Gregorian year. The C ABI name is
`taiyin_chinese_calendar_get_specific_jie_qi_ut`.

The index identifies a seasonal crossing, not an absolute proleptic-Gregorian
date guarantee. Modern `1800..2400` coverage is regression-tested to render
all 24 selected terms in `civil_year`. At remote historical epochs, the
astronomical and historical-calendar models may render a crossing in an
adjacent proleptic Gregorian year; this is also the behavior of `calcY()`.

`getPrevJieQi()` / `getNextJieQi()` query one term without constructing lunar
months or the complete 25-term year. `getPrevJie()` / `getNextJie()` filter to
the twelve jie, while `getPrevQi()` / `getNextQi()` filter to the twelve qi.
Their C ABI names use the same `get_prev_*_ut` and `get_next_*_ut` pattern.

`Prev` includes a term exactly at the split-JD input UT instant; `Next`
advances to the subsequent term. The native API does not add a wall-clock
serialization tolerance. The standalone result's
`index_from_winter_solstice` is modulo 24: `0` is Dongzhi, `1` is Xiaohan,
then values advance by 15 degrees through `23` (Daxue).
In contrast, `calcY().solar_terms[24]` preserves `24` to identify the next
winter solstice in its 25-term sequence; the two values represent the same
event but serve different sequence contracts.

For all terms of one lunisolar year, prefer one `calcY()` call. Standalone
queries independently locate the preceding winter solstice and are intended
for occasional single-term lookup.

## Conversion

The public conversion functions are:

```cpp
fromSolar(context, solar, lunar, diagnostic);
fromLunar(context, lunar, solar, diagnostic);
getLunarMonthNum(context, year, month, is_leap, days, diagnostic);
```

`LunarDate` is deliberately structured: `year`, numeric `month`, `day`,
`is_leap`, and the exceptional `month_name` ID. A normal `month_name` lets the
calendar profile resolve the ordinary name; `thirteen`, `later nine`,
`alternate twelve`, and `alternate one` can select the historical names
explicitly. `fromLunar()` validates that the selected month exists in that
lunar year and that the requested day fits its actual 29/30-day length.

`getLunarMonthNum()` has no `month_name` parameter. It prefers the ordinary
month when a historical reform creates the same numeric identity more than
once; if no ordinary month exists, it accepts the exceptional historical name.
This keeps exceptional-only months such as a leap thirteenth month queryable
without weakening the exact structured identity used by `fromLunar()`.

`later same name` distinguishes the later occurrence when a restoration
boundary creates two months with the same written numeric name in one lunar
year. This occurs for the second 十二月 of lunar year 700 and the second 五月
of lunar year 762. The marker does not change localized display and does not
set `is_leap`; render the ordinary name from the numeric `month` field. Keeping
the `month_name` returned by `fromSolar()` makes these dates invertible through
`fromLunar()`.

The C++ and C APIs do not parse localized UTF-8 strings such as `"九月"`,
`"闰五月"`, or `"后九月"`. That user-input normalization belongs in a binding
or application layer, which should map the text to the structured fields above
and then call `fromLunar()`. Keeping localized aliases outside the numerical
runtime avoids making language, typography, and whitespace policy part of the
stable C ABI.

All public values are fixed-width PODs. The native C++ structs and the C ABI
structs are deliberately separate representations. The C ABI uses
initialized, size-versioned structs suitable for Dart, Python, and JavaScript
FFI. Do not copy or reinterpret-cast between the C++ and C layouts; use the C
ABI entry points and their field-wise conversion layer.

Regression tests cover the 2033 leap-eleven-month case, historical BCE year
numbering, the Taichu/Xin/Jingchu/Wu-Zetian reform windows, modern bidirectional
conversion, and a real UTC+8/UTC+7 civil-day boundary case. The latter is also
checked against the mathematically equivalent 105-degree mean-solar boundary.
The Purple Mountain Observatory 2026 calendar is an independent minute-level
oracle for all 24 solar terms, all 50 principal lunar phases, lunar-month sizes,
and printed lunar-month boundaries. Folk-calendar derivatives such as meiyu,
dog days, and nine-nine winter counting are intentionally outside this module.

## Ganzhi Calendar

The calendrical Ganzhi layer is part of Chinese Calendar. Its four-pillar
year/month/day/hour labels are reused by BaZi, Qimen, Liuren, and ordinary
sexagenary-date applications. It does not create another context:
`calculate_four_pillars()` consumes the existing `ChineseCalendarContext`, an
absolute split-JD UTC instant, the caller-resolved civil/solar `virtual_time`,
and an explicit Rat-hour rule.

The five-tiger month rule, five-rat hour rule, sexagenary construction,
cycle advancement, and NaYin ID/five-element lookup are implemented in the
native C++ rule unit. NaYin is calendrical data attached to a sexagenary value,
so it is available through `taiyin_ganzhi_*` even when BaZi is not built. The
C++ calendar layer also supplies split-JD solar-term boundaries and calendar
orchestration. The C ABI exposes the same split as `taiyin_ganzhi_*` and
`taiyin_chinese_calendar_calc_four_pillars_ut()`.

BaZi is a separate optional Chinese-metaphysics interpretation layer. Building it
requires both `TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON` and
`TAIYIN_BUILD_BAZI_EXTENSION=ON`; the policy gate never enables BaZi or any
future metaphysics module by itself. The Ganzhi calendar layer is always built
into Chinese Calendar and is available to ordinary sexagenary-date applications
that do not use a metaphysics interpretation module.

BaZi accepts a completed
`taiyin_ganzhi_four_pillars` value and attaches the calendrical NaYin IDs,
then adds hidden stems, ten gods, life stages, Ming Gong, Shen Gong, Tai Yuan,
and Tai Xi. `taiyin_bazi_collect_chart_relations()` returns the merged
stem/branch interaction graph: combinations, clashes, restraint, harm,
destruction, punishment, hidden combinations, severance, triple combinations,
triple directions, half combinations, and arching combinations. It defaults to
the four primary pillars; callers opt into Ming Gong, Shen Gong, Tai Yuan, and
Tai Xi with the pillar mask. It no longer owns an astronomy or Chinese-calendar
context.

Ganzhi and BaZi use the project C++ toolchain, including ordinary CMake
cross-compilation and Apple universal builds; no language-specific target
overrides or post-build archive merge are required.
