#!/usr/bin/env python3
"""Generate the historical UTC+08 civil-day profile from an SSQ black box.

The input JavaScript is executed only while regenerating this file. The C++
runtime stores civil-day assignments and a generated compression model; it does
not execute the source calendar formulas. Generated day numbers are fixed
UTC+08 civil days, not astronomical event instants in UT.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.optimize import linprog

DAY_SCALE = 1_000_000_000
MIN_EXACT_SEGMENT_EVENTS = 64
RANK_BLOCK_EVENTS = 256
HISTORICAL_END_JD = 2_436_935


@dataclass(frozen=True)
class LinearSegment:
    first_index: int
    event_count: int
    base_ticks: int
    step_ticks: int


@dataclass(frozen=True)
class SparseResiduals:
    nonzero_mask: list[int]
    sign_bits: list[int]
    rank_prefix: list[int]
    nonzero_count: int


NODE_DRIVER = r"""
const fs = require('fs');
const vm = require('vm');
const path = process.argv[1];
const context = {
  Math,
  J2000: 2451545,
  int2: Math.floor,
  dt_T: () => { throw new Error('high-precision branch used'); },
  XL: new Proxy({}, {
    get() { return () => { throw new Error('high-precision branch used'); }; }
  })
};
vm.createContext(context);
vm.runInContext(fs.readFileSync(path, 'utf8'), context, {filename: path});
const SSQ = vm.runInContext('SSQ', context);

function collect(kind, firstProbe, step) {
  const days = [];
  let probe = firstProbe;
  while (probe < 2436935) {
    const day = SSQ.calc(probe - 2451545, kind) + 2451545;
    if (days.length && day <= days[days.length - 1]) {
      throw new Error(`${kind}: non-increasing result at ${probe}`);
    }
    days.push(day);
    probe = day + step;
  }
  return days;
}

process.stdout.write(JSON.stringify({
  moon: collect('朔', SSQ.suoKB[0], 29.5306),
  solar_term: collect('气', SSQ.qiKB[0], 365.2422 / 24)
}));
"""


def load_oracle(node: str, lunar_js: Path) -> dict[str, list[int]]:
    result = subprocess.run(
        [node, "-e", NODE_DRIVER, str(lunar_js)],
        check=True,
        capture_output=True,
        text=True,
    )
    data = json.loads(result.stdout)
    for name, days in data.items():
        if not days or any(right <= left for left, right in zip(days, days[1:])):
            raise RuntimeError(f"{name} oracle is not strictly increasing")
    return data


def rounded_ticks(base_ticks: int, step_ticks: int, index: int) -> int:
    value = base_ticks + step_ticks * index
    return (value + DAY_SCALE // 2) // DAY_SCALE


def exact_segment(days: list[int], first: int, end: int) -> LinearSegment | None:
    count = end - first
    x = np.arange(count, dtype=np.float64)
    y = np.asarray(days[first:end], dtype=np.float64)
    constraints = []
    limits = []
    for index, day in zip(x, y):
        constraints.append((-1.0, -index, 1.0))
        limits.append(-(day - 0.5))
        constraints.append((1.0, index, 1.0))
        limits.append(day + 0.5)
    result = linprog(
        (0.0, 0.0, -1.0),
        A_ub=np.asarray(constraints),
        b_ub=np.asarray(limits),
        bounds=((None, None), (None, None), (0.0, None)),
        method="highs",
    )
    if not result.success:
        return None
    base_ticks = round(float(result.x[0]) * DAY_SCALE)
    step_ticks = round(float(result.x[1]) * DAY_SCALE)
    if any(
        rounded_ticks(base_ticks, step_ticks, index) != day
        for index, day in enumerate(days[first:end])
    ):
        return None
    return LinearSegment(first, count, base_ticks, step_ticks)


def longest_exact_segment(days: list[int], first: int) -> LinearSegment:
    low = first + 1
    high = len(days)
    best = exact_segment(days, first, low)
    if best is None:
        raise RuntimeError("single-event segment is unexpectedly infeasible")
    while low <= high:
        middle = (low + high) // 2
        candidate = exact_segment(days, first, middle)
        if candidate is not None:
            best = candidate
            low = middle + 1
        else:
            high = middle - 1
    return best


def split_exact_prefix(days: list[int]) -> tuple[list[LinearSegment], int]:
    """Keep long exact segments, stopping before a sustained short-segment run.

    A one-off short interval can occur between two long exact fits, so a single
    <64-event segment is not a boundary by itself. Three consecutive short
    fits mark the point where a sparse residual tail is more compact.
    """
    segments: list[LinearSegment] = []
    first = 0
    short_run = 0
    first_short_segment = 0
    while first < len(days):
        candidate = longest_exact_segment(days, first)
        if candidate.event_count < MIN_EXACT_SEGMENT_EVENTS:
            if short_run == 0:
                first_short_segment = len(segments)
            short_run += 1
            if short_run == 3:
                kept = segments[:first_short_segment]
                return kept, (
                    kept[-1].first_index + kept[-1].event_count if kept else 0
                )
        else:
            short_run = 0
        segments.append(candidate)
        first += candidate.event_count
    return segments, first


def fit_tail(
    days: list[int], first: int, seasonal_phases: int
) -> tuple[LinearSegment, list[int], list[int]]:
    values = np.asarray(days[first:], dtype=np.float64)
    index = np.arange(values.size, dtype=np.float64)
    columns = [np.ones(values.size), index]
    if seasonal_phases:
        phases = np.arange(values.size) % seasonal_phases
        columns.extend(
            (phases == phase).astype(np.float64)
            for phase in range(1, seasonal_phases)
        )
    design = np.column_stack(columns)
    coefficients = np.linalg.lstsq(design, values, rcond=None)[0]

    base_ticks = round(float(coefficients[0]) * DAY_SCALE)
    step_ticks = round(float(coefficients[1]) * DAY_SCALE)
    phase_ticks = [0]
    phase_ticks.extend(round(float(value) * DAY_SCALE) for value in coefficients[2:])

    residuals = []
    for local_index, day in enumerate(days[first:]):
        ticks = base_ticks + step_ticks * local_index
        if seasonal_phases:
            ticks += phase_ticks[local_index % seasonal_phases]
        prediction = (ticks + DAY_SCALE // 2) // DAY_SCALE
        residual = day - prediction
        if residual not in (-1, 0, 1):
            raise RuntimeError(
                f"tail residual {residual} outside [-1, 1] at event "
                f"{first + local_index}"
            )
        residuals.append(residual)
    return (
        LinearSegment(first, len(days) - first, base_ticks, step_ticks),
        phase_ticks,
        residuals,
    )


def pack_bits(bits: list[int]) -> list[int]:
    words = [0] * ((len(bits) + 63) // 64)
    for index, value in enumerate(bits):
        if value:
            words[index // 64] |= 1 << (index % 64)
    return words


def sparse_residuals(residuals: list[int]) -> SparseResiduals:
    mask_bits = [int(value != 0) for value in residuals]
    signs = [int(value > 0) for value in residuals if value]
    prefixes = []
    rank = 0
    for first in range(0, len(mask_bits), RANK_BLOCK_EVENTS):
        prefixes.append(rank)
        rank += sum(mask_bits[first:first + RANK_BLOCK_EVENTS])
    prefixes.append(rank)
    if rank > 0xFFFF:
        raise RuntimeError("uint16_t rank prefix overflow")
    return SparseResiduals(pack_bits(mask_bits), pack_bits(signs), prefixes, rank)


def oracle_hash(days: list[int]) -> str:
    payload = "".join(f"{day}\n" for day in days).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def sequential_phase_origin(
    days: list[int], *, padding: float, epoch: float, period: float
) -> int:
    phase_indices = [int((day + padding - epoch) // period) for day in days]
    first = phase_indices[0]
    if phase_indices != list(range(first, first + len(days))):
        raise RuntimeError("oracle days do not map to sequential phase indices")
    return first


def format_words(name: str, words: list[int]) -> str:
    lines = [f"static const std::array<uint64_t, {len(words)}> {name} = {{{{"]
    for first in range(0, len(words), 4):
        row = ", ".join(
            f"UINT64_C(0x{word:016x})" for word in words[first:first + 4]
        )
        lines.append(f"    {row},")
    lines.append("}};")
    return "\n".join(lines)


def format_prefixes(name: str, values: list[int]) -> str:
    lines = [f"static const std::array<uint16_t, {len(values)}> {name} = {{{{"]
    for first in range(0, len(values), 12):
        lines.append("    " + ", ".join(
            str(value) for value in values[first:first + 12]
        ) + ",")
    lines.append("}};")
    return "\n".join(lines)


def format_segments(name: str, values: list[LinearSegment]) -> str:
    lines = [
        f"static const std::array<CivilDayLinearSegment, {len(values)}> "
        f"{name} = {{{{"
    ]
    for value in values:
        lines.append(
            "    {"
            f"{value.first_index}u, {value.event_count}u, "
            f"INT64_C({value.base_ticks}), INT64_C({value.step_ticks})"
            "},"
        )
    lines.append("}};")
    return "\n".join(lines)


def make_header(data: dict[str, list[int]]) -> tuple[str, list[str]]:
    moon = data["moon"]
    term = data["solar_term"]
    moon_early, moon_tail_first = split_exact_prefix(moon)
    term_early, term_tail_first = split_exact_prefix(term)
    moon_tail, moon_phases, moon_residual = fit_tail(moon, moon_tail_first, 0)
    term_tail, term_phases, term_residual = fit_tail(term, term_tail_first, 24)
    if moon_phases != [0]:
        raise RuntimeError("new-moon tail unexpectedly has seasonal phases")
    moon_sparse = sparse_residuals(moon_residual)
    term_sparse = sparse_residuals(term_residual)

    profile_material = json.dumps({
        "moon": moon,
        "solar_term": term,
        "moon_tail_first": moon_tail_first,
        "solar_term_tail_first": term_tail_first,
    }, separators=(",", ":")).encode("ascii")
    profile_hash = hashlib.sha256(profile_material).hexdigest()

    phase_lines = ["static const std::array<int64_t, 24> kSolarTermPhaseTicks = {{"]
    for first in range(0, 24, 4):
        phase_lines.append("    " + ", ".join(
            f"INT64_C({value})" for value in term_phases[first:first + 4]
        ) + ",")
    phase_lines.append("}};")
    phase_table = "\n".join(phase_lines)

    header = f"""// Generated by legacy/sxwnl/calendar/generate_historical_calendar_profile.py.
+// Do not edit by hand.
+#ifndef TAIYIN_CHINESE_CALENDAR_HISTORICAL_CALENDAR_DATA_H
+#define TAIYIN_CHINESE_CALENDAR_HISTORICAL_CALENDAR_DATA_H
+
+#include <array>
+#include <cstddef>
+#include <cstdint>
+
+namespace taiyin {{
+namespace chinese_calendar {{
+namespace internal {{
+
+// These integers assign events to proleptic fixed UTC+08:00 civil days. They
+// are calendar day numbers, not astronomical event instants in UT.
+constexpr int64_t kHistoricalCivilDayScale = INT64_C({DAY_SCALE});
+constexpr int32_t kHistoricalCivilOffsetMinutes = 480;
+constexpr int64_t kHistoricalProfileEndJd = INT64_C({HISTORICAL_END_JD});
+constexpr std::size_t kHistoricalResidualRankBlockEvents = {RANK_BLOCK_EVENTS}u;
+constexpr const char* kHistoricalProfileSha256 = "{profile_hash}";
+constexpr const char* kHistoricalNewMoonOracleSha256 = "{oracle_hash(moon)}";
+constexpr const char* kHistoricalSolarTermOracleSha256 = "{oracle_hash(term)}";
+
+struct CivilDayLinearSegment {{
+    uint32_t first_event_index;
+    uint32_t event_count;
+    int64_t base_ticks;
+    int64_t step_ticks;
+}};
+
+constexpr int64_t kHistoricalNewMoonFirstPhaseIndex = INT64_C(-33655);
+constexpr std::size_t kHistoricalNewMoonEventCount = {len(moon)}u;
+constexpr int64_t kHistoricalNewMoonFirstCivilDay = INT64_C({moon[0]});
+constexpr int64_t kHistoricalNewMoonLastCivilDay = INT64_C({moon[-1]});
+{format_segments("kHistoricalNewMoonExactSegments", moon_early)}
+static const CivilDayLinearSegment kHistoricalNewMoonTail = {{
+    {moon_tail.first_index}u, {moon_tail.event_count}u,
+    INT64_C({moon_tail.base_ticks}), INT64_C({moon_tail.step_ticks})
+}};
+{format_words("kHistoricalNewMoonResidualMask", moon_sparse.nonzero_mask)}
+{format_words("kHistoricalNewMoonResidualSigns", moon_sparse.sign_bits)}
+{format_prefixes("kHistoricalNewMoonResidualRank", moon_sparse.rank_prefix)}
+
+constexpr int64_t kHistoricalSolarTermFirstPhaseIndex = INT64_C(-53265);
+constexpr std::size_t kHistoricalSolarTermEventCount = {len(term)}u;
+constexpr int64_t kHistoricalSolarTermFirstCivilDay = INT64_C({term[0]});
+constexpr int64_t kHistoricalSolarTermLastCivilDay = INT64_C({term[-1]});
+{format_segments("kHistoricalSolarTermExactSegments", term_early)}
+static const CivilDayLinearSegment kHistoricalSolarTermTail = {{
+    {term_tail.first_index}u, {term_tail.event_count}u,
+    INT64_C({term_tail.base_ticks}), INT64_C({term_tail.step_ticks})
+}};
+{phase_table}
+{format_words("kHistoricalSolarTermResidualMask", term_sparse.nonzero_mask)}
+{format_words("kHistoricalSolarTermResidualSigns", term_sparse.sign_bits)}
+{format_prefixes("kHistoricalSolarTermResidualRank", term_sparse.rank_prefix)}
+
+}}  // namespace internal
+}}  // namespace chinese_calendar
+}}  // namespace taiyin
+
+#endif  // TAIYIN_CHINESE_CALENDAR_HISTORICAL_CALENDAR_DATA_H
+""".replace("\n+", "\n")
    summary = [
        f"new moon: {len(moon)} events, {len(moon_early)} exact segments, "
        f"tail {moon_tail.event_count}, {moon_sparse.nonzero_count} residuals",
        f"solar term: {len(term)} events, {len(term_early)} exact segments, "
        f"tail {term_tail.event_count}, {term_sparse.nonzero_count} residuals",
        f"profile SHA-256: {profile_hash}",
    ]
    return header, summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sxwnl-lunar-js", required=True, type=Path)
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("src/chinese_calendar/historical_calendar_data.h"),
    )
    parser.add_argument("--node", default="node")
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    data = load_oracle(args.node, args.sxwnl_lunar_js)
    header, summary = make_header(data)
    for line in summary:
        print(line, file=sys.stderr)
    if args.check:
        if not args.output.exists() or args.output.read_text() != header:
            print(f"generated profile differs from {args.output}", file=sys.stderr)
            return 1
        return 0
    args.output.write_text(header)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
