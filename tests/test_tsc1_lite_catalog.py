#!/usr/bin/env python3
"""Verify the checked-in magnitude-limited TSC1 distribution catalog."""

from __future__ import annotations

import math
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from compile_star_catalog import (  # noqa: E402
    ALIAS_ENTRY_STRUCT,
    FLAG_SPECIAL_DIRECTION,
    HEADER_STRUCT,
    STAR_RECORD_STRUCT,
    StringTableReader,
    fnv1a_64,
    inspect_tsc1,
)
from filter_tsc1_catalog import normalize_alias  # noqa: E402


CATALOG = ROOT / "data/stars/catalogs/lite/stars-bright-v5.tsc1"
REQUIREMENTS = ROOT / "data/stars/catalogs/lite/required_stars.json"
MAX_MAGNITUDE = 5.0
EXPECTED_STAR_COUNT = 2057
EXPECTED_ALIAS_COUNT = 12242


def fail(message: str) -> None:
    raise AssertionError(message)


def main() -> None:
    info = inspect_tsc1(CATALOG, verbose=False)
    if info["star_count"] != EXPECTED_STAR_COUNT:
        fail(f"unexpected lite star count: {info['star_count']}")
    if info["alias_count"] != EXPECTED_ALIAS_COUNT:
        fail(f"unexpected lite alias count: {info['alias_count']}")

    data = CATALOG.read_bytes()
    header = HEADER_STRUCT.unpack_from(data)
    star_count, alias_count = header[3], header[4]
    stars_offset, aliases_offset = header[5], header[6]
    strings_offset, strings_size = header[7], header[8]
    strings = StringTableReader(data[strings_offset:strings_offset + strings_size])
    requirements = json.loads(REQUIREMENTS.read_text(encoding="utf-8"))
    required_hips = {record["hip_id"] for record in requirements["required_stars"]}

    canonical_ids: set[str] = set()
    hip_to_index: dict[int, int] = {}
    special_count = 0
    for index in range(star_count):
        record = STAR_RECORD_STRUCT.unpack_from(
            data, stars_offset + index * STAR_RECORD_STRUCT.size)
        canonical_ids.add(strings.get(record[0]))
        hip_to_index[record[3]] = index
        magnitude = record[13]
        special = (record[15] & FLAG_SPECIAL_DIRECTION) != 0
        if special:
            special_count += 1
        elif (not math.isfinite(magnitude) or magnitude > MAX_MAGNITUDE) and record[3] not in required_hips:
            fail(f"non-special lite record outside magnitude limit: {strings.get(record[0])}")

    if special_count != 2:
        fail(f"expected two retained special directions, got {special_count}")
    required = {"spica", "antares", "galactic_center_j2000", "sgr_a_apparent"}
    missing = sorted(required.difference(canonical_ids))
    if missing:
        fail(f"lite catalog is missing required entries: {', '.join(missing)}")

    aliases: dict[str, int] = {}
    previous_alias_key: tuple[int, bytes] | None = None
    for index in range(alias_count):
        offset, star_index, alias_hash = ALIAS_ENTRY_STRUCT.unpack_from(
            data, aliases_offset + index * ALIAS_ENTRY_STRUCT.size)
        if star_index >= star_count:
            fail(f"alias {index} points outside the lite star table")
        alias = strings.get(offset)
        encoded = alias.encode("utf-8")
        if alias_hash != fnv1a_64(alias):
            fail(f"alias {index} has an inconsistent FNV-1a hash")
        alias_key = (alias_hash, encoded)
        if previous_alias_key is not None and alias_key < previous_alias_key:
            fail(f"alias {index} is not sorted by unsigned hash and UTF-8 bytes")
        previous_alias_key = alias_key
        aliases[alias] = star_index

    required_stars = requirements["required_stars"]
    selection = requirements.get("selection", {})
    if selection.get("chinese_line_stars") != 1385:
        fail("lite requirements must enumerate all Stellarium Chinese line stars")
    if selection.get("western_zodiac_line_stars") != 141:
        fail("lite requirements must enumerate all western zodiac line stars")
    if selection.get("unique_required_hips") != 1399 or len(required_stars) != 1399:
        fail("lite requirements have an unexpected cultural-star union size")
    for required in required_stars:
        star_index = hip_to_index.get(required["hip_id"])
        if star_index is None:
            fail(f"required HIP {required['hip_id']} is absent from lite catalog")
        for alias in required["aliases"]:
            normalized = normalize_alias(alias)
            if aliases.get(normalized) != star_index:
                fail(f"required alias {alias!r} is missing or points to the wrong star")

    for alias in ("织女一", "角宿一", "毕宿一"):
        if normalize_alias(alias) not in aliases:
            fail(f"expected traditional Chinese alias is absent: {alias}")

    print("test_tsc1_lite_catalog: ALL TESTS PASSED")


if __name__ == "__main__":
    main()
