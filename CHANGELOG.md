# Changelog

## 1.1.0

- Add native C++/C ABI BaZi reverse lookup, returning maximal matching
  half-open UT1 intervals with the existing chart clocks and rat-hour rules.
- Share the chart-clock implementation with Ziwei while preserving its
  existing symbols. Refine apparent-solar inverse boundaries from either side.
- Align forward historical pillar-month selection and reverse intervals at
  assigned civil midnights, including assignments several days from the root.
- Add the opt-in experimental Wasm bridge and reproducible native/browser
  comparison tools. Existing native interfaces and data formats are retained.
- Validate Web integer/string transport and callback ownership across setup,
  reset and re-registration; include a checked astronomy bridge generator.
- Retain C ABI 11 and optional-module ABI 1. Optional-module shared-library
  file versions now follow the project release version; SONAMEs remain stable.

## 1.0.0

- First stable release, retaining C ABI 11 and the numerical implementation
  validated in 1.0.0-rc.3.
- Document the 1.x C ABI compatibility policy, modular core/BaZi/Ziwei
  deployment, and matching Python/Dart packages.
- Align English and Chinese release, ABI, and semi-analytical coverage notes.

The stable release keeps the documented observer, satellite, time-zone, and
stellar-model limits. See [current limits](docs/current_limitations.md) and
[data packages](docs/ephemeris_data_packages.md).
