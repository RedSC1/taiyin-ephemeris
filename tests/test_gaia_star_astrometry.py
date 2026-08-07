#!/usr/bin/env python3

from __future__ import annotations

import csv
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from fetch_gaia_star_astrometry import (  # noqa: E402
    ASTROMETRY_FIELDNAMES,
    ManifestIdentity,
    build_astrometry_rows,
    read_cache,
    read_manifest,
    write_rows,
)
from generate_star_identity_manifest import FIELDNAMES as MANIFEST_FIELDNAMES  # noqa: E402
from star_catalog_sources import BSCRecord, HipparcosRecord  # noqa: E402


class FakeTapClient:
    def __init__(self) -> None:
        self.queries: list[str] = []

    def query_csv(self, adql: str) -> list[dict[str, str]]:
        self.queries.append(adql)
        if "hipparcos2_best_neighbour" in adql:
            return [
                {
                    "hip_id": "65474",
                    "angular_distance": "0.0002",
                    "xm_flag": "8",
                    "source_id": "6193600049863267200",
                    "ra": "201.29835230",
                    "dec": "-11.16131948",
                    "pmra": "-42.35",
                    "pmdec": "-31.73",
                    "parallax": "12.44",
                    "radial_velocity": "1.0",
                    "phot_g_mean_mag": "1.2",
                    "ref_epoch": "2016.0",
                }
            ]
        return [
            {
                "source_id": "123456789",
                "ra": "1.25",
                "dec": "2.5",
                "pmra": "3.75",
                "pmdec": "4.25",
                "parallax": "5.5",
                "radial_velocity": "6.5",
                "phot_g_mean_mag": "7.5",
                "ref_epoch": "2016.0",
            }
        ]


class FailingTapClient:
    def query_csv(self, adql: str) -> list[dict[str, str]]:
        raise AssertionError(f"unexpected TAP query: {adql}")


def expect_true(value: bool, label: str, failures: list[str]) -> None:
    if not value:
        failures.append(f"expected true: {label}")


def expect_equal(actual, expected, label: str, failures: list[str]) -> None:
    if actual != expected:
        failures.append(f"{label}: actual={actual!r} expected={expected!r}")


def write_manifest_fixture(path: Path) -> None:
    rows = [
        {
            "tier_flags": "fixed_traditional|bright_bsc|hipparcos",
            "canonical_id": "spica",
            "display_name": "Spica",
            "aliases": "hip_65474,hr_5056",
            "hip_id": "65474",
            "hr_id": "5056",
            "hd_id": "116658",
            "gaia_dr3_source_id": "",
            "simbad_id": "* alf Vir",
            "bayer": "alpha Vir",
            "flamsteed": "",
            "constellation": "Vir",
            "vmag": "0.98",
            "priority": "100",
            "notes": "fixture",
        },
        {
            "tier_flags": "hipparcos",
            "canonical_id": "source_only",
            "display_name": "Source Only",
            "aliases": "gaia_dr3_123456789",
            "hip_id": "",
            "hr_id": "",
            "hd_id": "",
            "gaia_dr3_source_id": "123456789",
            "simbad_id": "",
            "bayer": "",
            "flamsteed": "",
            "constellation": "",
            "vmag": "",
            "priority": "10",
            "notes": "",
        },
        {
            "tier_flags": "fixed_traditional",
            "canonical_id": "galactic_center_j2000",
            "display_name": "Galactic Center (J2000 direction)",
            "aliases": "galactic_center,gc",
            "hip_id": "",
            "hr_id": "",
            "hd_id": "",
            "gaia_dr3_source_id": "",
            "simbad_id": "Galactic Center",
            "bayer": "",
            "flamsteed": "",
            "constellation": "Sgr",
            "vmag": "",
            "priority": "100",
            "notes": "not a Gaia point-source lookup",
        },
    ]
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=MANIFEST_FIELDNAMES)
        writer.writeheader()
        writer.writerows(rows)


def test_read_manifest(failures: list[str]) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "manifest.csv"
        write_manifest_fixture(path)
        identities = read_manifest(path)
    expect_equal(len(identities), 3, "manifest fixture row count", failures)
    expect_equal(identities[0].canonical_id, "spica", "manifest canonical id", failures)
    expect_equal(identities[0].hip_id, "65474", "manifest HIP id", failures)


def test_build_rows_from_fake_tap(failures: list[str]) -> None:
    identities = [
        ManifestIdentity("spica", "fixed_traditional|bright_bsc|hipparcos", "Spica", "", "65474", "5056", "116658", "0.98", "fixture"),
        ManifestIdentity("source_only", "hipparcos", "Source Only", "123456789", "", "", "", "", ""),
        ManifestIdentity("no_query", "fixed_traditional", "No Query", "", "", "", "", "", "manual direction"),
    ]
    client = FakeTapClient()
    rows = build_astrometry_rows(identities, client, {}, batch_size=10, sleep_seconds=0, verbose=False)
    by_id = {row["canonical_id"]: row for row in rows}
    expect_equal(len(client.queries), 2, "fake TAP query count", failures)
    expect_equal(by_id["spica"]["gaia_dr3_source_id"], "6193600049863267200", "HIP match source id", failures)
    expect_equal(by_id["spica"]["match_method"], "hipparcos2_best_neighbour", "HIP match method", failures)
    expect_equal(by_id["source_only"]["ra_deg"], "1.25", "source-id match RA", failures)
    expect_equal(by_id["source_only"]["match_method"], "gaia_dr3_source_id", "source-id match method", failures)
    expect_equal(by_id["no_query"]["match_quality"], "missing", "no-query missing quality", failures)
    expect_true("no gaia_dr3_source_id or hip_id" in by_id["no_query"]["notes"], "no-query note", failures)


def test_hipparcos_fallback_for_gaia_miss(failures: list[str]) -> None:
    identities = [
        ManifestIdentity("gaia_miss", "hipparcos", "Gaia Miss", "", "42", "", "", "", "needs fallback"),
    ]
    hipparcos_records = {
        "42": HipparcosRecord(
            hip_id=42,
            hd_id=4242,
            vmag=8.1,
            ra_deg=12.5,
            dec_deg=-3.25,
            parallax_mas=4.5,
            pm_ra_mas_yr=5.5,
            pm_dec_mas_yr=-6.5,
            hp_mag=8.2,
        )
    }
    rows = build_astrometry_rows(
        identities,
        FakeTapClient(),
        {},
        batch_size=10,
        sleep_seconds=0,
        verbose=False,
        hipparcos_records=hipparcos_records,
    )
    row = rows[0]
    expect_equal(row["astrometry_source"], "Hipparcos", "fallback source", failures)
    expect_equal(row["match_method"], "hipparcos_main_fallback", "fallback method", failures)
    expect_equal(row["reference_epoch"], "1991.25", "fallback epoch", failures)
    expect_equal(row["ra_deg"], "12.5", "fallback RA", failures)
    expect_equal(row["hd_id"], "4242", "fallback HD", failures)


def test_bsc_fallback_for_no_hip_row(failures: list[str]) -> None:
    identities = [
        ManifestIdentity("hr_only", "bright_bsc", "HR Only", "", "", "99", "12345", "6.5", "needs BSC fallback"),
    ]
    bsc_records = {
        "99": BSCRecord(
            hr_id=99,
            hd_id=12345,
            raw_name="HR Only",
            vmag=6.5,
            ra_deg=1.25,
            dec_deg=-2.5,
            pm_ra_mas_yr=3.5,
            pm_dec_mas_yr=-4.5,
            parallax_mas=5.5,
            radial_velocity_km_s=6.0,
        )
    }
    rows = build_astrometry_rows(
        identities,
        FailingTapClient(),
        {},
        batch_size=10,
        sleep_seconds=0,
        verbose=False,
        bsc_records=bsc_records,
    )
    row = rows[0]
    expect_equal(row["astrometry_source"], "BSC5", "BSC fallback source", failures)
    expect_equal(row["match_method"], "bsc5_catalog_fallback", "BSC fallback method", failures)
    expect_equal(row["reference_epoch"], "2000.0", "BSC fallback epoch", failures)
    expect_equal(row["ra_deg"], "1.25", "BSC fallback RA", failures)
    expect_equal(row["radial_velocity_km_s"], "6", "BSC fallback radial velocity", failures)


def test_cache_roundtrip_and_skip(failures: list[str]) -> None:
    cached_row = {name: "" for name in ASTROMETRY_FIELDNAMES}
    cached_row.update(
        {
            "canonical_id": "cached_star",
            "tier_flags": "hipparcos",
            "display_name": "Cached Star",
            "gaia_dr3_source_id": "42",
            "ra_deg": "10.0",
            "dec_deg": "20.0",
            "reference_epoch": "2016.0",
            "astrometry_source": "Gaia DR3",
            "match_method": "gaia_dr3_source_id",
            "match_quality": "direct",
        }
    )
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "cache.csv"
        write_rows(path, [cached_row])
        cache = read_cache(path)
    rows = build_astrometry_rows(
        [ManifestIdentity("cached_star", "hipparcos", "Cached Star", "42", "", "", "", "", "")],
        FailingTapClient(),
        cache,
        batch_size=10,
        sleep_seconds=0,
        verbose=False,
    )
    expect_equal(len(rows), 1, "cached output row count", failures)
    expect_equal(rows[0]["ra_deg"], "10.0", "cached RA", failures)


def main() -> int:
    failures: list[str] = []
    test_read_manifest(failures)
    test_build_rows_from_fake_tap(failures)
    test_hipparcos_fallback_for_gaia_miss(failures)
    test_bsc_fallback_for_no_hip_row(failures)
    test_cache_roundtrip_and_skip(failures)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("test_gaia_star_astrometry: ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
