#!/usr/bin/env python3

from __future__ import annotations

import csv
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from generate_star_identity_manifest import (  # noqa: E402
    FIELDNAMES,
    TIER_BSC,
    TIER_FIXED,
    TIER_HIP,
    ManifestBuilder,
    build_manifest,
    filter_by_tier,
    identity_from_bsc,
    identity_from_hipparcos,
    load_fixed_seed,
    write_manifest,
)
from star_catalog_sources import BSCRecord, HipparcosRecord, parse_bsc5_line, parse_hipparcos_line  # noqa: E402


def expect_true(value: bool, label: str, failures: list[str]) -> None:
    if not value:
        failures.append(f"expected true: {label}")


def expect_equal(actual, expected, label: str, failures: list[str]) -> None:
    if actual != expected:
        failures.append(f"{label}: actual={actual!r} expected={expected!r}")


def make_bsc_fixture_line(hr: int, raw_name: str, hd: int, vmag: float) -> str:
    data = list(" " * 180)
    data[0:4] = f"{hr:4d}"
    data[4:14] = f"{raw_name:<10}"[:10]
    data[25:31] = f"{hd:6d}"
    data[75:77] = f"{13:2d}"
    data[77:79] = f"{25:2d}"
    data[79:83] = f"{11.6:4.1f}"
    data[83:84] = "-"
    data[84:86] = f"{11:2d}"
    data[86:88] = f"{9:2d}"
    data[88:90] = f"{41:2d}"
    data[102:107] = f"{vmag:5.2f}"
    data[148:154] = f"{-0.042:6.3f}"
    data[154:160] = f"{-0.032:6.3f}"
    data[161:166] = f"{0.012:5.3f}"
    data[166:170] = f"{1:4d}"
    return "".join(data)


def make_hip_fixture_line(hip: int, hd: int, vmag: float) -> str:
    data = list(" " * 420)
    data[0:6] = f"{hip:6d}"
    data[41:46] = f"{vmag:5.2f}"
    data[51:63] = f"{201.29835230:12.8f}"
    data[64:76] = f"{-11.16131948:12.8f}"
    data[79:86] = f"{12.44:7.2f}"
    data[87:95] = f"{-42.35:8.2f}"
    data[96:104] = f"{-31.73:8.2f}"
    data[274:281] = f"{1.10:7.4f}"
    data[390:396] = f"{hd:6d}"
    return "".join(data)


def test_source_parsers(failures: list[str]) -> None:
    bsc = parse_bsc5_line(make_bsc_fixture_line(5056, "Spica", 116658, 0.98))
    expect_true(bsc is not None, "BSC fixture parses", failures)
    if bsc:
        expect_equal(bsc.hr_id, 5056, "BSC HR", failures)
        expect_equal(bsc.hd_id, 116658, "BSC HD", failures)
        expect_equal(bsc.raw_name, "Spica", "BSC raw name", failures)
        expect_equal(bsc.vmag, 0.98, "BSC Vmag", failures)
        expect_equal(round(bsc.ra_deg or 0.0, 6), round((13 + 25 / 60.0 + 11.6 / 3600.0) * 15.0, 6), "BSC RA", failures)
        expect_equal(round(bsc.dec_deg or 0.0, 6), round(-(11 + 9 / 60.0 + 41 / 3600.0), 6), "BSC Dec", failures)
        expect_equal(bsc.pm_ra_mas_yr, -42.0, "BSC pmRA", failures)
        expect_equal(bsc.pm_dec_mas_yr, -32.0, "BSC pmDE", failures)
        expect_equal(bsc.parallax_mas, 12.0, "BSC parallax", failures)
        expect_equal(bsc.radial_velocity_km_s, 1.0, "BSC radial velocity", failures)

    hip = parse_hipparcos_line(make_hip_fixture_line(65474, 116658, 0.98))
    expect_true(hip is not None, "Hipparcos fixture parses", failures)
    if hip:
        expect_equal(hip.hip_id, 65474, "HIP id", failures)
        expect_equal(hip.hd_id, 116658, "HIP HD", failures)
        expect_equal(hip.vmag, 0.98, "HIP Vmag", failures)
        expect_equal(hip.ra_deg, 201.29835230, "HIP RAdeg", failures)
        expect_equal(hip.dec_deg, -11.16131948, "HIP DEdeg", failures)
        expect_equal(hip.parallax_mas, 12.44, "HIP parallax", failures)
        expect_equal(hip.pm_ra_mas_yr, -42.35, "HIP pmRA", failures)
        expect_equal(hip.pm_dec_mas_yr, -31.73, "HIP pmDE", failures)
        expect_equal(hip.hp_mag, 1.10, "HIP Hpmag", failures)


def test_fixed_seed(failures: list[str]) -> None:
    rows = load_fixed_seed(TOOLS / "star_identity_fixed_seed.csv")
    ids = {row.canonical_id for row in rows}
    for expected in ["spica", "galactic_center_j2000", "sgr_a_apparent"]:
        expect_true(expected in ids, f"fixed seed contains {expected}", failures)
    spica = next(row for row in rows if row.canonical_id == "spica")
    expect_equal(spica.hip_id, 65474, "Spica HIP", failures)
    expect_equal(spica.hr_id, 5056, "Spica HR", failures)
    expect_true(TIER_FIXED in spica.tier_flags, "Spica fixed tier", failures)


def test_manifest_merge(failures: list[str]) -> None:
    builder = ManifestBuilder()
    for identity in load_fixed_seed(TOOLS / "star_identity_fixed_seed.csv"):
        builder.add_or_merge(identity)
    builder.add_or_merge(identity_from_bsc(BSCRecord(hr_id=5056, hd_id=116658, raw_name="Spica", vmag=0.98)))
    builder.add_or_merge(identity_from_hipparcos(HipparcosRecord(hip_id=65474, hd_id=116658, vmag=0.98)))
    rows = builder.sorted_rows()
    spica_rows = [row for row in rows if row.canonical_id == "spica"]
    expect_equal(len(spica_rows), 1, "Spica deduplicated", failures)
    if spica_rows:
        spica = spica_rows[0]
        expect_true(TIER_FIXED in spica.tier_flags, "Spica fixed flag after merge", failures)
        expect_true(TIER_BSC in spica.tier_flags, "Spica BSC flag after merge", failures)
        expect_true(TIER_HIP in spica.tier_flags, "Spica HIP flag after merge", failures)


def test_hd_duplicate_hipparcos_rows_do_not_collapse(failures: list[str]) -> None:
    builder = ManifestBuilder()
    builder.add_or_merge(identity_from_hipparcos(HipparcosRecord(hip_id=1, hd_id=100, vmag=1.0)))
    builder.add_or_merge(identity_from_hipparcos(HipparcosRecord(hip_id=2, hd_id=100, vmag=2.0)))
    rows = builder.sorted_rows()
    hip_ids = {row.hip_id for row in rows}
    expect_equal(len(rows), 2, "HD duplicate Hipparcos rows remain separate", failures)
    expect_equal(hip_ids, {1, 2}, "HD duplicate HIP ids preserved", failures)


def test_write_manifest(failures: list[str]) -> None:
    builder = ManifestBuilder()
    for identity in load_fixed_seed(TOOLS / "star_identity_fixed_seed.csv"):
        builder.add_or_merge(identity)
    rows = filter_by_tier(builder.sorted_rows(), TIER_FIXED)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "manifest.csv"
        count = write_manifest(path, rows)
        expect_equal(count, len(rows), "written row count", failures)
        with path.open("r", encoding="utf-8", newline="") as file:
            reader = csv.DictReader(file)
            expect_equal(reader.fieldnames, FIELDNAMES, "manifest header", failures)
            materialized = list(reader)
            expect_equal(len(materialized), len(rows), "manifest readback count", failures)


def test_cli_build_seed_only(failures: list[str]) -> None:
    class Args:
        tier = "fixed"
        fixed_seed = TOOLS / "star_identity_fixed_seed.csv"
        bsc_catalog = None
        hip_catalog = None

    builder = build_manifest(Args())
    rows = builder.sorted_rows()
    expect_true(any(row.canonical_id == "spica" for row in rows), "build_manifest seed-only Spica", failures)
    expect_true(all(TIER_FIXED in row.tier_flags for row in rows), "seed-only rows are fixed", failures)


def main() -> int:
    failures: list[str] = []
    test_source_parsers(failures)
    test_fixed_seed(failures)
    test_manifest_merge(failures)
    test_hd_duplicate_hipparcos_rows_do_not_collapse(failures)
    test_write_manifest(failures)
    test_cli_build_seed_only(failures)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("test_star_identity_manifest: ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
