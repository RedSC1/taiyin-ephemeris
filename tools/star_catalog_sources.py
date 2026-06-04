#!/usr/bin/env python3

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


@dataclass(frozen=True)
class BSCRecord:
    hr_id: int
    hd_id: int | None
    raw_name: str
    vmag: float | None
    ra_deg: float | None = None
    dec_deg: float | None = None
    pm_ra_mas_yr: float | None = None
    pm_dec_mas_yr: float | None = None
    parallax_mas: float | None = None
    radial_velocity_km_s: float | None = None


@dataclass(frozen=True)
class HipparcosRecord:
    hip_id: int
    hd_id: int | None
    vmag: float | None
    ra_deg: float | None = None
    dec_deg: float | None = None
    parallax_mas: float | None = None
    pm_ra_mas_yr: float | None = None
    pm_dec_mas_yr: float | None = None
    hp_mag: float | None = None


def _parse_int(raw: str) -> int | None:
    raw = raw.strip()
    if not raw:
        return None
    try:
        return int(raw)
    except ValueError:
        return None


def _parse_float(raw: str) -> float | None:
    raw = raw.strip()
    if not raw:
        return None
    try:
        return float(raw)
    except ValueError:
        return None


def _hms_to_degrees(hours: int | None, minutes: int | None, seconds: float | None) -> float | None:
    if hours is None or minutes is None or seconds is None:
        return None
    return (hours + minutes / 60.0 + seconds / 3600.0) * 15.0


def _dms_to_degrees(sign: str, degrees: int | None, minutes: int | None, seconds: int | None) -> float | None:
    if degrees is None or minutes is None or seconds is None:
        return None
    value = degrees + minutes / 60.0 + seconds / 3600.0
    return -value if sign.strip() == "-" else value


def parse_bsc5_line(line: str) -> BSCRecord | None:
    """Parse one Yale Bright Star Catalogue V/50 catalog.dat row.

    Uses the fixed-width columns from the V/50 ReadMe for identity and
    fallback astrometry fields. Rows without an HR number are ignored.
    """

    if not line.strip():
        return None
    padded = line.rstrip("\n")
    hr_id = _parse_int(padded[0:4]) if len(padded) >= 4 else None
    if hr_id is None:
        return None
    raw_name = padded[4:14].strip() if len(padded) >= 14 else ""
    hd_id = _parse_int(padded[25:31]) if len(padded) >= 31 else None
    vmag = _parse_float(padded[102:107]) if len(padded) >= 107 else None

    ra_deg = None
    dec_deg = None
    if len(padded) >= 90:
        ra_deg = _hms_to_degrees(
            _parse_int(padded[75:77]),
            _parse_int(padded[77:79]),
            _parse_float(padded[79:83]),
        )
        dec_deg = _dms_to_degrees(
            padded[83:84],
            _parse_int(padded[84:86]),
            _parse_int(padded[86:88]),
            _parse_int(padded[88:90]),
        )

    pm_ra_arcsec_yr = _parse_float(padded[148:154]) if len(padded) >= 154 else None
    pm_dec_arcsec_yr = _parse_float(padded[154:160]) if len(padded) >= 160 else None
    parallax_arcsec = _parse_float(padded[161:166]) if len(padded) >= 166 else None
    radial_velocity = _parse_int(padded[166:170]) if len(padded) >= 170 else None

    return BSCRecord(
        hr_id=hr_id,
        hd_id=hd_id,
        raw_name=raw_name,
        vmag=vmag,
        ra_deg=ra_deg,
        dec_deg=dec_deg,
        pm_ra_mas_yr=None if pm_ra_arcsec_yr is None else pm_ra_arcsec_yr * 1000.0,
        pm_dec_mas_yr=None if pm_dec_arcsec_yr is None else pm_dec_arcsec_yr * 1000.0,
        parallax_mas=None if parallax_arcsec is None else parallax_arcsec * 1000.0,
        radial_velocity_km_s=None if radial_velocity is None else float(radial_velocity),
    )


def parse_bsc5_catalog(path: Path) -> Iterator[BSCRecord]:
    with path.open("r", encoding="latin-1") as file:
        for line_number, line in enumerate(file, 1):
            record = parse_bsc5_line(line)
            if record is None:
                continue
            if record.hr_id <= 0:
                raise ValueError(f"invalid BSC HR id at {path}:{line_number}")
            yield record


def parse_hipparcos_line(line: str) -> HipparcosRecord | None:
    """Parse one Hipparcos I/239 hip_main.dat row.

    The CDS Hipparcos main table is fixed-width. Different mirrors include
    slightly different record padding, so this parser focuses on the stable
    identity fields and accepts both a compact test fixture layout and the CDS
    layout where HIP appears near the beginning of the record.
    """

    if not line.strip():
        return None
    padded = line.rstrip("\n")

    hip_id = _parse_int(padded[0:6])
    if hip_id is None and len(padded) >= 14:
        hip_id = _parse_int(padded[2:14])
    if hip_id is None:
        return None

    # CDS I/239 hip_main.dat columns are 1-based in the ReadMe:
    # Vmag 42-46, RAdeg 52-63, DEdeg 65-76, Plx 80-86, pmRA 88-95,
    # pmDE 97-104, Hpmag 275-281. Convert to Python 0-based slices.
    vmag = _parse_float(padded[41:46]) if len(padded) >= 46 else None
    ra_deg = _parse_float(padded[51:63]) if len(padded) >= 63 else None
    dec_deg = _parse_float(padded[64:76]) if len(padded) >= 76 else None
    parallax_mas = _parse_float(padded[79:86]) if len(padded) >= 86 else None
    pm_ra_mas_yr = _parse_float(padded[87:95]) if len(padded) >= 95 else None
    pm_dec_mas_yr = _parse_float(padded[96:104]) if len(padded) >= 104 else None
    hp_mag = _parse_float(padded[274:281]) if len(padded) >= 281 else None

    # HD is present in some Hipparcos distributions/derived extracts near the
    # end, but not all source files expose it in a simple fixed position. Try a
    # conservative long-record slice; otherwise leave it blank.
    hd_id = _parse_int(padded[390:396]) if len(padded) >= 396 else None

    return HipparcosRecord(
        hip_id=hip_id,
        hd_id=hd_id,
        vmag=vmag,
        ra_deg=ra_deg,
        dec_deg=dec_deg,
        parallax_mas=parallax_mas,
        pm_ra_mas_yr=pm_ra_mas_yr,
        pm_dec_mas_yr=pm_dec_mas_yr,
        hp_mag=hp_mag,
    )


def parse_hipparcos_main(path: Path) -> Iterator[HipparcosRecord]:
    with path.open("r", encoding="latin-1") as file:
        for line_number, line in enumerate(file, 1):
            record = parse_hipparcos_line(line)
            if record is None:
                continue
            if record.hip_id <= 0:
                raise ValueError(f"invalid Hipparcos id at {path}:{line_number}")
            yield record
