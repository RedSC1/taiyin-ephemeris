#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import math
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


MAGIC = b"TSC1"
VERSION = 1
HEADER_STRUCT = struct.Struct("<4sIIIIQQQQdd64s")
STAR_RECORD_STRUCT = struct.Struct("<IIQIIIdddddddfHH")
ALIAS_ENTRY_STRUCT = struct.Struct("<IIQ")

SOURCE_UNKNOWN = 0
SOURCE_GAIA_DR3 = 1
SOURCE_HIPPARCOS = 2
SOURCE_BSC5 = 3
SOURCE_MANUAL = 4

SOURCE_NAME_TO_CODE = {
    "Gaia DR3": SOURCE_GAIA_DR3,
    "Hipparcos": SOURCE_HIPPARCOS,
    "BSC5": SOURCE_BSC5,
    "Manual": SOURCE_MANUAL,
}
SOURCE_CODE_TO_NAME = {
    SOURCE_UNKNOWN: "Unknown",
    SOURCE_GAIA_DR3: "Gaia DR3",
    SOURCE_HIPPARCOS: "Hipparcos",
    SOURCE_BSC5: "BSC5",
    SOURCE_MANUAL: "Manual",
}

FLAG_HAS_GAIA_ID = 1 << 0
FLAG_HAS_HIP_ID = 1 << 1
FLAG_HAS_HR_ID = 1 << 2
FLAG_HAS_HD_ID = 1 << 3
FLAG_HAS_RADIAL_VELOCITY = 1 << 4
FLAG_HAS_PARALLAX = 1 << 5
FLAG_SPECIAL_DIRECTION = 1 << 6

FNV1A_64_OFFSET = 14695981039346656037
FNV1A_64_PRIME = 1099511628211

TIER_FIXED = "fixed_traditional"
TIER_BSC = "bright_bsc"
TIER_HIP = "hipparcos"
TIER_INCLUDES = {
    TIER_FIXED: {TIER_FIXED},
    TIER_BSC: {TIER_FIXED, TIER_BSC},
    TIER_HIP: {TIER_FIXED, TIER_BSC, TIER_HIP},
}

MANUAL_RECORDS = {
    "galactic_center_j2000": {
        "ra_deg": 266.4168371,
        "dec_deg": -29.00781056,
        "pm_ra_mas_yr": 0.0,
        "pm_dec_mas_yr": 0.0,
        "parallax_mas": 0.0,
        "radial_velocity_km_s": 0.0,
        "reference_epoch": 2000.0,
        "magnitude": math.nan,
        "flags": FLAG_SPECIAL_DIRECTION,
    },
    "sgr_a_apparent": {
        "ra_deg": 266.4168371,
        "dec_deg": -29.00781056,
        "pm_ra_mas_yr": 0.0,
        "pm_dec_mas_yr": 0.0,
        "parallax_mas": 0.0,
        "radial_velocity_km_s": 0.0,
        "reference_epoch": 2000.0,
        "magnitude": math.nan,
        "flags": FLAG_SPECIAL_DIRECTION,
    },
}


@dataclass
class IdentityRow:
    tier_flags: set[str]
    canonical_id: str
    display_name: str
    aliases: set[str] = field(default_factory=set)
    hip_id: int | None = None
    hr_id: int | None = None
    hd_id: int | None = None
    gaia_dr3_source_id: int | None = None
    vmag: float | None = None
    priority: int = 0


@dataclass
class AstrometryRow:
    canonical_id: str
    gaia_dr3_source_id: int | None
    hip_id: int | None
    hr_id: int | None
    hd_id: int | None
    ra_deg: float | None
    dec_deg: float | None
    pm_ra_mas_yr: float | None
    pm_dec_mas_yr: float | None
    parallax_mas: float | None
    radial_velocity_km_s: float | None
    magnitude: float | None
    reference_epoch: float | None
    source_name: str


@dataclass
class StarForCatalog:
    identity: IdentityRow
    astrometry: AstrometryRow
    source_code: int
    flags: int
    aliases: set[str]


@dataclass
class CompiledCatalog:
    stars: list[StarForCatalog]
    alias_count: int
    string_table_size: int
    min_epoch: float
    max_epoch: float


class StringTableBuilder:
    def __init__(self) -> None:
        self.data = bytearray(b"\0")
        self.offsets: dict[str, int] = {"": 0}

    def add(self, value: str) -> int:
        if value in self.offsets:
            return self.offsets[value]
        offset = len(self.data)
        self.offsets[value] = offset
        self.data.extend(value.encode("utf-8"))
        self.data.append(0)
        return offset


class StringTableReader:
    def __init__(self, data: bytes) -> None:
        self.data = data

    def get(self, offset: int) -> str:
        if offset >= len(self.data):
            raise ValueError(f"string offset out of range: {offset}")
        end = self.data.find(b"\0", offset)
        if end < 0:
            raise ValueError(f"unterminated string at offset: {offset}")
        return self.data[offset:end].decode("utf-8")


def snake_case(value: str) -> str:
    value = value.strip().lower()
    value = value.replace("*", "")
    value = value.replace("+", " plus ")
    value = value.replace("/", " ")
    value = value.replace("-", " ")
    value = re.sub(r"[^a-z0-9]+", "_", value)
    value = re.sub(r"_+", "_", value).strip("_")
    return value


def fnv1a_64(value: str) -> int:
    result = FNV1A_64_OFFSET
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * FNV1A_64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return result


def parse_optional_int(value: str | None) -> int | None:
    value = (value or "").strip()
    return int(value) if value else None


def parse_optional_float(value: str | None) -> float | None:
    value = (value or "").strip()
    return float(value) if value else None


def zero_if_none(value: int | None) -> int:
    return 0 if value is None else value


def nan_if_none(value: float | None) -> float:
    return math.nan if value is None else value


def read_identity_manifest(path: Path) -> dict[str, IdentityRow]:
    rows: dict[str, IdentityRow] = {}
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        required = {"tier_flags", "canonical_id", "display_name", "aliases", "hip_id", "hr_id", "hd_id", "gaia_dr3_source_id", "vmag", "priority"}
        missing = sorted(required.difference(reader.fieldnames or []))
        if missing:
            raise ValueError(f"identity manifest missing columns: {', '.join(missing)}")
        for row in reader:
            canonical_id = row["canonical_id"].strip()
            if not canonical_id:
                continue
            aliases = {snake_case(alias) for alias in row["aliases"].split(",") if snake_case(alias)}
            rows[canonical_id] = IdentityRow(
                tier_flags=set(filter(None, row["tier_flags"].split("|"))),
                canonical_id=canonical_id,
                display_name=row["display_name"].strip() or canonical_id,
                aliases=aliases,
                hip_id=parse_optional_int(row.get("hip_id")),
                hr_id=parse_optional_int(row.get("hr_id")),
                hd_id=parse_optional_int(row.get("hd_id")),
                gaia_dr3_source_id=parse_optional_int(row.get("gaia_dr3_source_id")),
                vmag=parse_optional_float(row.get("vmag")),
                priority=int(row.get("priority") or 0),
            )
    return rows


def read_astrometry(path: Path) -> dict[str, AstrometryRow]:
    rows: dict[str, AstrometryRow] = {}
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        required = {
            "canonical_id",
            "gaia_dr3_source_id",
            "hip_id",
            "hr_id",
            "hd_id",
            "ra_deg",
            "dec_deg",
            "pm_ra_mas_yr",
            "pm_dec_mas_yr",
            "parallax_mas",
            "radial_velocity_km_s",
            "phot_g_mean_mag",
            "vmag",
            "reference_epoch",
            "astrometry_source",
        }
        missing = sorted(required.difference(reader.fieldnames or []))
        if missing:
            raise ValueError(f"astrometry CSV missing columns: {', '.join(missing)}")
        for row in reader:
            canonical_id = row["canonical_id"].strip()
            if not canonical_id:
                continue
            magnitude = parse_optional_float(row.get("phot_g_mean_mag"))
            if magnitude is None:
                magnitude = parse_optional_float(row.get("vmag"))
            rows[canonical_id] = AstrometryRow(
                canonical_id=canonical_id,
                gaia_dr3_source_id=parse_optional_int(row.get("gaia_dr3_source_id")),
                hip_id=parse_optional_int(row.get("hip_id")),
                hr_id=parse_optional_int(row.get("hr_id")),
                hd_id=parse_optional_int(row.get("hd_id")),
                ra_deg=parse_optional_float(row.get("ra_deg")),
                dec_deg=parse_optional_float(row.get("dec_deg")),
                pm_ra_mas_yr=parse_optional_float(row.get("pm_ra_mas_yr")),
                pm_dec_mas_yr=parse_optional_float(row.get("pm_dec_mas_yr")),
                parallax_mas=parse_optional_float(row.get("parallax_mas")),
                radial_velocity_km_s=parse_optional_float(row.get("radial_velocity_km_s")),
                magnitude=magnitude,
                reference_epoch=parse_optional_float(row.get("reference_epoch")),
                source_name=(row.get("astrometry_source") or "").strip(),
            )
    return rows


def tier_filter_flags(tier: str) -> set[str]:
    if tier not in TIER_INCLUDES:
        raise ValueError(f"unknown tier: {tier}")
    return TIER_INCLUDES[tier]


def include_identity(identity: IdentityRow, tier: str) -> bool:
    return bool(identity.tier_flags.intersection(tier_filter_flags(tier)))


def build_manual_astrometry(identity: IdentityRow) -> AstrometryRow | None:
    manual = MANUAL_RECORDS.get(identity.canonical_id)
    if manual is None:
        return None
    return AstrometryRow(
        canonical_id=identity.canonical_id,
        gaia_dr3_source_id=identity.gaia_dr3_source_id,
        hip_id=identity.hip_id,
        hr_id=identity.hr_id,
        hd_id=identity.hd_id,
        ra_deg=float(manual["ra_deg"]),
        dec_deg=float(manual["dec_deg"]),
        pm_ra_mas_yr=float(manual["pm_ra_mas_yr"]),
        pm_dec_mas_yr=float(manual["pm_dec_mas_yr"]),
        parallax_mas=float(manual["parallax_mas"]),
        radial_velocity_km_s=float(manual["radial_velocity_km_s"]),
        magnitude=float(manual["magnitude"]),
        reference_epoch=float(manual["reference_epoch"]),
        source_name="Manual",
    )


def record_flags(identity: IdentityRow, astrometry: AstrometryRow) -> int:
    flags = 0
    if astrometry.gaia_dr3_source_id or identity.gaia_dr3_source_id:
        flags |= FLAG_HAS_GAIA_ID
    if astrometry.hip_id or identity.hip_id:
        flags |= FLAG_HAS_HIP_ID
    if astrometry.hr_id or identity.hr_id:
        flags |= FLAG_HAS_HR_ID
    if astrometry.hd_id or identity.hd_id:
        flags |= FLAG_HAS_HD_ID
    if astrometry.radial_velocity_km_s is not None and not math.isnan(astrometry.radial_velocity_km_s):
        flags |= FLAG_HAS_RADIAL_VELOCITY
    if astrometry.parallax_mas is not None and not math.isnan(astrometry.parallax_mas):
        flags |= FLAG_HAS_PARALLAX
    if identity.canonical_id in MANUAL_RECORDS:
        flags |= FLAG_SPECIAL_DIRECTION
    return flags


def aliases_for(identity: IdentityRow, astrometry: AstrometryRow) -> set[str]:
    aliases = set(identity.aliases)
    aliases.add(identity.canonical_id)
    display_alias = snake_case(identity.display_name)
    if display_alias:
        aliases.add(display_alias)
    gaia_id = astrometry.gaia_dr3_source_id or identity.gaia_dr3_source_id
    hip_id = astrometry.hip_id or identity.hip_id
    hr_id = astrometry.hr_id or identity.hr_id
    hd_id = astrometry.hd_id or identity.hd_id
    if gaia_id:
        aliases.add(f"gaia_dr3_{gaia_id}")
    if hip_id:
        aliases.add(f"hip_{hip_id}")
    if hr_id:
        aliases.add(f"hr_{hr_id}")
    if hd_id:
        aliases.add(f"hd_{hd_id}")
    return {alias for alias in aliases if alias}


def build_catalog(identity_rows: dict[str, IdentityRow], astrometry_rows: dict[str, AstrometryRow], tier: str) -> list[StarForCatalog]:
    stars: list[StarForCatalog] = []
    for identity in identity_rows.values():
        if not include_identity(identity, tier):
            continue
        astrometry = astrometry_rows.get(identity.canonical_id) or build_manual_astrometry(identity)
        if astrometry is None or not astrometry.source_name:
            astrometry = build_manual_astrometry(identity)
        if astrometry is None or not astrometry.source_name:
            print(f"warning: skipping {identity.canonical_id}: missing astrometry", file=sys.stderr)
            continue
        source_code = SOURCE_NAME_TO_CODE.get(astrometry.source_name, SOURCE_UNKNOWN)
        if source_code == SOURCE_UNKNOWN:
            raise ValueError(f"unknown astrometry source for {identity.canonical_id}: {astrometry.source_name!r}")
        skipped = False
        for field_name in ("ra_deg", "dec_deg", "reference_epoch"):
            if getattr(astrometry, field_name) is None and identity.canonical_id not in MANUAL_RECORDS:
                print(f"warning: skipping {identity.canonical_id}: missing {field_name}", file=sys.stderr)
                skipped = True
                break
        if skipped:
            continue
        stars.append(
            StarForCatalog(
                identity=identity,
                astrometry=astrometry,
                source_code=source_code,
                flags=record_flags(identity, astrometry),
                aliases=aliases_for(identity, astrometry),
            )
        )
    return sorted(
        stars,
        key=lambda star: (
            -star.identity.priority,
            star.identity.hr_id if star.identity.hr_id is not None else 10**9,
            star.identity.hip_id if star.identity.hip_id is not None else 10**9,
            star.identity.canonical_id,
        ),
    )


def validate_aliases(stars: list[StarForCatalog], strict: bool) -> dict[str, int]:
    alias_to_index: dict[str, int] = {}
    alias_priorities: dict[str, tuple[int, str]] = {}
    for index, star in enumerate(stars):
        for alias in star.aliases:
            previous = alias_to_index.get(alias)
            if previous is None:
                alias_to_index[alias] = index
                alias_priorities[alias] = (star.identity.priority, star.identity.canonical_id)
                continue
            previous_priority, previous_id = alias_priorities[alias]
            current = (star.identity.priority, star.identity.canonical_id)
            if strict:
                raise ValueError(f"ambiguous alias {alias!r}: {previous_id} and {star.identity.canonical_id}")
            print(
                f"warning: ambiguous alias {alias!r}: keeping higher-priority mapping between "
                f"{previous_id} and {star.identity.canonical_id}",
                file=sys.stderr,
            )
            if current > (previous_priority, previous_id):
                alias_to_index[alias] = index
                alias_priorities[alias] = current
    return alias_to_index


def write_tsc1(path: Path, stars: list[StarForCatalog], strict_aliases: bool = False) -> CompiledCatalog:
    alias_to_index = validate_aliases(stars, strict_aliases)
    strings = StringTableBuilder()
    star_records = bytearray()

    epochs = [star.astrometry.reference_epoch for star in stars if star.astrometry.reference_epoch is not None]
    min_epoch = min(epochs) if epochs else math.nan
    max_epoch = max(epochs) if epochs else math.nan

    for star in stars:
        identity = star.identity
        astrometry = star.astrometry
        canonical_offset = strings.add(identity.canonical_id)
        display_offset = strings.add(identity.display_name)
        star_records.extend(
            STAR_RECORD_STRUCT.pack(
                canonical_offset,
                display_offset,
                zero_if_none(astrometry.gaia_dr3_source_id or identity.gaia_dr3_source_id),
                zero_if_none(astrometry.hip_id or identity.hip_id),
                zero_if_none(astrometry.hr_id or identity.hr_id),
                zero_if_none(astrometry.hd_id or identity.hd_id),
                nan_if_none(astrometry.ra_deg),
                nan_if_none(astrometry.dec_deg),
                nan_if_none(astrometry.pm_ra_mas_yr),
                nan_if_none(astrometry.pm_dec_mas_yr),
                nan_if_none(astrometry.parallax_mas),
                nan_if_none(astrometry.radial_velocity_km_s),
                nan_if_none(astrometry.reference_epoch),
                float(nan_if_none(astrometry.magnitude)),
                star.source_code,
                star.flags,
            )
        )

    alias_entries: list[tuple[int, str, int]] = []
    for alias, star_index in alias_to_index.items():
        alias_offset = strings.add(alias)
        alias_entries.append((fnv1a_64(alias), alias, star_index, alias_offset))
    alias_entries.sort(key=lambda item: (item[0], item[1]))

    alias_records = bytearray()
    for alias_hash, _alias, star_index, alias_offset in alias_entries:
        alias_records.extend(ALIAS_ENTRY_STRUCT.pack(alias_offset, star_index, alias_hash))

    star_records_offset = HEADER_STRUCT.size
    alias_records_offset = star_records_offset + len(star_records)
    string_table_offset = alias_records_offset + len(alias_records)
    string_table_size = len(strings.data)

    header = HEADER_STRUCT.pack(
        MAGIC,
        VERSION,
        0,
        len(stars),
        len(alias_entries),
        star_records_offset,
        alias_records_offset,
        string_table_offset,
        string_table_size,
        min_epoch,
        max_epoch,
        bytes(64),
    )

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as file:
        file.write(header)
        file.write(star_records)
        file.write(alias_records)
        file.write(strings.data)

    return CompiledCatalog(stars=stars, alias_count=len(alias_entries), string_table_size=string_table_size, min_epoch=min_epoch, max_epoch=max_epoch)


def read_header(data: bytes) -> tuple:
    if len(data) < HEADER_STRUCT.size:
        raise ValueError("file too small for TSC1 header")
    header = HEADER_STRUCT.unpack_from(data, 0)
    if header[0] != MAGIC:
        raise ValueError("invalid TSC1 magic")
    if header[1] != VERSION:
        raise ValueError(f"unsupported TSC1 version: {header[1]}")
    return header


def inspect_tsc1(path: Path, verbose: bool = True) -> dict[str, object]:
    data = path.read_bytes()
    header = read_header(data)
    (
        _magic,
        version,
        flags,
        star_count,
        alias_count,
        star_records_offset,
        alias_records_offset,
        string_table_offset,
        string_table_size,
        min_epoch,
        max_epoch,
        _reserved,
    ) = header
    expected_min_size = string_table_offset + string_table_size
    if len(data) < expected_min_size:
        raise ValueError("file truncated before string table end")
    strings = StringTableReader(data[string_table_offset : string_table_offset + string_table_size])

    source_counts: dict[str, int] = {}
    first_stars: list[dict[str, object]] = []
    for index in range(star_count):
        offset = star_records_offset + index * STAR_RECORD_STRUCT.size
        record = STAR_RECORD_STRUCT.unpack_from(data, offset)
        canonical_id = strings.get(record[0])
        display_name = strings.get(record[1])
        source = SOURCE_CODE_TO_NAME.get(record[14], f"Unknown({record[14]})")
        source_counts[source] = source_counts.get(source, 0) + 1
        if len(first_stars) < 5:
            first_stars.append(
                {
                    "index": index,
                    "canonical_id": canonical_id,
                    "display_name": display_name,
                    "source": source,
                    "epoch": record[12],
                }
            )

    info: dict[str, object] = {
        "path": str(path),
        "version": version,
        "flags": flags,
        "star_count": star_count,
        "alias_count": alias_count,
        "star_records_offset": star_records_offset,
        "alias_records_offset": alias_records_offset,
        "string_table_offset": string_table_offset,
        "string_table_size": string_table_size,
        "catalog_min_epoch": min_epoch,
        "catalog_max_epoch": max_epoch,
        "source_counts": source_counts,
        "first_stars": first_stars,
        "file_size": len(data),
    }
    if verbose:
        print(f"path: {path}")
        print(f"version: {version}")
        print(f"star_count: {star_count}")
        print(f"alias_count: {alias_count}")
        print(f"string_table_size: {string_table_size}")
        print(f"epoch_range: {min_epoch:g}..{max_epoch:g}")
        print("source_counts:")
        for source, count in sorted(source_counts.items()):
            print(f"  {source}: {count}")
        print("first_stars:")
        for star in first_stars:
            print(f"  {star['index']}: {star['canonical_id']} ({star['source']}, epoch={star['epoch']:g})")
    return info


def compile_catalog(args: argparse.Namespace) -> int:
    identity_rows = read_identity_manifest(args.identity_manifest)
    astrometry_rows = read_astrometry(args.astrometry)
    stars = build_catalog(identity_rows, astrometry_rows, args.tier)
    compiled = write_tsc1(args.output, stars, args.strict_aliases)
    print(
        f"wrote {args.output}: {len(compiled.stars)} stars, {compiled.alias_count} aliases, "
        f"{compiled.string_table_size} string bytes, epoch {compiled.min_epoch:g}..{compiled.max_epoch:g}"
    )
    inspect_tsc1(args.output, verbose=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Compile Taiyin star identity/astrometry CSVs into TSC1 binary catalogs.")
    parser.add_argument("--identity-manifest", type=Path)
    parser.add_argument("--astrometry", type=Path)
    parser.add_argument("--tier", choices=[TIER_FIXED, TIER_BSC, TIER_HIP])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--inspect", type=Path, help="Inspect an existing TSC1 file instead of compiling.")
    parser.add_argument("--strict-aliases", action="store_true", help="Fail on any ambiguous alias.")
    args = parser.parse_args()

    try:
        if args.inspect:
            inspect_tsc1(args.inspect, verbose=True)
            return 0
        missing = [
            name
            for name, value in (
                ("--identity-manifest", args.identity_manifest),
                ("--astrometry", args.astrometry),
                ("--tier", args.tier),
                ("--output", args.output),
            )
            if value is None
        ]
        if missing:
            raise ValueError(f"missing required compile arguments: {', '.join(missing)}")
        return compile_catalog(args)
    except Exception as exc:  # noqa: BLE001 - CLI reports actionable errors.
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
