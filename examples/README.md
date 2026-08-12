# C++ Examples

All examples are normal CMake targets and are expected to compile with the
same C++17 toolchain as the library. Unless stated otherwise, each executable
accepts an optional Taiyin data-root argument and otherwise tries
`TAIYIN_DATA_ROOT` and `./data`.

| Target | What it demonstrates | Example epoch |
| --- | --- | --- |
| `example_observed_ut_bare_chart` | Topocentric observed positions, horizontal coordinates, and refraction | 1046 BCE-01-20 06:30 UT |
| `example_observed_utc_eop_bare_chart` | Precise modern UTC conversion with leap seconds and EOP | 2024-01-01 12:00 UTC |
| `example_apparent_ut_chart_table` | Geocentric apparent positions and correction/model choices | 1046 BCE-01-20 06:30 UT |
| `example_eclipse_search` | Lunar/solar search, local circumstances, and route-map polygons | 2024/2025 eclipses |
| `example_chinese_calendar_han` | Historical Chinese lunisolar conversion across the modeled Taichu reform, round trip, and solar terms | 104 BCE-01-20 UTC+08 |
| `example_ganzhi_bce` | Four Ganzhi pillars and NaYin without the BaZi extension | 1046 BCE-01-20 06:30 UTC+08 |
| `example_muye_jupiter` | Jupiter at midnight, culmination, the four-pillar time, and sunrise for a Muye candidate site | 1046 BCE-01-20 UTC+08 |

Build all example targets:

```sh
cmake -S . -B build
cmake --build build --target \
  example_observed_ut_bare_chart \
  example_observed_utc_eop_bare_chart \
  example_apparent_ut_chart_table \
  example_eclipse_search \
  example_chinese_calendar_han \
  example_ganzhi_bce
```

For example:

```sh
./build/example_ganzhi_bce /path/to/taiyin/data
./build/example_chinese_calendar_han /path/to/taiyin/data
```

Taiyin uses astronomical year numbering internally: year `0` is 1 BCE, so
1046 BCE is passed as year `-1045`. Ganzhi calculation for that epoch is a
solar-term and sexagenary-cycle calculation. It is intentionally separate from
the Han-era lunisolar example: applying a proleptic lunisolar calendar to the
late Shang date would not establish the historically observed month name.

The UTC/EOP example remains modern because leap-second and measured
Earth-orientation tables do not exist for BCE dates. The UT examples instead
use Taiyin's estimated historical time-scale route.
