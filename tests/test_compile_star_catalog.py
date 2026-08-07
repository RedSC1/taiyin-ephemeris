#!/usr/bin/env python3

from __future__ import annotations

import csv
import math
import struct
import tempfile
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
sys.path.insert(0, str(TOOLS))

from compile_star_catalog import (  # noqa: E402
    ALIAS_ENTRY_STRUCT,
    FLAG_HAS_GAIA_ID,
    FLAG_HAS_HIP_ID,
    FLAG_HAS_HR_ID,
    FLAG_SPECIAL_DIRECTION,
    HEADER_STRUCT,
    MAGIC,
    SOURCE_GAIA_DR3,
    SOURCE_MANUAL,
    STAR_RECORD_STRUCT,
    TIER_BSC,
    TIER_FIXED,
    TIER_HIP,
    VERSION,
    build_catalog,
    fnv1a_64,
    inspect_tsc1,
    read_astrometry,
    read_identity_manifest,
    write_tsc1,
)
from generate_star_identity_manifest import FIELDNAMES as IDENTITY_FIELDNAMES  # noqa: E402
from fetch_gaia_star_astrometry import ASTROMETRY_FIELDNAMES  # noqa: E402


def expect_true(value: bool, label: str, failures: list[str]) -> None:
    if not value:
        failures.append(f"expected true: {label}")


def expect_equal(actual, expected, label: str, failures: list[str]) -> None:
    if actual != expected:
        failures.append(f"{label}: actual={actual!r} expected={expected!r}")


def write_fixture_csvs(directory: Path) -> tuple[Path, Path]:
    identity_path = directory / "identity.csv"
    astrometry_path = directory / "astrometry.csv"
    identity_rows = [
        {
            "tier_flags": "fixed_traditional|bright_bsc|hipparcos",
            "canonical_id": "spica",
            "display_name": "Spica",
            "aliases": "alpha_vir,vir_alpha",
            "hip_id": "65474",
            "hr_id": "5056",
            "hd_id": "116658",
            "gaia_dr3_source_id": "6193600049863267200",
            "simbad_id": "* alf Vir",
            "bayer": "alpha Vir",
            "flamsteed": "",
            "constellation": "Vir",
            "vmag": "0.98",
            "priority": "100",
            "notes": "fixture",
        },
        {
            "tier_flags": "bright_bsc|hipparcos",
            "canonical_id": "hr_1",
            "display_name": "HR 1",
            "aliases": "hd_3",
            "hip_id": "",
            "hr_id": "1",
            "hd_id": "3",
            "gaia_dr3_source_id": "",
            "simbad_id": "",
            "bayer": "",
            "flamsteed": "",
            "constellation": "",
            "vmag": "6.7",
            "priority": "20",
            "notes": "fixture",
        },
        {
            "tier_flags": "hipparcos",
            "canonical_id": "hip_2",
            "display_name": "HIP 2",
            "aliases": "",
            "hip_id": "2",
            "hr_id": "",
            "hd_id": "",
            "gaia_dr3_source_id": "",
            "simbad_id": "",
            "bayer": "",
            "flamsteed": "",
            "constellation": "",
            "vmag": "7.7",
            "priority": "10",
            "notes": "fixture",
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
            "notes": "manual special",
        },
    ]
    astrometry_rows = [
        {
            "canonical_id": "spica",
            "tier_flags": "fixed_traditional|bright_bsc|hipparcos",
            "display_name": "Spica",
            "gaia_dr3_source_id": "6193600049863267200",
            "hip_id": "65474",
            "hr_id": "5056",
            "hd_id": "116658",
            "ra_deg": "201.298247375",
            "dec_deg": "-11.161319472",
            "pm_ra_mas_yr": "-42.35",
            "pm_dec_mas_yr": "-31.73",
            "parallax_mas": "12.44",
            "radial_velocity_km_s": "1.0",
            "phot_g_mean_mag": "1.2",
            "vmag": "0.98",
            "reference_epoch": "2016.0",
            "astrometry_source": "Gaia DR3",
            "match_method": "fixture",
            "match_quality": "direct",
            "angular_distance_arcsec": "",
            "xm_flag": "",
            "notes": "fixture",
        },
        {
            "canonical_id": "hr_1",
            "tier_flags": "bright_bsc|hipparcos",
            "display_name": "HR 1",
            "gaia_dr3_source_id": "",
            "hip_id": "",
            "hr_id": "1",
            "hd_id": "3",
            "ra_deg": "10.0",
            "dec_deg": "20.0",
            "pm_ra_mas_yr": "1.0",
            "pm_dec_mas_yr": "2.0",
            "parallax_mas": "3.0",
            "radial_velocity_km_s": "4.0",
            "phot_g_mean_mag": "",
            "vmag": "6.7",
            "reference_epoch": "2000.0",
            "astrometry_source": "BSC5",
            "match_method": "fixture",
            "match_quality": "fallback",
            "angular_distance_arcsec": "",
            "xm_flag": "",
            "notes": "fixture",
        },
        {
            "canonical_id": "hip_2",
            "tier_flags": "hipparcos",
            "display_name": "HIP 2",
            "gaia_dr3_source_id": "",
            "hip_id": "2",
            "hr_id": "",
            "hd_id": "",
            "ra_deg": "30.0",
            "dec_deg": "40.0",
            "pm_ra_mas_yr": "5.0",
            "pm_dec_mas_yr": "6.0",
            "parallax_mas": "7.0",
            "radial_velocity_km_s": "",
            "phot_g_mean_mag": "",
            "vmag": "7.7",
            "reference_epoch": "1991.25",
            "astrometry_source": "Hipparcos",
            "match_method": "fixture",
            "match_quality": "fallback",
            "angular_distance_arcsec": "",
            "xm_flag": "",
            "notes": "fixture",
        },
    ]
    with identity_path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=IDENTITY_FIELDNAMES)
        writer.writeheader()
        writer.writerows(identity_rows)
    with astrometry_path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=ASTROMETRY_FIELDNAMES)
        writer.writeheader()
        writer.writerows(astrometry_rows)
    return identity_path, astrometry_path


def read_c_string(data: bytes, offset: int) -> str:
    end = data.index(b"\0", offset)
    return data[offset:end].decode("utf-8")


def test_compile_and_inspect(failures: list[str]) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        identity_path, astrometry_path = write_fixture_csvs(directory)
        identities = read_identity_manifest(identity_path)
        astrometry = read_astrometry(astrometry_path)

        fixed = build_catalog(identities, astrometry, TIER_FIXED)
        bright = build_catalog(identities, astrometry, TIER_BSC)
        hip = build_catalog(identities, astrometry, TIER_HIP)
        expect_equal(len(fixed), 2, "fixed cumulative count", failures)
        expect_equal(len(bright), 3, "bright cumulative count", failures)
        expect_equal(len(hip), 4, "hipparcos cumulative count", failures)

        output = directory / "fixture.tsc1"
        compiled = write_tsc1(output, hip)
        expect_true(output.exists(), "TSC1 output exists", failures)
        expect_true(compiled.alias_count >= 10, "alias count includes generated aliases", failures)

        info = inspect_tsc1(output, verbose=False)
        expect_equal(info["version"], VERSION, "inspect version", failures)
        expect_equal(info["star_count"], 4, "inspect star count", failures)
        expect_equal(info["source_counts"].get("Gaia DR3"), 1, "Gaia source count", failures)
        expect_equal(info["source_counts"].get("BSC5"), 1, "BSC source count", failures)
        expect_equal(info["source_counts"].get("Hipparcos"), 1, "Hipparcos source count", failures)
        expect_equal(info["source_counts"].get("Manual"), 1, "Manual source count", failures)

        data = output.read_bytes()
        header = HEADER_STRUCT.unpack_from(data, 0)
        expect_equal(header[0], MAGIC, "header magic", failures)
        expect_equal(header[1], VERSION, "header version", failures)
        expect_equal(header[3], 4, "header star count", failures)
        star_offset = header[5]
        alias_offset = header[6]
        string_offset = header[7]
        string_size = header[8]
        expect_equal(star_offset, HEADER_STRUCT.size, "star offset", failures)
        expect_equal(alias_offset, star_offset + 4 * STAR_RECORD_STRUCT.size, "alias offset", failures)
        expect_true(string_offset > alias_offset, "string offset after aliases", failures)
        expect_true(string_size > 0, "string table non-empty", failures)

        strings = data[string_offset : string_offset + string_size]
        first_record = STAR_RECORD_STRUCT.unpack_from(data, star_offset)
        first_id = read_c_string(strings, first_record[0])
        expect_equal(first_id, "spica", "first record sorted by priority", failures)
        expect_equal(first_record[2], 6193600049863267200, "Gaia source ID", failures)
        expect_equal(first_record[3], 65474, "HIP ID", failures)
        expect_equal(first_record[4], 5056, "HR ID", failures)
        expect_equal(first_record[14], SOURCE_GAIA_DR3, "source code", failures)
        expect_true(bool(first_record[15] & FLAG_HAS_GAIA_ID), "Gaia flag", failures)
        expect_true(bool(first_record[15] & FLAG_HAS_HIP_ID), "HIP flag", failures)
        expect_true(bool(first_record[15] & FLAG_HAS_HR_ID), "HR flag", failures)

        aliases = []
        for index in range(header[4]):
            alias_record = ALIAS_ENTRY_STRUCT.unpack_from(data, alias_offset + index * ALIAS_ENTRY_STRUCT.size)
            aliases.append((read_c_string(strings, alias_record[0]), alias_record[1], alias_record[2]))
        alias_names = {alias for alias, _star, _hash in aliases}
        expect_true("spica" in alias_names, "canonical alias present", failures)
        expect_true("hip_65474" in alias_names, "HIP alias present", failures)
        expect_true("hr_5056" in alias_names, "HR alias present", failures)
        expect_true("galactic_center" in alias_names, "manual alias present", failures)
        spica_alias = next(item for item in aliases if item[0] == "spica")
        expect_equal(spica_alias[2], fnv1a_64("spica"), "alias hash", failures)

        manual_record = None
        for index in range(header[3]):
            record = STAR_RECORD_STRUCT.unpack_from(data, star_offset + index * STAR_RECORD_STRUCT.size)
            if read_c_string(strings, record[0]) == "galactic_center_j2000":
                manual_record = record
                break
        expect_true(manual_record is not None, "manual record exists", failures)
        if manual_record:
            expect_equal(manual_record[14], SOURCE_MANUAL, "manual source code", failures)
            expect_true(bool(manual_record[15] & FLAG_SPECIAL_DIRECTION), "manual special flag", failures)
            expect_true(math.isclose(manual_record[6], 266.4168371), "manual RA", failures)


def main() -> int:
    failures: list[str] = []
    test_compile_and_inspect(failures)
    if failures:
        for failure in failures:
            print("FAIL:", failure)
        return 1
    print("test_compile_star_catalog: ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
