#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from star_catalog_sources import BSCRecord, HipparcosRecord, parse_bsc5_catalog, parse_hipparcos_main


FIELDNAMES = [
    "tier_flags",
    "canonical_id",
    "display_name",
    "aliases",
    "hip_id",
    "hr_id",
    "hd_id",
    "gaia_dr3_source_id",
    "simbad_id",
    "bayer",
    "flamsteed",
    "constellation",
    "vmag",
    "priority",
    "notes",
]

TIER_FIXED = "fixed_traditional"
TIER_BSC = "bright_bsc"
TIER_HIP = "hipparcos"

BSC_DOWNLOAD_URL = "https://cdsarc.cds.unistra.fr/ftp/V/50/catalog.gz"
HIP_DOWNLOAD_URL = "https://cdsarc.cds.unistra.fr/ftp/I/239/hip_main.dat"


@dataclass
class StarIdentity:
    canonical_id: str
    display_name: str
    tier_flags: set[str] = field(default_factory=set)
    aliases: set[str] = field(default_factory=set)
    hip_id: int | None = None
    hr_id: int | None = None
    hd_id: int | None = None
    gaia_dr3_source_id: int | None = None
    simbad_id: str = ""
    bayer: str = ""
    flamsteed: str = ""
    constellation: str = ""
    vmag: float | None = None
    priority: int = 0
    notes: str = ""


def snake_case(value: str) -> str:
    value = value.strip().lower()
    value = value.replace("*", "")
    value = value.replace("+", " plus ")
    value = value.replace("/", " ")
    value = value.replace("-", " ")
    value = re.sub(r"[^a-z0-9]+", "_", value)
    value = re.sub(r"_+", "_", value).strip("_")
    return value


def parse_optional_int(value: str) -> int | None:
    value = value.strip()
    return int(value) if value else None


def parse_optional_float(value: str) -> float | None:
    value = value.strip()
    return float(value) if value else None


def format_optional_int(value: int | None) -> str:
    return "" if value is None else str(value)


def format_optional_float(value: float | None) -> str:
    if value is None:
        return ""
    return f"{value:.6g}"


def normalize_alias(alias: str) -> str:
    normalized = snake_case(alias)
    return normalized


def add_alias(identity: StarIdentity, alias: str) -> None:
    normalized = normalize_alias(alias)
    if normalized and normalized != identity.canonical_id:
        identity.aliases.add(normalized)


def add_standard_aliases(identity: StarIdentity) -> None:
    if identity.hip_id is not None:
        add_alias(identity, f"hip_{identity.hip_id}")
    if identity.hr_id is not None:
        add_alias(identity, f"hr_{identity.hr_id}")
    if identity.hd_id is not None:
        add_alias(identity, f"hd_{identity.hd_id}")
    if identity.gaia_dr3_source_id is not None:
        add_alias(identity, f"gaia_dr3_{identity.gaia_dr3_source_id}")
    if identity.bayer:
        add_alias(identity, identity.bayer)
    if identity.flamsteed:
        add_alias(identity, identity.flamsteed)
    if identity.display_name:
        add_alias(identity, identity.display_name)


def load_fixed_seed(path: Path) -> list[StarIdentity]:
    rows: list[StarIdentity] = []
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        missing = [name for name in FIELDNAMES if name not in (reader.fieldnames or [])]
        if missing:
            raise ValueError(f"fixed seed missing columns: {', '.join(missing)}")
        for row_number, row in enumerate(reader, 2):
            canonical_id = snake_case(row["canonical_id"])
            if not canonical_id:
                raise ValueError(f"empty canonical_id in {path}:{row_number}")
            identity = StarIdentity(
                canonical_id=canonical_id,
                display_name=row["display_name"].strip() or canonical_id,
                tier_flags=set(filter(None, row["tier_flags"].split("|"))),
                hip_id=parse_optional_int(row["hip_id"]),
                hr_id=parse_optional_int(row["hr_id"]),
                hd_id=parse_optional_int(row["hd_id"]),
                gaia_dr3_source_id=parse_optional_int(row["gaia_dr3_source_id"]),
                simbad_id=row["simbad_id"].strip(),
                bayer=row["bayer"].strip(),
                flamsteed=row["flamsteed"].strip(),
                constellation=row["constellation"].strip(),
                vmag=parse_optional_float(row["vmag"]),
                priority=int(row["priority"] or 0),
                notes=row["notes"].strip(),
            )
            if not identity.tier_flags:
                identity.tier_flags.add(TIER_FIXED)
            for alias in row["aliases"].split(","):
                add_alias(identity, alias)
            add_standard_aliases(identity)
            rows.append(identity)
    return rows


class ManifestBuilder:
    def __init__(self) -> None:
        self.rows: dict[str, StarIdentity] = {}
        self.by_hip: dict[int, str] = {}
        self.by_hr: dict[int, str] = {}
        self.by_hd: dict[int, str] = {}

    def add_or_merge(self, incoming: StarIdentity) -> StarIdentity:
        key = self._find_existing_key(incoming)
        if key is None:
            key = incoming.canonical_id
            if key in self.rows:
                raise ValueError(f"duplicate canonical_id with incompatible IDs: {key}")
            self.rows[key] = incoming
        else:
            self._merge(self.rows[key], incoming)
        self._index(self.rows[key])
        return self.rows[key]

    def _find_existing_key(self, identity: StarIdentity) -> str | None:
        if identity.canonical_id in self.rows:
            return identity.canonical_id
        if identity.hr_id is not None and identity.hr_id in self.by_hr:
            return self.by_hr[identity.hr_id]
        if identity.hip_id is not None and identity.hip_id in self.by_hip:
            return self.by_hip[identity.hip_id]
        if identity.hd_id is not None and identity.hd_id in self.by_hd:
            key = self.by_hd[identity.hd_id]
            target = self.rows[key]
            if (
                target.hip_id is not None
                and identity.hip_id is not None
                and target.hip_id != identity.hip_id
            ):
                return None
            if target.hr_id is not None and identity.hr_id is not None and target.hr_id != identity.hr_id:
                return None
            return key
        return None

    def _merge(self, target: StarIdentity, incoming: StarIdentity) -> None:
        target.tier_flags.update(incoming.tier_flags)
        target.aliases.update(incoming.aliases)
        target.hip_id = target.hip_id if target.hip_id is not None else incoming.hip_id
        target.hr_id = target.hr_id if target.hr_id is not None else incoming.hr_id
        target.hd_id = target.hd_id if target.hd_id is not None else incoming.hd_id
        target.gaia_dr3_source_id = target.gaia_dr3_source_id if target.gaia_dr3_source_id is not None else incoming.gaia_dr3_source_id
        target.simbad_id = target.simbad_id or incoming.simbad_id
        target.bayer = target.bayer or incoming.bayer
        target.flamsteed = target.flamsteed or incoming.flamsteed
        target.constellation = target.constellation or incoming.constellation
        target.vmag = target.vmag if target.vmag is not None else incoming.vmag
        if incoming.priority > target.priority:
            target.priority = incoming.priority
        if incoming.notes and incoming.notes not in target.notes:
            target.notes = (target.notes + "; " + incoming.notes).strip("; ")
        add_standard_aliases(target)

    def _index(self, identity: StarIdentity) -> None:
        if identity.hip_id is not None:
            self.by_hip[identity.hip_id] = identity.canonical_id
        if identity.hr_id is not None:
            self.by_hr[identity.hr_id] = identity.canonical_id
        if identity.hd_id is not None and identity.hd_id not in self.by_hd:
            self.by_hd[identity.hd_id] = identity.canonical_id

    def sorted_rows(self) -> list[StarIdentity]:
        return sorted(
            self.rows.values(),
            key=lambda row: (
                -row.priority,
                row.hr_id if row.hr_id is not None else 10**9,
                row.hip_id if row.hip_id is not None else 10**9,
                row.canonical_id,
            ),
        )


def identity_from_bsc(record: BSCRecord) -> StarIdentity:
    identity = StarIdentity(
        canonical_id=f"hr_{record.hr_id}",
        display_name=record.raw_name or f"HR {record.hr_id}",
        tier_flags={TIER_BSC},
        hr_id=record.hr_id,
        hd_id=record.hd_id,
        vmag=record.vmag,
        priority=20,
    )
    add_standard_aliases(identity)
    if record.raw_name:
        add_alias(identity, record.raw_name)
    return identity


def identity_from_hipparcos(record: HipparcosRecord) -> StarIdentity:
    identity = StarIdentity(
        canonical_id=f"hip_{record.hip_id}",
        display_name=f"HIP {record.hip_id}",
        tier_flags={TIER_HIP},
        hip_id=record.hip_id,
        hd_id=record.hd_id,
        vmag=record.vmag,
        priority=10,
    )
    add_standard_aliases(identity)
    return identity


def row_to_dict(identity: StarIdentity) -> dict[str, str]:
    add_standard_aliases(identity)
    return {
        "tier_flags": "|".join(sorted(identity.tier_flags)),
        "canonical_id": identity.canonical_id,
        "display_name": identity.display_name,
        "aliases": ",".join(sorted(identity.aliases)),
        "hip_id": format_optional_int(identity.hip_id),
        "hr_id": format_optional_int(identity.hr_id),
        "hd_id": format_optional_int(identity.hd_id),
        "gaia_dr3_source_id": format_optional_int(identity.gaia_dr3_source_id),
        "simbad_id": identity.simbad_id,
        "bayer": identity.bayer,
        "flamsteed": identity.flamsteed,
        "constellation": identity.constellation,
        "vmag": format_optional_float(identity.vmag),
        "priority": str(identity.priority),
        "notes": identity.notes,
    }


def write_manifest(path: Path, rows: Iterable[StarIdentity]) -> int:
    path.parent.mkdir(parents=True, exist_ok=True)
    materialized = list(rows)
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=FIELDNAMES)
        writer.writeheader()
        for row in materialized:
            writer.writerow(row_to_dict(row))
    return len(materialized)


def filter_by_tier(rows: Iterable[StarIdentity], tier: str) -> list[StarIdentity]:
    return [row for row in rows if tier in row.tier_flags]


def build_manifest(args: argparse.Namespace) -> ManifestBuilder:
    builder = ManifestBuilder()
    if args.fixed_seed:
        for identity in load_fixed_seed(args.fixed_seed):
            builder.add_or_merge(identity)

    if args.tier in {"bsc", "hipparcos", "all"}:
        if not args.bsc_catalog:
            raise ValueError(f"BSC catalog required for tier {args.tier}; download {BSC_DOWNLOAD_URL}")
        for record in parse_bsc5_catalog(args.bsc_catalog):
            builder.add_or_merge(identity_from_bsc(record))

    if args.tier in {"hipparcos", "all"}:
        if not args.hip_catalog:
            raise ValueError(f"Hipparcos catalog required for tier {args.tier}; download {HIP_DOWNLOAD_URL}")
        for record in parse_hipparcos_main(args.hip_catalog):
            builder.add_or_merge(identity_from_hipparcos(record))

    return builder


def default_fixed_seed_path() -> Path:
    return Path(__file__).resolve().with_name("star_identity_fixed_seed.csv")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate Taiyin star identity manifest CSV files.")
    parser.add_argument("--tier", choices=["fixed", "bsc", "hipparcos", "all"], default="all")
    parser.add_argument("--fixed-seed", type=Path, default=default_fixed_seed_path())
    parser.add_argument("--bsc-catalog", type=Path)
    parser.add_argument("--hip-catalog", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    try:
        builder = build_manifest(args)
    except Exception as exc:  # noqa: BLE001 - CLI reports actionable errors.
        print(f"error: {exc}", file=sys.stderr)
        return 1

    rows = builder.sorted_rows()
    outputs: list[tuple[str, list[StarIdentity]]] = []
    if args.tier in {"fixed", "all"}:
        outputs.append(("star_identity_fixed_traditional.csv", filter_by_tier(rows, TIER_FIXED)))
    if args.tier in {"bsc", "all"}:
        outputs.append(("star_identity_bright_bsc.csv", filter_by_tier(rows, TIER_BSC)))
    if args.tier in {"hipparcos", "all"}:
        outputs.append(("star_identity_hipparcos.csv", filter_by_tier(rows, TIER_HIP)))
    outputs.append(("star_identity_merged.csv", rows))

    for filename, output_rows in outputs:
        path = args.output_dir / filename
        if args.dry_run:
            print(f"dry-run {path}: {len(output_rows)} rows")
        else:
            count = write_manifest(path, output_rows)
            print(f"wrote {path}: {count} rows")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
