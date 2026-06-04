#!/usr/bin/env python3

from __future__ import annotations

import argparse
import datetime as _dt
import json
import math
import re
import struct
import sys
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable


SBDB_QUERY_URL = "https://ssd-api.jpl.nasa.gov/sbdb_query.api"
RAW_ROWS_FORMAT = "TAIYIN_SBDB_QUERY_ROWS_V1"
FIELDS = [
    "spkid",
    "full_name",
    "pdes",
    "name",
    "kind",
    "orbit_id",
    "epoch",
    "e",
    "a",
    "i",
    "om",
    "w",
    "ma",
    "condition_code",
    "H",
    "diameter",
    "albedo",
    "neo",
    "pha",
]

MAGIC = b"TKC1"
VERSION = 1
TKC1_KEPLER_METHOD_ID = 3003
TKC1_SOURCE_SBDB = 42
TKC1_GENERATION = 1
CENTER_SUN = 10
FRAME_ICRF_J2000 = 1
SOLAR_MU_AU3_DAY2 = 0.0002959122082855911025
DEFAULT_VALIDITY_DAYS = 3652.5

FLAG_NUMBERED = 1 << 0
FLAG_NAMED = 1 << 1
FLAG_NEO = 1 << 2
FLAG_PHA = 1 << 3
FLAG_COMET = 1 << 4

HEADER_STRUCT = struct.Struct("<4sIIIIIIQQQQQddQII96s")
OBJECT_STRUCT = struct.Struct("<iiiiddIIIIQII")
ELEMENT_STRUCT = struct.Struct("<dddddddddd")
ALIAS_STRUCT = struct.Struct("<IIQ")

FNV1A_64_OFFSET = 14695981039346656037
FNV1A_64_PRIME = 1099511628211

TIER0_NUMBERS = [1, 2, 3, 4, 433, 1181, 2060, 5145, 7066]
TIER_CHOICES = ["tier0-core", "tier1-first1000-numbered", "tier2-pha"]


@dataclass
class SmallBody:
    spkid: int
    full_name: str
    pdes: str
    name: str
    kind: str
    orbit_id: str
    epoch: float
    eccentricity: float
    semi_major_axis_au: float
    inclination_rad: float
    longitude_ascending_node_rad: float
    argument_periapsis_rad: float
    mean_anomaly_rad: float
    condition_code: str
    h: str
    diameter: str
    albedo: str
    neo: bool
    pha: bool
    target_id: int
    small_body_number: int
    canonical_name: str
    display_name: str
    aliases: set[str] = field(default_factory=set)


class StringTableBuilder:
    def __init__(self) -> None:
        self.data = bytearray(b"\0")
        self.offsets: dict[str, int] = {"": 0}

    def add(self, value: str) -> int:
        value = value or ""
        if value in self.offsets:
            return self.offsets[value]
        offset = len(self.data)
        self.offsets[value] = offset
        self.data.extend(value.encode("utf-8"))
        self.data.append(0)
        return offset


def fnv1a_64(value: str) -> int:
    result = FNV1A_64_OFFSET
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return result


def normalize_alias(value: str) -> str:
    value = (value or "").strip().lower()
    chars: list[str] = []
    last_was_separator = False
    for ch in value:
        if ch.isalnum():
            chars.append(ch)
            last_was_separator = False
        elif ch in "_-()" or ch.isspace():
            if chars and not last_was_separator:
                chars.append("_")
                last_was_separator = True
    return "".join(chars).strip("_")


def snake_case(value: str) -> str:
    value = (value or "").strip().lower()
    value = re.sub(r"[^a-z0-9]+", "_", value)
    return re.sub(r"_+", "_", value).strip("_")


def parse_optional_float(value: object) -> float | None:
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    return float(text)


def asteroid_number_from_pdes(pdes: str) -> int:
    pdes = (pdes or "").strip()
    return int(pdes) if pdes.isdigit() else 0


def target_id_from_row(spkid: int, pdes: str) -> tuple[int, int]:
    number = asteroid_number_from_pdes(pdes)
    if number > 0:
        return 2_000_000 + number, number
    return spkid, 0


def query_sbdb(params: dict[str, object]) -> dict[str, object]:
    query = {
        "fields": ",".join(FIELDS),
        "full-prec": "true",
    }
    query.update({k: str(v) for k, v in params.items() if v is not None})
    url = SBDB_QUERY_URL + "?" + urllib.parse.urlencode(query)
    with urllib.request.urlopen(url, timeout=120) as response:
        return json.load(response)


def query_paged(
    params: dict[str, object],
    page_size: int = 1000,
    max_rows: int | None = None,
    stop_when: Callable[[list[list[object]]], bool] | None = None,
) -> list[list[object]]:
    rows: list[list[object]] = []
    offset = 0
    started = _dt.datetime.now(_dt.timezone.utc)
    while True:
        current_limit = page_size
        if max_rows is not None:
            remaining = max_rows - len(rows)
            if remaining <= 0:
                break
            current_limit = min(current_limit, remaining)
        page_params = dict(params)
        page_params["limit"] = current_limit
        page_params["limit-from"] = offset
        before = _dt.datetime.now(_dt.timezone.utc)
        print(f"fetching SBDB page offset={offset} limit={current_limit}", file=sys.stderr, flush=True)
        data = query_sbdb(page_params)
        after = _dt.datetime.now(_dt.timezone.utc)
        page_rows = data.get("data") or []
        if not isinstance(page_rows, list) or not page_rows:
            print(f"SBDB returned no rows after {(after - before).total_seconds():.1f}s", file=sys.stderr, flush=True)
            break
        rows.extend(page_rows)
        count = int(data.get("count") or len(rows))
        offset += len(page_rows)
        elapsed = (after - started).total_seconds()
        print(
            f"fetched page_rows={len(page_rows)} total_rows={len(rows)} sbdb_count={count} "
            f"page_seconds={(after - before).total_seconds():.1f} elapsed_seconds={elapsed:.1f}",
            file=sys.stderr,
            flush=True,
        )
        if stop_when and stop_when(rows):
            print("tier selection complete; stopping early", file=sys.stderr, flush=True)
            break
        if offset >= count:
            break
    return rows


def fetch_tier_rows(tier: str, page_size: int = 500) -> list[list[object]]:
    if tier == "tier0-core":
        wanted = set(TIER0_NUMBERS)

        def found_all_tier0(rows: list[list[object]]) -> bool:
            found = {body.small_body_number for row in rows if (body := row_to_body(row)) and body.small_body_number in wanted}
            missing = sorted(wanted.difference(found))
            print(f"tier0 found={sorted(found)} missing={missing}", file=sys.stderr, flush=True)
            return not missing

        return query_paged(
            {"sb-kind": "a", "sb-ns": "n", "sort": "spkid"},
            page_size=page_size,
            max_rows=max(TIER0_NUMBERS) + page_size,
            stop_when=found_all_tier0,
        )
    if tier == "tier1-first1000-numbered":
        return query_paged(
            {"sb-kind": "a", "sb-ns": "n", "sort": "spkid"},
            page_size=page_size,
            max_rows=1000,
        )
    if tier == "tier2-pha":
        return query_paged({"sb-kind": "a", "sb-group": "pha", "sort": "spkid"}, page_size=page_size)
    raise ValueError(f"unknown tier: {tier}")


def make_raw_rows_payload(tier: str, rows: list[list[object]]) -> dict[str, object]:
    return {
        "format": RAW_ROWS_FORMAT,
        "tier": tier,
        "fields": FIELDS,
        "source": "NASA/JPL SBDB Query API",
        "source_url": SBDB_QUERY_URL,
        "downloaded_at_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(),
        "row_count": len(rows),
        "rows": rows,
    }


def save_raw_rows(path: Path, tier: str, rows: list[list[object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = make_raw_rows_payload(tier, rows)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_raw_rows(path: Path, expected_tier: str | None = None) -> list[list[object]]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if isinstance(payload, list):
        return payload
    if not isinstance(payload, dict):
        raise ValueError(f"raw SBDB cache must be an object or row list: {path}")
    if payload.get("format") != RAW_ROWS_FORMAT:
        raise ValueError(f"unsupported raw SBDB cache format in {path}: {payload.get('format')!r}")
    if expected_tier and payload.get("tier") != expected_tier:
        raise ValueError(f"raw SBDB cache tier mismatch in {path}: {payload.get('tier')!r} != {expected_tier!r}")
    if payload.get("fields") != FIELDS:
        raise ValueError(f"raw SBDB cache fields do not match this generator version: {path}")
    rows = payload.get("rows")
    if not isinstance(rows, list):
        raise ValueError(f"raw SBDB cache has no rows list: {path}")
    return rows


def row_to_body(row: list[object]) -> SmallBody | None:
    values = dict(zip(FIELDS, row))
    try:
        spkid = int(values["spkid"])
        full_name = str(values.get("full_name") or "").strip()
        pdes = str(values.get("pdes") or "").strip()
        name = str(values.get("name") or "").strip()
        kind = str(values.get("kind") or "").strip()
        orbit_id = str(values.get("orbit_id") or "").strip()
        epoch = float(values["epoch"])
        eccentricity = float(values["e"])
        semi_major_axis_au = float(values["a"])
        inclination_rad = math.radians(float(values["i"]))
        node_rad = math.radians(float(values["om"]))
        peri_rad = math.radians(float(values["w"]))
        mean_rad = math.radians(float(values["ma"]))
    except (TypeError, ValueError, KeyError):
        return None

    if not (semi_major_axis_au > 0.0 and 0.0 <= eccentricity < 1.0):
        return None

    target_id, small_body_number = target_id_from_row(spkid, pdes)
    base_name = name or full_name or pdes or str(spkid)
    canonical = snake_case(base_name)
    if small_body_number == 1181:
        canonical = "asteroid_lilith_1181"
    if not canonical:
        canonical = f"small_body_{target_id}"

    display = name or full_name or canonical
    aliases = {canonical, str(target_id), f"sbdb_{spkid}"}
    if pdes:
        aliases.add(pdes)
    if name:
        aliases.add(name)
    if full_name:
        aliases.add(full_name)
    if small_body_number > 0:
        aliases.update({
            str(small_body_number),
            f"asteroid_{small_body_number}",
            f"{small_body_number}_{canonical}",
            f"({small_body_number}) {base_name}",
        })
        if name:
            aliases.add(f"{small_body_number} {name}")
    if small_body_number == 1181:
        aliases.update({"lilith_1181", "asteroid lilith 1181", "1181_lilith"})

    return SmallBody(
        spkid=spkid,
        full_name=full_name,
        pdes=pdes,
        name=name,
        kind=kind,
        orbit_id=orbit_id,
        epoch=epoch,
        eccentricity=eccentricity,
        semi_major_axis_au=semi_major_axis_au,
        inclination_rad=inclination_rad,
        longitude_ascending_node_rad=node_rad,
        argument_periapsis_rad=peri_rad,
        mean_anomaly_rad=mean_rad,
        condition_code=str(values.get("condition_code") or "").strip(),
        h=str(values.get("H") or "").strip(),
        diameter=str(values.get("diameter") or "").strip(),
        albedo=str(values.get("albedo") or "").strip(),
        neo=str(values.get("neo") or "").strip().upper() == "Y",
        pha=str(values.get("pha") or "").strip().upper() == "Y",
        target_id=target_id,
        small_body_number=small_body_number,
        canonical_name=canonical,
        display_name=display,
        aliases={normalize_alias(alias) for alias in aliases if normalize_alias(alias)},
    )


def bodies_from_tier_rows(tier: str, rows: list[list[object]]) -> list[SmallBody]:
    if tier == "tier0-core":
        wanted = set(TIER0_NUMBERS)
        bodies = [body for row in rows if (body := row_to_body(row)) and body.small_body_number in wanted]
        bodies_by_number = {body.small_body_number: body for body in bodies}
        missing = sorted(wanted.difference(bodies_by_number))
        if missing:
            raise RuntimeError(f"missing tier0 SBDB objects: {missing}")
        return [bodies_by_number[number] for number in TIER0_NUMBERS]

    if tier == "tier1-first1000-numbered":
        bodies = [body for row in rows if (body := row_to_body(row)) and 1 <= body.small_body_number <= 1000]
        bodies_by_number = {body.small_body_number: body for body in bodies}
        missing = [number for number in range(1, 1001) if number not in bodies_by_number]
        if missing:
            raise RuntimeError(f"missing first1000 SBDB objects, first missing: {missing[:10]}")
        return [bodies_by_number[number] for number in range(1, 1001)]

    if tier == "tier2-pha":
        bodies = [body for row in rows if (body := row_to_body(row))]
        bodies.sort(key=lambda body: body.target_id)
        return bodies

    raise ValueError(f"unknown tier: {tier}")


def fetch_tier(tier: str) -> list[SmallBody]:
    return bodies_from_tier_rows(tier, fetch_tier_rows(tier))


def build_tkc1(bodies: Iterable[SmallBody], validity_days: float = DEFAULT_VALIDITY_DAYS) -> bytes:
    body_list = sorted(bodies, key=lambda body: body.target_id)
    if not body_list:
        raise ValueError("cannot write empty TKC1 catalog")
    seen: set[int] = set()
    for body in body_list:
        if body.target_id in seen:
            raise ValueError(f"duplicate target_id: {body.target_id}")
        seen.add(body.target_id)

    strings = StringTableBuilder()
    object_records = []
    element_records = []
    alias_records = []
    catalog_start = math.inf
    catalog_end = -math.inf

    for object_index, body in enumerate(body_list):
        jd_start = body.epoch - validity_days
        jd_end = body.epoch + validity_days
        catalog_start = min(catalog_start, jd_start)
        catalog_end = max(catalog_end, jd_end)
        canonical_offset = strings.add(body.canonical_name)
        display_offset = strings.add(body.display_name)
        element_start_index = len(element_records)
        element_records.append(ELEMENT_STRUCT.pack(
            jd_start,
            jd_end,
            body.epoch,
            SOLAR_MU_AU3_DAY2,
            body.semi_major_axis_au,
            body.eccentricity,
            body.inclination_rad,
            body.longitude_ascending_node_rad,
            body.argument_periapsis_rad,
            body.mean_anomaly_rad,
        ))
        flags = 0
        if body.small_body_number > 0:
            flags |= FLAG_NUMBERED
        if body.name:
            flags |= FLAG_NAMED
        if body.neo:
            flags |= FLAG_NEO
        if body.pha:
            flags |= FLAG_PHA
        if body.kind.startswith("c"):
            flags |= FLAG_COMET
        object_records.append(OBJECT_STRUCT.pack(
            body.target_id,
            CENTER_SUN,
            TKC1_KEPLER_METHOD_ID,
            FRAME_ICRF_J2000,
            jd_start,
            jd_end,
            element_start_index,
            1,
            canonical_offset,
            display_offset,
            body.spkid,
            body.small_body_number,
            flags,
        ))
        for alias in body.aliases:
            alias_offset = strings.add(alias)
            alias_hash = fnv1a_64(alias)
            alias_records.append((alias_hash, alias, ALIAS_STRUCT.pack(alias_offset, object_index, alias_hash)))

    alias_records.sort(key=lambda item: (item[0], item[1]))
    alias_blob = b"".join(item[2] for item in alias_records)
    object_blob = b"".join(object_records)
    element_blob = b"".join(element_records)

    object_offset = HEADER_STRUCT.size
    element_offset = object_offset + len(object_blob)
    alias_offset = element_offset + len(element_blob)
    string_offset = alias_offset + len(alias_blob)
    header = HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        0,
        len(object_records),
        len(element_records),
        len(alias_records),
        0,
        object_offset,
        element_offset,
        alias_offset,
        string_offset,
        len(strings.data),
        catalog_start,
        catalog_end,
        TKC1_SOURCE_SBDB,
        1,
        TKC1_GENERATION,
        b"\0" * 96,
    )
    return header + object_blob + element_blob + alias_blob + bytes(strings.data)


def update_manifest(manifest_path: Path, tier: str, output: Path, bodies: list[SmallBody], validity_days: float) -> None:
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    else:
        manifest = {
            "format": "TKC1",
            "source": "NASA/JPL SBDB Query API",
            "source_url": SBDB_QUERY_URL,
            "notes": [
                "TKC1 SBDB tiers use osculating two-body Kepler elements and are approximate fallback ephemerides.",
                "Use SPK/OPM4/Horizons-derived data for precision work.",
                "Numbered asteroid Taiyin target IDs use 2000000 + asteroid_number; unnumbered objects use SBDB spkid.",
            ],
            "files": [],
        }
    files = [entry for entry in manifest.get("files", []) if entry.get("tier") != tier]
    files.append({
        "tier": tier,
        "path": str(output.relative_to(manifest_path.parent) if output.is_relative_to(manifest_path.parent) else output),
        "object_count": len(bodies),
        "validity_days_each_side": validity_days,
        "catalog_jd_tdb_start": min(body.epoch - validity_days for body in bodies),
        "catalog_jd_tdb_end": max(body.epoch + validity_days for body in bodies),
        "generated_at_utc": _dt.datetime.now(_dt.timezone.utc).isoformat(),
    })
    files.sort(key=lambda entry: entry["tier"])
    manifest["files"] = files
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_or_fetch_rows(args: argparse.Namespace) -> tuple[list[list[object]], str]:
    if args.input:
        return load_raw_rows(args.input, args.tier), f"loaded raw rows from {args.input}"
    if args.cache and args.cache.exists() and not args.refresh:
        return load_raw_rows(args.cache, args.tier), f"loaded raw rows from cache {args.cache}"

    rows = fetch_tier_rows(args.tier, args.page_size)
    if args.cache:
        save_raw_rows(args.cache, args.tier, rows)
        return rows, f"downloaded raw rows and cached to {args.cache}"
    return rows, "downloaded raw rows without cache"


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch/cache JPL SBDB osculating elements and write a TKC1 Kepler catalog.")
    parser.add_argument("--tier", required=True, choices=TIER_CHOICES)
    parser.add_argument("--output", type=Path, help="TKC1 output path. Required unless --download-only is used.")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--validity-days", type=float, default=DEFAULT_VALIDITY_DAYS)
    parser.add_argument("--input", type=Path, help="Read raw SBDB rows from this JSON file and do not use the network.")
    parser.add_argument("--cache", type=Path, help="Read raw rows from this JSON cache if present; otherwise download and write it.")
    parser.add_argument("--refresh", action="store_true", help="Ignore an existing --cache file and download fresh raw rows.")
    parser.add_argument("--download-only", action="store_true", help="Only download/cache raw rows; do not write a TKC1 file.")
    parser.add_argument("--page-size", type=int, default=500, help="SBDB Query API page size for downloads; smaller values print progress sooner.")
    args = parser.parse_args()

    if args.input and args.cache:
        parser.error("--input and --cache are mutually exclusive")
    if args.input and args.refresh:
        parser.error("--refresh only applies with --cache")
    if args.download_only and not args.cache and not args.input:
        parser.error("--download-only requires --cache or --input")
    if not args.download_only and not args.output:
        parser.error("--output is required unless --download-only is used")
    if args.page_size <= 0:
        parser.error("--page-size must be positive")

    rows, row_source = load_or_fetch_rows(args)
    bodies = bodies_from_tier_rows(args.tier, rows)
    print(f"{row_source}; selected {len(bodies)} {args.tier} objects from {len(rows)} raw rows")

    if args.download_only:
        return 0

    blob = build_tkc1(bodies, args.validity_days)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(blob)
    manifest_path = args.manifest or args.output.parent / "manifest.json"
    update_manifest(manifest_path, args.tier, args.output, bodies, args.validity_days)
    print(f"wrote {args.output} ({len(blob)} bytes, {len(bodies)} objects)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
