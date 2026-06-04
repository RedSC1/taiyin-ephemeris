#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from star_catalog_sources import BSCRecord, HipparcosRecord, parse_bsc5_catalog, parse_hipparcos_main


DEFAULT_TAP_URL = "https://gea.esac.esa.int/tap-server/tap/sync"
DEFAULT_BATCH_SIZE = 1000
DEFAULT_TIMEOUT_SECONDS = 600
USER_AGENT = "taiyin-ephemeris-gaia-fetch/0.1"
HIPPARCOS_REFERENCE_EPOCH = "1991.25"
BSC_REFERENCE_EPOCH = "2000.0"

ASTROMETRY_FIELDNAMES = [
    "canonical_id",
    "tier_flags",
    "display_name",
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
    "match_method",
    "match_quality",
    "angular_distance_arcsec",
    "xm_flag",
    "notes",
]

GAIA_SELECT_FIELDS = """
    gs.source_id,
    gs.ra,
    gs.dec,
    gs.pmra,
    gs.pmdec,
    gs.parallax,
    gs.radial_velocity,
    gs.phot_g_mean_mag,
    gs.ref_epoch
"""


@dataclass(frozen=True)
class ManifestIdentity:
    canonical_id: str
    tier_flags: str
    display_name: str
    gaia_dr3_source_id: str
    hip_id: str
    hr_id: str
    hd_id: str
    vmag: str
    notes: str


class TapClient:
    def __init__(self, tap_url: str, timeout_seconds: int) -> None:
        self.tap_url = tap_url
        self.timeout_seconds = timeout_seconds

    def query_csv(self, adql: str) -> list[dict[str, str]]:
        payload = urlencode(
            {
                "REQUEST": "doQuery",
                "LANG": "ADQL",
                "FORMAT": "csv",
                "QUERY": adql,
            }
        ).encode("utf-8")
        request = Request(
            self.tap_url,
            data=payload,
            headers={"User-Agent": USER_AGENT, "Content-Type": "application/x-www-form-urlencoded"},
        )
        with urlopen(request, timeout=self.timeout_seconds) as response:
            text = response.read().decode("utf-8", errors="replace")
        if text.lstrip().startswith("<"):
            raise RuntimeError(f"TAP returned non-CSV response: {text[:500]}")
        reader = csv.DictReader(text.splitlines())
        return list(reader)


def parse_optional_id(value: str | None) -> str:
    return (value or "").strip()


def read_manifest(path: Path, limit: int | None = None) -> list[ManifestIdentity]:
    rows: list[ManifestIdentity] = []
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        required = {"canonical_id", "tier_flags", "display_name", "gaia_dr3_source_id", "hip_id", "hr_id", "hd_id", "vmag", "notes"}
        missing = sorted(required.difference(reader.fieldnames or []))
        if missing:
            raise ValueError(f"manifest missing columns: {', '.join(missing)}")
        for row in reader:
            rows.append(
                ManifestIdentity(
                    canonical_id=row["canonical_id"].strip(),
                    tier_flags=row["tier_flags"].strip(),
                    display_name=row["display_name"].strip(),
                    gaia_dr3_source_id=parse_optional_id(row.get("gaia_dr3_source_id")),
                    hip_id=parse_optional_id(row.get("hip_id")),
                    hr_id=parse_optional_id(row.get("hr_id")),
                    hd_id=parse_optional_id(row.get("hd_id")),
                    vmag=(row.get("vmag") or "").strip(),
                    notes=(row.get("notes") or "").strip(),
                )
            )
            if limit is not None and len(rows) >= limit:
                break
    return rows


def read_cache(path: Path | None) -> dict[str, dict[str, str]]:
    if path is None or not path.exists():
        return {}
    with path.open("r", encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file)
        if reader.fieldnames != ASTROMETRY_FIELDNAMES:
            raise ValueError(f"cache has unexpected columns: {path}")
        return {row["canonical_id"]: normalize_output_row(row) for row in reader if row.get("canonical_id")}


def normalize_output_row(row: dict[str, str]) -> dict[str, str]:
    return {name: (row.get(name) or "") for name in ASTROMETRY_FIELDNAMES}


def chunks(values: Sequence[str], batch_size: int) -> Iterable[list[str]]:
    for index in range(0, len(values), batch_size):
        yield list(values[index : index + batch_size])


def adql_int_list(values: Sequence[str]) -> str:
    cleaned: list[str] = []
    for value in values:
        if not value.isdigit():
            raise ValueError(f"expected numeric ID, got {value!r}")
        cleaned.append(value)
    return ", ".join(cleaned)


def fetch_by_source_ids(client: TapClient, source_ids: Sequence[str]) -> dict[str, dict[str, str]]:
    if not source_ids:
        return {}
    adql = f"""
SELECT
{GAIA_SELECT_FIELDS}
FROM gaiadr3.gaia_source AS gs
WHERE gs.source_id IN ({adql_int_list(source_ids)})
"""
    records = client.query_csv(adql)
    return {record["source_id"].strip(): record for record in records if record.get("source_id")}


def fetch_by_hip_ids(client: TapClient, hip_ids: Sequence[str]) -> dict[str, dict[str, str]]:
    if not hip_ids:
        return {}
    adql = f"""
SELECT
    bn.original_ext_source_id AS hip_id,
    bn.angular_distance,
    bn.xm_flag,
{GAIA_SELECT_FIELDS}
FROM gaiadr3.hipparcos2_best_neighbour AS bn
JOIN gaiadr3.gaia_source AS gs
    ON bn.source_id = gs.source_id
WHERE bn.original_ext_source_id IN ({adql_int_list(hip_ids)})
"""
    records = client.query_csv(adql)
    result: dict[str, dict[str, str]] = {}
    for record in records:
        hip_id = record.get("hip_id", "").strip()
        if hip_id and hip_id not in result:
            result[hip_id] = record
    return result


def retry_fetch(
    label: str,
    fetcher: Callable[[TapClient, Sequence[str]], dict[str, dict[str, str]]],
    client: TapClient,
    batch: Sequence[str],
    retries: int,
    backoff_seconds: float,
) -> dict[str, dict[str, str]]:
    attempts = retries + 1
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            return fetcher(client, batch)
        except Exception as exc:  # noqa: BLE001 - batch retry wrapper.
            last_error = exc
            if attempt >= attempts:
                break
            sleep_seconds = backoff_seconds * (2 ** (attempt - 1))
            print(
                f"warning: {label} batch failed attempt {attempt}/{attempts}: {exc}; retrying in {sleep_seconds:g}s",
                file=sys.stderr,
            )
            time.sleep(sleep_seconds)
    raise RuntimeError(f"{label} batch failed after {attempts} attempts: {last_error}")


def gaia_value(record: dict[str, str], name: str) -> str:
    return (record.get(name) or "").strip()


def format_optional_float(value: float | None) -> str:
    return "" if value is None else f"{value:.12g}"


def output_row_from_match(
    identity: ManifestIdentity,
    record: dict[str, str] | None,
    match_method: str,
    note: str = "",
) -> dict[str, str]:
    if record is None:
        return {
            "canonical_id": identity.canonical_id,
            "tier_flags": identity.tier_flags,
            "display_name": identity.display_name,
            "gaia_dr3_source_id": identity.gaia_dr3_source_id,
            "hip_id": identity.hip_id,
            "hr_id": identity.hr_id,
            "hd_id": identity.hd_id,
            "ra_deg": "",
            "dec_deg": "",
            "pm_ra_mas_yr": "",
            "pm_dec_mas_yr": "",
            "parallax_mas": "",
            "radial_velocity_km_s": "",
            "phot_g_mean_mag": "",
            "vmag": identity.vmag,
            "reference_epoch": "",
            "astrometry_source": "",
            "match_method": match_method,
            "match_quality": "missing",
            "angular_distance_arcsec": "",
            "xm_flag": "",
            "notes": merge_notes(identity.notes, note or "no Gaia DR3 match"),
        }

    source_id = gaia_value(record, "source_id")
    angular_distance = gaia_value(record, "angular_distance")
    xm_flag = gaia_value(record, "xm_flag")
    quality_parts: list[str] = []
    if xm_flag:
        quality_parts.append(f"xm_flag={xm_flag}")
    if angular_distance:
        quality_parts.append(f"angular_distance_arcsec={angular_distance}")
    return {
        "canonical_id": identity.canonical_id,
        "tier_flags": identity.tier_flags,
        "display_name": identity.display_name,
        "gaia_dr3_source_id": source_id or identity.gaia_dr3_source_id,
        "hip_id": identity.hip_id or gaia_value(record, "hip_id"),
        "hr_id": identity.hr_id,
        "hd_id": identity.hd_id,
        "ra_deg": gaia_value(record, "ra"),
        "dec_deg": gaia_value(record, "dec"),
        "pm_ra_mas_yr": gaia_value(record, "pmra"),
        "pm_dec_mas_yr": gaia_value(record, "pmdec"),
        "parallax_mas": gaia_value(record, "parallax"),
        "radial_velocity_km_s": gaia_value(record, "radial_velocity"),
        "phot_g_mean_mag": gaia_value(record, "phot_g_mean_mag"),
        "vmag": identity.vmag,
        "reference_epoch": gaia_value(record, "ref_epoch"),
        "astrometry_source": "Gaia DR3",
        "match_method": match_method,
        "match_quality": ";".join(quality_parts) if quality_parts else "direct",
        "angular_distance_arcsec": angular_distance,
        "xm_flag": xm_flag,
        "notes": merge_notes(identity.notes, note),
    }


def output_row_from_bsc_fallback(identity: ManifestIdentity, record: BSCRecord) -> dict[str, str]:
    return {
        "canonical_id": identity.canonical_id,
        "tier_flags": identity.tier_flags,
        "display_name": identity.display_name,
        "gaia_dr3_source_id": identity.gaia_dr3_source_id,
        "hip_id": identity.hip_id,
        "hr_id": identity.hr_id or str(record.hr_id),
        "hd_id": identity.hd_id or ("" if record.hd_id is None else str(record.hd_id)),
        "ra_deg": format_optional_float(record.ra_deg),
        "dec_deg": format_optional_float(record.dec_deg),
        "pm_ra_mas_yr": format_optional_float(record.pm_ra_mas_yr),
        "pm_dec_mas_yr": format_optional_float(record.pm_dec_mas_yr),
        "parallax_mas": format_optional_float(record.parallax_mas),
        "radial_velocity_km_s": format_optional_float(record.radial_velocity_km_s),
        "phot_g_mean_mag": "",
        "vmag": identity.vmag or format_optional_float(record.vmag),
        "reference_epoch": BSC_REFERENCE_EPOCH,
        "astrometry_source": "BSC5",
        "match_method": "bsc5_catalog_fallback",
        "match_quality": "fallback",
        "angular_distance_arcsec": "",
        "xm_flag": "",
        "notes": merge_notes(identity.notes, "Gaia/Hipparcos astrometry missing; used BSC5 fallback"),
    }


def output_row_from_hipparcos_fallback(identity: ManifestIdentity, record: HipparcosRecord) -> dict[str, str]:
    vmag = identity.vmag or format_optional_float(record.vmag) or format_optional_float(record.hp_mag)
    return {
        "canonical_id": identity.canonical_id,
        "tier_flags": identity.tier_flags,
        "display_name": identity.display_name,
        "gaia_dr3_source_id": identity.gaia_dr3_source_id,
        "hip_id": identity.hip_id or str(record.hip_id),
        "hr_id": identity.hr_id,
        "hd_id": identity.hd_id or ("" if record.hd_id is None else str(record.hd_id)),
        "ra_deg": format_optional_float(record.ra_deg),
        "dec_deg": format_optional_float(record.dec_deg),
        "pm_ra_mas_yr": format_optional_float(record.pm_ra_mas_yr),
        "pm_dec_mas_yr": format_optional_float(record.pm_dec_mas_yr),
        "parallax_mas": format_optional_float(record.parallax_mas),
        "radial_velocity_km_s": "",
        "phot_g_mean_mag": "",
        "vmag": vmag,
        "reference_epoch": HIPPARCOS_REFERENCE_EPOCH,
        "astrometry_source": "Hipparcos",
        "match_method": "hipparcos_main_fallback",
        "match_quality": "fallback",
        "angular_distance_arcsec": "",
        "xm_flag": "",
        "notes": merge_notes(identity.notes, "Gaia DR3 match missing; used Hipparcos fallback"),
    }


def merge_notes(*notes: str) -> str:
    unique: list[str] = []
    for note in notes:
        note = note.strip()
        if note and note not in unique:
            unique.append(note)
    return "; ".join(unique)


def bsc_has_position(record: BSCRecord) -> bool:
    return record.ra_deg is not None and record.dec_deg is not None


def hipparcos_has_position(record: HipparcosRecord) -> bool:
    return record.ra_deg is not None and record.dec_deg is not None


def load_bsc_astrometry(path: Path | None) -> dict[str, BSCRecord]:
    if path is None:
        return {}
    records: dict[str, BSCRecord] = {}
    for record in parse_bsc5_catalog(path):
        records[str(record.hr_id)] = record
    return records


def load_hipparcos_astrometry(path: Path | None) -> dict[str, HipparcosRecord]:
    if path is None:
        return {}
    records: dict[str, HipparcosRecord] = {}
    for record in parse_hipparcos_main(path):
        records[str(record.hip_id)] = record
    return records


def write_rows(path: Path, rows: Iterable[dict[str, str]]) -> int:
    materialized = [normalize_output_row(row) for row in rows]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=ASTROMETRY_FIELDNAMES)
        writer.writeheader()
        writer.writerows(materialized)
    return len(materialized)


def apply_missing_or_catalog_fallback(
    output_by_canonical: dict[str, dict[str, str]],
    identity: ManifestIdentity,
    hipparcos_records: dict[str, HipparcosRecord],
    bsc_records: dict[str, BSCRecord],
    match_method: str,
    note: str = "",
) -> None:
    if identity.hip_id and identity.hip_id in hipparcos_records and hipparcos_has_position(hipparcos_records[identity.hip_id]):
        output_by_canonical[identity.canonical_id] = output_row_from_hipparcos_fallback(
            identity,
            hipparcos_records[identity.hip_id],
        )
    elif identity.hr_id and identity.hr_id in bsc_records and bsc_has_position(bsc_records[identity.hr_id]):
        output_by_canonical[identity.canonical_id] = output_row_from_bsc_fallback(
            identity,
            bsc_records[identity.hr_id],
        )
    else:
        output_by_canonical[identity.canonical_id] = output_row_from_match(identity, None, match_method, note)


def ordered_available_rows(
    identities: Sequence[ManifestIdentity],
    output_by_canonical: dict[str, dict[str, str]],
) -> list[dict[str, str]]:
    return [output_by_canonical[identity.canonical_id] for identity in identities if identity.canonical_id in output_by_canonical]


def build_astrometry_rows(
    identities: Sequence[ManifestIdentity],
    client: TapClient,
    cache: dict[str, dict[str, str]],
    batch_size: int,
    sleep_seconds: float,
    verbose: bool = True,
    hipparcos_records: dict[str, HipparcosRecord] | None = None,
    bsc_records: dict[str, BSCRecord] | None = None,
    workers: int = 1,
    retries: int = 2,
    backoff_seconds: float = 5.0,
    checkpoint: Callable[[dict[str, dict[str, str]]], None] | None = None,
) -> list[dict[str, str]]:
    hipparcos_records = hipparcos_records or {}
    bsc_records = bsc_records or {}
    output_by_canonical: dict[str, dict[str, str]] = {}
    missing_identities: list[ManifestIdentity] = []

    for identity in identities:
        cached = cache.get(identity.canonical_id)
        if cached is None:
            missing_identities.append(identity)
            continue
        source = cached.get("astrometry_source")
        has_position = bool(cached.get("ra_deg") and cached.get("dec_deg") and cached.get("reference_epoch"))
        if source == "Gaia DR3" and has_position:
            output_by_canonical[identity.canonical_id] = cached
        elif source == "Hipparcos" and identity.hip_id and identity.hip_id in hipparcos_records and hipparcos_has_position(hipparcos_records[identity.hip_id]):
            output_by_canonical[identity.canonical_id] = output_row_from_hipparcos_fallback(identity, hipparcos_records[identity.hip_id])
        elif source == "BSC5" and identity.hr_id and identity.hr_id in bsc_records and bsc_has_position(bsc_records[identity.hr_id]):
            output_by_canonical[identity.canonical_id] = output_row_from_bsc_fallback(identity, bsc_records[identity.hr_id])
        elif source and has_position:
            output_by_canonical[identity.canonical_id] = cached
        elif not source and (hipparcos_records or bsc_records):
            apply_missing_or_catalog_fallback(
                output_by_canonical,
                identity,
                hipparcos_records,
                bsc_records,
                "cached_missing",
            )
        else:
            missing_identities.append(identity)

    source_id_to_identities: dict[str, list[ManifestIdentity]] = {}
    hip_id_to_identities: dict[str, list[ManifestIdentity]] = {}
    no_query_identities: list[ManifestIdentity] = []

    for identity in missing_identities:
        if identity.gaia_dr3_source_id:
            source_id_to_identities.setdefault(identity.gaia_dr3_source_id, []).append(identity)
        elif identity.hip_id:
            hip_id_to_identities.setdefault(identity.hip_id, []).append(identity)
        else:
            no_query_identities.append(identity)

    for identity in no_query_identities:
        apply_missing_or_catalog_fallback(
            output_by_canonical,
            identity,
            hipparcos_records,
            bsc_records,
            "no_query_id",
            "no gaia_dr3_source_id or hip_id",
        )

    if checkpoint:
        checkpoint(output_by_canonical)

    query_batches(
        label="source_id",
        ids=sorted(source_id_to_identities.keys(), key=int),
        batch_size=batch_size,
        workers=workers,
        sleep_seconds=sleep_seconds,
        verbose=verbose,
        fetcher=fetch_by_source_ids,
        client=client,
        retries=retries,
        backoff_seconds=backoff_seconds,
        apply_batch=lambda batch, matches: apply_source_id_matches(
            output_by_canonical,
            source_id_to_identities,
            batch,
            matches,
            hipparcos_records,
            bsc_records,
        ),
        checkpoint=lambda: checkpoint(output_by_canonical) if checkpoint else None,
    )

    query_batches(
        label="Hipparcos",
        ids=sorted(hip_id_to_identities.keys(), key=int),
        batch_size=batch_size,
        workers=workers,
        sleep_seconds=sleep_seconds,
        verbose=verbose,
        fetcher=fetch_by_hip_ids,
        client=client,
        retries=retries,
        backoff_seconds=backoff_seconds,
        apply_batch=lambda batch, matches: apply_hip_matches(
            output_by_canonical,
            hip_id_to_identities,
            batch,
            matches,
            hipparcos_records,
            bsc_records,
        ),
        checkpoint=lambda: checkpoint(output_by_canonical) if checkpoint else None,
    )

    return [output_by_canonical[identity.canonical_id] for identity in identities]


def query_batches(
    label: str,
    ids: list[str],
    batch_size: int,
    workers: int,
    sleep_seconds: float,
    verbose: bool,
    fetcher: Callable[[TapClient, Sequence[str]], dict[str, dict[str, str]]],
    client: TapClient,
    retries: int,
    backoff_seconds: float,
    apply_batch: Callable[[list[str], dict[str, dict[str, str]]], None],
    checkpoint: Callable[[], None] | None,
) -> None:
    batches = list(chunks(ids, batch_size))
    if not batches:
        return
    if workers <= 1:
        for batch_index, batch in enumerate(batches, 1):
            if verbose:
                print(f"query {label} batch {batch_index}/{len(batches)}: {len(batch)} IDs", file=sys.stderr)
            matches = retry_fetch(label, fetcher, client, batch, retries, backoff_seconds)
            apply_batch(batch, matches)
            if checkpoint:
                checkpoint()
            if sleep_seconds > 0:
                time.sleep(sleep_seconds)
        return

    with ThreadPoolExecutor(max_workers=workers) as executor:
        future_to_batch: dict[object, tuple[int, list[str]]] = {}
        for batch_index, batch in enumerate(batches, 1):
            if verbose:
                print(f"submit {label} batch {batch_index}/{len(batches)}: {len(batch)} IDs", file=sys.stderr)
            future = executor.submit(retry_fetch, label, fetcher, client, batch, retries, backoff_seconds)
            future_to_batch[future] = (batch_index, batch)
            if sleep_seconds > 0:
                time.sleep(sleep_seconds)
        completed = 0
        for future in as_completed(future_to_batch):
            batch_index, batch = future_to_batch[future]
            matches = future.result()
            apply_batch(batch, matches)
            completed += 1
            if verbose:
                print(
                    f"completed {label} batch {batch_index}/{len(batches)} ({completed}/{len(batches)})",
                    file=sys.stderr,
                )
            if checkpoint:
                checkpoint()


def apply_source_id_matches(
    output_by_canonical: dict[str, dict[str, str]],
    source_id_to_identities: dict[str, list[ManifestIdentity]],
    batch: Sequence[str],
    matches: dict[str, dict[str, str]],
    hipparcos_records: dict[str, HipparcosRecord],
    bsc_records: dict[str, BSCRecord],
) -> None:
    for source_id in batch:
        for identity in source_id_to_identities[source_id]:
            record = matches.get(source_id)
            if record is not None:
                output_by_canonical[identity.canonical_id] = output_row_from_match(
                    identity,
                    record,
                    "gaia_dr3_source_id",
                )
            else:
                apply_missing_or_catalog_fallback(
                    output_by_canonical,
                    identity,
                    hipparcos_records,
                    bsc_records,
                    "gaia_dr3_source_id",
                )


def apply_hip_matches(
    output_by_canonical: dict[str, dict[str, str]],
    hip_id_to_identities: dict[str, list[ManifestIdentity]],
    batch: Sequence[str],
    matches: dict[str, dict[str, str]],
    hipparcos_records: dict[str, HipparcosRecord],
    bsc_records: dict[str, BSCRecord],
) -> None:
    for hip_id in batch:
        for identity in hip_id_to_identities[hip_id]:
            record = matches.get(hip_id)
            if record is not None:
                output_by_canonical[identity.canonical_id] = output_row_from_match(
                    identity,
                    record,
                    "hipparcos2_best_neighbour",
                )
            else:
                apply_missing_or_catalog_fallback(
                    output_by_canonical,
                    identity,
                    hipparcos_records,
                    bsc_records,
                    "hipparcos2_best_neighbour",
                )


def default_cache_path() -> Path:
    return Path("data/stars/cache/gaia_dr3_astrometry_cache.csv")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch Gaia DR3 astrometry for a Taiyin star identity manifest.")
    parser.add_argument("--input-manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, default=default_cache_path())
    parser.add_argument("--hip-catalog", type=Path, help="Optional local CDS I/239 hip_main.dat for Hipparcos fallback.")
    parser.add_argument("--bsc-catalog", type=Path, help="Optional local CDS V/50 catalog.dat for BSC5 fallback.")
    parser.add_argument("--tap-url", default=DEFAULT_TAP_URL)
    parser.add_argument("--batch-size", type=int, default=DEFAULT_BATCH_SIZE)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--sleep", type=float, default=0.25, help="Seconds to sleep between TAP batch submissions.")
    parser.add_argument("--workers", type=int, default=1, help="Concurrent TAP batch workers. Use conservatively.")
    parser.add_argument("--retries", type=int, default=3, help="Retries per failed TAP batch.")
    parser.add_argument("--backoff", type=float, default=5.0, help="Initial retry backoff in seconds.")
    parser.add_argument("--limit", type=int, help="Only process the first N manifest rows; useful for smoke tests.")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    if args.batch_size <= 0:
        print("error: --batch-size must be positive", file=sys.stderr)
        return 1
    if args.workers <= 0:
        print("error: --workers must be positive", file=sys.stderr)
        return 1
    if args.retries < 0:
        print("error: --retries must be non-negative", file=sys.stderr)
        return 1

    try:
        identities = read_manifest(args.input_manifest, args.limit)
        cache = read_cache(args.cache)
        hipparcos_records = load_hipparcos_astrometry(args.hip_catalog)
        bsc_records = load_bsc_astrometry(args.bsc_catalog)
        missing = [
            identity
            for identity in identities
            if identity.canonical_id not in cache or (not cache[identity.canonical_id].get("astrometry_source") and (hipparcos_records or bsc_records))
        ]
        source_count = sum(1 for identity in missing if identity.gaia_dr3_source_id)
        hip_count = sum(1 for identity in missing if not identity.gaia_dr3_source_id and identity.hip_id)
        no_query_count = len(missing) - source_count - hip_count
        print(
            f"loaded {len(identities)} manifest rows; {len(cache)} cached; "
            f"{len(hipparcos_records)} Hipparcos fallback rows; "
            f"{len(bsc_records)} BSC fallback rows; "
            f"to query source_id={source_count}, hip={hip_count}, no_query_id={no_query_count}",
            file=sys.stderr,
        )
        if args.dry_run:
            return 0

        client = TapClient(args.tap_url, args.timeout)

        def checkpoint(output_by_canonical: dict[str, dict[str, str]]) -> None:
            if not args.cache:
                return
            write_rows(args.cache, ordered_available_rows(identities, output_by_canonical))

        rows = build_astrometry_rows(
            identities,
            client,
            cache,
            args.batch_size,
            args.sleep,
            hipparcos_records=hipparcos_records,
            bsc_records=bsc_records,
            workers=args.workers,
            retries=args.retries,
            backoff_seconds=args.backoff,
            checkpoint=checkpoint,
        )
        output_count = write_rows(args.output, rows)
        if args.cache:
            write_rows(args.cache, rows)
        matched_count = sum(1 for row in rows if row.get("astrometry_source") == "Gaia DR3")
        hipparcos_fallback_count = sum(1 for row in rows if row.get("astrometry_source") == "Hipparcos")
        bsc_fallback_count = sum(1 for row in rows if row.get("astrometry_source") == "BSC5")
        missing_count = output_count - matched_count - hipparcos_fallback_count - bsc_fallback_count
        print(
            f"wrote {args.output}: {output_count} rows "
            f"({matched_count} Gaia matched, {hipparcos_fallback_count} Hipparcos fallback, "
            f"{bsc_fallback_count} BSC5 fallback, {missing_count} missing)"
        )
        if args.cache:
            print(f"wrote {args.cache}: {output_count} rows")
    except Exception as exc:  # noqa: BLE001 - CLI reports actionable errors.
        print(f"error: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
