# Numeric dump format

The debug/differential format is a sequence of signed 64-bit integers. It is
not a native-memory image and does not depend on padding, endianness, enum ABI,
or localized labels. Every record starts with:

```text
[format_version, record_kind, ...]
```

Version 3 uses kind `1` for a `Chart` and kind `2` for `ResolvedFlow`. Star
positions use `-1` when the star is absent from a particular layer.

## Chart, version 3

```text
version, kind, star_count, flow_count,
31 flattened anchors,
body_palace, gender, life_master, body_master,
12 palace stems,
natal transforms [lu, quan, ke, ji],
star_count natal star positions,
star_count natal transformation masks,
for each flow layer:
  level, life_palace, coordinate_stem, coordinate_branch,
  transforms [lu, quan, ke, ji],
  star_count layer star positions
```

The total length is:

```text
55 + 2 * star_count + flow_count * (8 + star_count)
```

Layers must be contiguous from Decade. Invalid or internally inconsistent
charts are rejected rather than serialized ambiguously.

## ResolvedFlow, version 3

The fixed 49-value record contains:

```text
version, kind,
effective_birth_year, effective_target_year,
target_month, target_month_sequence, target_day, target_hour_index,
target_rat_hour_segment,
target_month_is_leap,

decade limit [level, stem, branch, natal_role],
decade index, start_age, end_age, start_year, end_year, is_childhood,

small limit [stem, branch, natal_role, virtual_age],

year limit [level, stem, branch, natal_role], year,

month limit [level, stem, branch, natal_role],
month year, logical_month, sequence, is_leap, doujun,

day limit [level, stem, branch, natal_role], day,
hour limit [level, stem, branch, natal_role],
hour_index, rat_hour_segment
```

Rat-hour segment encoding is `0=None`, `1=Unified`, `2=Early`, `3=Late`.
Each natal transformation mask uses bits 0--3 for birth-year Lu/Quan/Ke/Ji,
bits 4--7 for centrifugal/self, and bits 8--11 for centripetal marks.

The format version is independent of the eventual stable C ABI. Adding or
reordering fields requires a new numeric dump version.
