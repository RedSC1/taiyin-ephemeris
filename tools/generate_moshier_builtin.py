#!/usr/bin/env python3

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
JS_DATA = ROOT.parent / "js-ephemeris" / "src" / "moshier" / "data"
HEADER = ROOT / "include" / "taiyin" / "internal" / "moshier_builtin.h"
SOURCE = ROOT / "src" / "moshier_builtin.cpp"


BODIES = [
    ("mer", "mercury", "mer404", "mercury404Correction", 1, "GPlan", 0, 1228000.5, 2817152.5),
    ("ven", "venus", "ven404", "venus404Correction", 2, "GPlan", 0, 1228000.5, 2817152.5),
    ("ear", "emb", "ear404", "earth404Correction", 3, "G3Plan", 3, 1221000.5, 2817152.5),
    ("mar", "mars", "mar404", "mars404Correction", 4, "GPlan", 0, 625000.5, 2817152.5),
    ("jup", "jupiter", "jup404", "jupiter404Correction", 5, "GPlan", 0, 625000.5, 2817152.5),
    ("sat", "saturn", "sat404", "saturn404Correction", 6, "GPlan", 0, 625000.5, 2817152.5),
    ("ura", "uranus", "ura404", "uranus404Correction", 7, "GPlan", 0, 625000.5, 2817152.5),
    ("nep", "neptune", "nep404", "neptune404Correction", 8, "GPlan", 0, 625000.5, 2817152.5),
    ("plu", "pluto", "plu404", "pluto404Correction", 9, "GPlan", 0, 625000.5, 2817152.5),
]

MOON_COVERAGE = (1221000.5, 2817152.5)
MOON_TARGET_ID = 301
EARTH_TARGET_ID = 399
EARTH_CENTER_ID = 10
MOON_CENTER_ID = 399


def parse_array(text: str, marker: str) -> list[str]:
    marker_pos = text.find(marker)
    if marker_pos < 0:
        raise RuntimeError(f"missing marker {marker!r}")
    start = text.find("[", marker_pos)
    end = text.find("]", start)
    if start < 0 or end < 0:
        raise RuntimeError(f"missing array for {marker!r}")
    body = text[start + 1 : end]
    return re.findall(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?", body, flags=re.IGNORECASE)


def parse_scalar(text: str, marker: str) -> str:
    marker_pos = text.find(marker)
    if marker_pos < 0:
        raise RuntimeError(f"missing marker {marker!r}")
    tail = text[marker_pos + len(marker) :]
    match = re.search(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?", tail, flags=re.IGNORECASE)
    if not match:
        raise RuntimeError(f"missing scalar after {marker!r}")
    return match.group(0)


def parse_planet_file(name: str) -> dict[str, list[str] | str]:
    text = (JS_DATA / f"{name}.ts").read_text()
    return {
        "arg": parse_array(text, "const arg_tbl"),
        "lon": parse_array(text, "const lon_tbl"),
        "lat": parse_array(text, "const lat_tbl"),
        "rad": parse_array(text, "const rad_tbl"),
        "max_harmonic": parse_array(text, "max_harmonic:"),
        "maxargs": parse_scalar(text, "maxargs:"),
        "max_power_of_t": parse_scalar(text, "max_power_of_t:"),
        "distance": parse_scalar(text, "distance:"),
        "timescale": parse_scalar(text, "timescale:"),
        "trunclvl": parse_scalar(text, "trunclvl:"),
    }


def parse_corrections() -> dict[str, list[dict[str, str | list[str]]]]:
    text = (JS_DATA / "corrections.ts").read_text()
    out: dict[str, list[dict[str, str | list[str]]]] = {}
    for _, _, _, correction_name, *_ in BODIES:
        start = text.find(f"export const {correction_name}")
        if start < 0:
            raise RuntimeError(f"missing correction {correction_name}")
        array_start = text.find("[", start)
        array_end = text.find("];", array_start)
        if array_start < 0 or array_end < 0:
            raise RuntimeError(f"bad correction array {correction_name}")
        body = text[array_start + 1 : array_end]
        segments = []
        for match in re.finditer(
            r"\{\s*start:\s*([^,]+),\s*end:\s*([^,]+),\s*center:\s*([^,]+),\s*half:\s*([^,]+),\s*lon:\s*\[([^\]]+)\],\s*lat:\s*\[([^\]]+)\]\s*\}",
            body,
        ):
            segments.append(
                {
                    "start": match.group(1).strip(),
                    "end": match.group(2).strip(),
                    "center": match.group(3).strip(),
                    "half": match.group(4).strip(),
                    "lon": re.findall(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?", match.group(5), flags=re.IGNORECASE),
                    "lat": re.findall(r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:e[-+]?\d+)?", match.group(6), flags=re.IGNORECASE),
                }
            )
        out[correction_name] = segments
    return out


def fmt_array(values: list[str], indent: str = "    ", per_line: int = 10) -> str:
    lines = []
    for i in range(0, len(values), per_line):
        lines.append(indent + ", ".join(values[i : i + per_line]) + ",")
    return "\n".join(lines)


def prefixed_numbers(values: list[str], cast: str | None = None) -> list[str]:
    if not cast:
        return values
    return [f"static_cast<{cast}>({value})" for value in values]


def generate_header() -> str:
    return """#ifndef TAIYIN_INTERNAL_MOSHIER_BUILTIN_H
#define TAIYIN_INTERNAL_MOSHIER_BUILTIN_H

#include "moshier.h"

namespace taiyin {
namespace internal {

bool get_builtin_moshier_planet_table(int target_id, MoshierPlanetTable* out) noexcept;

bool get_builtin_moshier_planet_corrections(
    int target_id,
    const MoshierCorrectionSegment** out_segments,
    size_t* out_count
) noexcept;

bool get_builtin_moshier_planet_coverage(
    int target_id,
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool get_builtin_moshier_moon_tables(
    MoshierMoonLRTable* out_lr,
    MoshierMoonLatTable* out_lat
) noexcept;

bool get_builtin_moshier_moon_coverage(
    double* out_jd_tdb_start,
    double* out_jd_tdb_end
) noexcept;

bool compile_builtin_moshier_planet_ephemeris_block(
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    bool apply_de441_correction,
    CompiledEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_MOSHIER_BUILTIN_H
"""


def generate_source() -> str:
    corrections = parse_corrections()
    chunks = [
        '#include "taiyin/internal/moshier_builtin.h"\n',
        '#include "taiyin/internal/ephemeris_block.h"\n',
        "#include <algorithm>\n",
        "\nnamespace taiyin {\nnamespace internal {\nnamespace {\n\n",
        "const int MOSHIER_SUN_CENTER_ID = 10;\n\n",
    ]

    for file_prefix, body_name, table_name, correction_name, target_id, evaluator, objnum, jd_start, jd_end in BODIES:
        table = parse_planet_file(file_prefix + "404")
        chunks.append(f"const int8_t {body_name}_args[] = {{\n")
        chunks.append(fmt_array(prefixed_numbers(table["arg"], "int8_t")))
        chunks.append("\n};\n\n")
        for key in ("lon", "lat", "rad"):
            chunks.append(f"const double {body_name}_{key}[] = {{\n")
            chunks.append(fmt_array(table[key]))
            chunks.append("\n};\n\n")
        chunks.append(f"const MoshierCorrectionSegment {body_name}_corrections[] = {{\n")
        for segment in corrections[correction_name]:
            lon = ", ".join(segment["lon"])
            lat = ", ".join(segment["lat"])
            chunks.append(
                f"    {{ {segment['start']}, {segment['end']}, {segment['center']}, {segment['half']}, "
                f"{{ {lon} }}, {{ {lat} }} }},\n"
            )
        chunks.append("};\n\n")

        max_harmonic = list(table["max_harmonic"])
        while len(max_harmonic) < 18:
            max_harmonic.append("0")
        chunks.append(f"MoshierPlanetTable make_{body_name}_table() noexcept {{\n")
        chunks.append("    MoshierPlanetTable table;\n")
        chunks.append(f"    table.maxargs = {table['maxargs']};\n")
        chunks.append(f"    const int harmonics[MOSHIER_NARGS] = {{ {', '.join(max_harmonic[:18])} }};\n")
        chunks.append("    std::copy(harmonics, harmonics + MOSHIER_NARGS, table.max_harmonic);\n")
        chunks.append(f"    table.max_power_of_t = {table['max_power_of_t']};\n")
        chunks.append(f"    table.arg_tbl = {body_name}_args;\n")
        chunks.append(f"    table.arg_count = sizeof({body_name}_args) / sizeof({body_name}_args[0]);\n")
        chunks.append(f"    table.lon_tbl = {body_name}_lon;\n")
        chunks.append(f"    table.lon_count = sizeof({body_name}_lon) / sizeof({body_name}_lon[0]);\n")
        chunks.append(f"    table.lat_tbl = {body_name}_lat;\n")
        chunks.append(f"    table.lat_count = sizeof({body_name}_lat) / sizeof({body_name}_lat[0]);\n")
        chunks.append(f"    table.rad_tbl = {body_name}_rad;\n")
        chunks.append(f"    table.rad_count = sizeof({body_name}_rad) / sizeof({body_name}_rad[0]);\n")
        chunks.append(f"    table.distance_au = {table['distance']};\n")
        chunks.append(f"    table.timescale_days = {table['timescale']};\n")
        chunks.append(f"    table.trunclvl = {table['trunclvl']};\n")
        chunks.append("    return table;\n")
        chunks.append("}\n\n")

    moon_lr_text = (JS_DATA / "mlr404.ts").read_text()
    moon_lr = {
        "arg": parse_array(moon_lr_text, "const arg_tbl"),
        "lon": parse_array(moon_lr_text, "const lon_tbl"),
        "rad": parse_array(moon_lr_text, "const rad_tbl"),
        "max_harmonic": parse_array(moon_lr_text, "max_harmonic:"),
        "maxargs": parse_scalar(moon_lr_text, "maxargs:"),
        "max_power_of_t": parse_scalar(moon_lr_text, "max_power_of_t:"),
        "distance": parse_scalar(moon_lr_text, "distance:"),
        "timescale": parse_scalar(moon_lr_text, "timescale:"),
        "trunclvl": parse_scalar(moon_lr_text, "trunclvl:"),
    }
    moon_lat_text = (JS_DATA / "mlat404.ts").read_text()
    moon_lat = {
        "arg": parse_array(moon_lat_text, "const arg_tbl"),
        "lon": parse_array(moon_lat_text, "const lon_tbl"),
        "max_harmonic": parse_array(moon_lat_text, "max_harmonic:"),
        "maxargs": parse_scalar(moon_lat_text, "maxargs:"),
        "max_power_of_t": parse_scalar(moon_lat_text, "max_power_of_t:"),
        "distance": parse_scalar(moon_lat_text, "distance:"),
        "timescale": parse_scalar(moon_lat_text, "timescale:"),
        "trunclvl": parse_scalar(moon_lat_text, "trunclvl:"),
    }

    chunks.append("const int8_t moon_lr_args[] = {\n")
    chunks.append(fmt_array(prefixed_numbers(moon_lr["arg"], "int8_t")))
    chunks.append("\n};\n\n")
    for key in ("lon", "rad"):
        chunks.append(f"const double moon_lr_{key}[] = {{\n")
        chunks.append(fmt_array(moon_lr[key]))
        chunks.append("\n};\n\n")
    chunks.append("const int8_t moon_lat_args[] = {\n")
    chunks.append(fmt_array(prefixed_numbers(moon_lat["arg"], "int8_t")))
    chunks.append("\n};\n\n")
    chunks.append("const double moon_lat_lon[] = {\n")
    chunks.append(fmt_array(moon_lat["lon"]))
    chunks.append("\n};\n\n")

    moon_lr_harmonic = list(moon_lr["max_harmonic"])
    while len(moon_lr_harmonic) < 18:
        moon_lr_harmonic.append("0")
    moon_lat_harmonic = list(moon_lat["max_harmonic"])
    while len(moon_lat_harmonic) < 18:
        moon_lat_harmonic.append("0")

    chunks.append("MoshierMoonLRTable make_moon_lr_table() noexcept {\n")
    chunks.append("    MoshierMoonLRTable table;\n")
    chunks.append(f"    table.maxargs = {moon_lr['maxargs']};\n")
    chunks.append(f"    const int harmonics[MOSHIER_NARGS] = {{ {', '.join(moon_lr_harmonic[:18])} }};\n")
    chunks.append("    std::copy(harmonics, harmonics + MOSHIER_NARGS, table.max_harmonic);\n")
    chunks.append(f"    table.max_power_of_t = {moon_lr['max_power_of_t']};\n")
    chunks.append("    table.arg_tbl = moon_lr_args;\n")
    chunks.append("    table.arg_count = sizeof(moon_lr_args) / sizeof(moon_lr_args[0]);\n")
    chunks.append("    table.lon_tbl = moon_lr_lon;\n")
    chunks.append("    table.lon_count = sizeof(moon_lr_lon) / sizeof(moon_lr_lon[0]);\n")
    chunks.append("    table.rad_tbl = moon_lr_rad;\n")
    chunks.append("    table.rad_count = sizeof(moon_lr_rad) / sizeof(moon_lr_rad[0]);\n")
    chunks.append(f"    table.distance_au = {moon_lr['distance']};\n")
    chunks.append(f"    table.timescale_days = {moon_lr['timescale']};\n")
    chunks.append(f"    table.trunclvl = {moon_lr['trunclvl']};\n")
    chunks.append("    return table;\n")
    chunks.append("}\n\n")

    chunks.append("MoshierMoonLatTable make_moon_lat_table() noexcept {\n")
    chunks.append("    MoshierMoonLatTable table;\n")
    chunks.append(f"    table.maxargs = {moon_lat['maxargs']};\n")
    chunks.append(f"    const int harmonics[MOSHIER_NARGS] = {{ {', '.join(moon_lat_harmonic[:18])} }};\n")
    chunks.append("    std::copy(harmonics, harmonics + MOSHIER_NARGS, table.max_harmonic);\n")
    chunks.append(f"    table.max_power_of_t = {moon_lat['max_power_of_t']};\n")
    chunks.append("    table.arg_tbl = moon_lat_args;\n")
    chunks.append("    table.arg_count = sizeof(moon_lat_args) / sizeof(moon_lat_args[0]);\n")
    chunks.append("    table.lon_tbl = moon_lat_lon;\n")
    chunks.append("    table.lon_count = sizeof(moon_lat_lon) / sizeof(moon_lat_lon[0]);\n")
    chunks.append(f"    table.distance_au = {moon_lat['distance']};\n")
    chunks.append(f"    table.timescale_days = {moon_lat['timescale']};\n")
    chunks.append(f"    table.trunclvl = {moon_lat['trunclvl']};\n")
    chunks.append("    return table;\n")
    chunks.append("}\n\n")

    chunks.append("}  // namespace\n\n")

    chunks.append("bool get_builtin_moshier_planet_table(int target_id, MoshierPlanetTable* out) noexcept {\n")
    chunks.append("    if (!out) {\n        return false;\n    }\n")
    chunks.append("    switch (target_id) {\n")
    for _, body_name, _, _, target_id, *_ in BODIES:
        chunks.append(f"        case {target_id}: *out = make_{body_name}_table(); return true;\n")
    chunks.append("        default: return false;\n    }\n")
    chunks.append("}\n\n")

    chunks.append("bool get_builtin_moshier_planet_corrections(int target_id, const MoshierCorrectionSegment** out_segments, size_t* out_count) noexcept {\n")
    chunks.append("    if (!out_segments || !out_count) {\n        return false;\n    }\n")
    chunks.append("    *out_segments = 0;\n    *out_count = 0;\n")
    chunks.append("    switch (target_id) {\n")
    for _, body_name, _, _, target_id, *_ in BODIES:
        chunks.append(
            f"        case {target_id}: *out_segments = {body_name}_corrections; "
            f"*out_count = sizeof({body_name}_corrections) / sizeof({body_name}_corrections[0]); return true;\n"
        )
    chunks.append("        default: return false;\n    }\n")
    chunks.append("}\n\n")

    chunks.append("bool get_builtin_moshier_planet_coverage(int target_id, double* out_jd_tdb_start, double* out_jd_tdb_end) noexcept {\n")
    chunks.append("    if (!out_jd_tdb_start || !out_jd_tdb_end) {\n        return false;\n    }\n")
    chunks.append("    switch (target_id) {\n")
    for _, _, _, _, target_id, _, _, jd_start, jd_end in BODIES:
        chunks.append(f"        case {target_id}: *out_jd_tdb_start = {jd_start}; *out_jd_tdb_end = {jd_end}; return true;\n")
    chunks.append("        default: return false;\n    }\n")
    chunks.append("}\n\n")

    chunks.append("bool get_builtin_moshier_moon_tables(MoshierMoonLRTable* out_lr, MoshierMoonLatTable* out_lat) noexcept {\n")
    chunks.append("    if (!out_lr || !out_lat) {\n        return false;\n    }\n")
    chunks.append("    *out_lr = make_moon_lr_table();\n")
    chunks.append("    *out_lat = make_moon_lat_table();\n")
    chunks.append("    return true;\n")
    chunks.append("}\n\n")

    chunks.append("bool get_builtin_moshier_moon_coverage(double* out_jd_tdb_start, double* out_jd_tdb_end) noexcept {\n")
    chunks.append("    if (!out_jd_tdb_start || !out_jd_tdb_end) {\n        return false;\n    }\n")
    chunks.append(f"    *out_jd_tdb_start = {MOON_COVERAGE[0]};\n")
    chunks.append(f"    *out_jd_tdb_end = {MOON_COVERAGE[1]};\n")
    chunks.append("    return true;\n")
    chunks.append("}\n\n")

    chunks.append("bool compile_builtin_moshier_planet_ephemeris_block(int target_id, int center_id, double jd_tdb_start, double jd_tdb_end, bool apply_de441_correction, CompiledEphemerisBlock* out) noexcept {\n")
    chunks.append("    if (!out) {\n        return false;\n    }\n")
    chunks.append(f"    if (target_id == {MOON_TARGET_ID}) {{\n")
    chunks.append(f"        if (center_id != {MOON_CENTER_ID}) {{\n            return false;\n        }}\n")
    chunks.append("        double moon_start = 0.0;\n        double moon_end = 0.0;\n")
    chunks.append("        MoshierMoonLRTable moon_lr;\n        MoshierMoonLatTable moon_lat;\n")
    chunks.append("        if (!get_builtin_moshier_moon_coverage(&moon_start, &moon_end)\n")
    chunks.append("            || jd_tdb_start < moon_start\n")
    chunks.append("            || jd_tdb_end > moon_end\n")
    chunks.append("            || !get_builtin_moshier_moon_tables(&moon_lr, &moon_lat)) {\n            return false;\n        }\n")
    chunks.append("        return compile_moshier_moon_ephemeris_block(target_id, center_id, jd_tdb_start, jd_tdb_end, moon_lr, moon_lat, out);\n")
    chunks.append("    }\n")
    chunks.append(f"    if (target_id == {EARTH_TARGET_ID}) {{\n")
    chunks.append(f"        if (center_id != {EARTH_CENTER_ID}) {{\n            return false;\n        }}\n")
    chunks.append("        MoshierPlanetTable emb_table;\n")
    chunks.append("        MoshierMoonLRTable moon_lr;\n        MoshierMoonLatTable moon_lat;\n")
    chunks.append("        double emb_start = 0.0;\n        double emb_end = 0.0;\n        double moon_start = 0.0;\n        double moon_end = 0.0;\n")
    chunks.append("        if (!get_builtin_moshier_planet_table(3, &emb_table)\n")
    chunks.append("            || !get_builtin_moshier_planet_coverage(3, &emb_start, &emb_end)\n")
    chunks.append("            || !get_builtin_moshier_moon_coverage(&moon_start, &moon_end)\n")
    chunks.append("            || jd_tdb_start < emb_start\n")
    chunks.append("            || jd_tdb_end > emb_end\n")
    chunks.append("            || jd_tdb_start < moon_start\n")
    chunks.append("            || jd_tdb_end > moon_end\n")
    chunks.append("            || !get_builtin_moshier_moon_tables(&moon_lr, &moon_lat)) {\n            return false;\n        }\n")
    chunks.append("        const MoshierCorrectionSegment* corrections = 0;\n        size_t correction_count = 0;\n")
    chunks.append("        if (apply_de441_correction && !get_builtin_moshier_planet_corrections(3, &corrections, &correction_count)) {\n            return false;\n        }\n")
    chunks.append("        return compile_moshier_earth_body_ephemeris_block(target_id, center_id, jd_tdb_start, jd_tdb_end, emb_table, corrections, correction_count, moon_lr, moon_lat, out);\n")
    chunks.append("    }\n")
    chunks.append("    if (center_id != MOSHIER_SUN_CENTER_ID) {\n        return false;\n    }\n")
    chunks.append("    MoshierPlanetTable table;\n")
    chunks.append("    if (!get_builtin_moshier_planet_table(target_id, &table)) {\n        return false;\n    }\n")
    chunks.append("    double coverage_start = 0.0;\n    double coverage_end = 0.0;\n")
    chunks.append("    if (!get_builtin_moshier_planet_coverage(target_id, &coverage_start, &coverage_end)\n")
    chunks.append("        || jd_tdb_start < coverage_start\n")
    chunks.append("        || jd_tdb_end > coverage_end) {\n        return false;\n    }\n")
    chunks.append("    const MoshierCorrectionSegment* corrections = 0;\n    size_t correction_count = 0;\n")
    chunks.append("    if (apply_de441_correction && !get_builtin_moshier_planet_corrections(target_id, &corrections, &correction_count)) {\n        return false;\n    }\n")
    chunks.append("    switch (target_id) {\n")
    for _, _, _, _, target_id, evaluator, objnum, *_ in BODIES:
        chunks.append(
            f"        case {target_id}: return compile_moshier_planet_ephemeris_block("
            f"target_id, center_id, jd_tdb_start, jd_tdb_end, MoshierPlanetEvaluator::{evaluator}, {objnum}, table, corrections, correction_count, out);\n"
        )
    chunks.append("        default: return false;\n    }\n")
    chunks.append("}\n\n")

    chunks.append("}  // namespace internal\n}  // namespace taiyin\n")
    return "".join(chunks)


def main() -> None:
    HEADER.write_text(generate_header())
    SOURCE.write_text(generate_source())


if __name__ == "__main__":
    main()
