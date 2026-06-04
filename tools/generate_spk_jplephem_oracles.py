#!/usr/bin/env python3
"""Generate C++ SPK oracle rows from BSP files using jplephem.

This tool is intentionally not part of the runtime test path.  Run it when
refreshing the baked values in tests/test_spk_jplephem_oracles.cpp.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import argparse
import os
import sys

AU_KM = 149_597_870.700
DEFAULT_DATA_ROOT = Path(os.environ.get("TAIYIN_NASA_BSP_ROOT", ""))


@dataclass(frozen=True)
class OracleCase:
    label: str
    subpath: str
    path_symbol: str
    target_id: int
    center_id: int
    jd_tdb: float
    common_center_id: int | None = None


CASES = [
    OracleCase("de441-mars-barycenter-sun", "planetary/de441.bsp", "kDe441Path", 4, 10, 2451545.0, 0),
    OracleCase("main-belt-ceres-sun", "asteroids/sb441-n16.bsp", "kMainBeltAsteroidsPath", 2000001, 10, 2451545.0),
    OracleCase("near-earth-eros-sun", "asteroids/sb441-n373s.bsp", "kNearEarthAsteroidsPath", 2000433, 10, 2451545.0),
    OracleCase("jupiter-cob-barycenter", "satellites/jup365.bsp", "kJupiterSatellitesPath", 599, 5, 2451545.0),
    OracleCase("jupiter-io-cob", "satellites/jup365.bsp", "kJupiterSatellitesPath", 501, 599, 2451545.0, 5),
    OracleCase("saturn-cob-barycenter", "satellites/sat441.bsp", "kSaturnSatellitesPath", 699, 6, 2451545.0),
    OracleCase("saturn-mimas-cob", "satellites/sat441.bsp", "kSaturnSatellitesPath", 601, 699, 2451545.0, 6),
]


def import_jplephem():
    try:
        from jplephem.spk import SPK  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "jplephem is required to regenerate SPK oracle rows. "
            "Install it with: python3 -m pip install jplephem"
        ) from exc
    return SPK


def find_segment(kernel, center_id: int, target_id: int, jd_tdb: float):
    for segment in kernel.segments:
        if segment.center == center_id and segment.target == target_id and segment.start_jd <= jd_tdb <= segment.end_jd:
            return segment
    raise KeyError(f"no segment for center={center_id} target={target_id} jd={jd_tdb}")


def compute_direct(kernel, center_id: int, target_id: int, jd_tdb: float):
    segment = find_segment(kernel, center_id, target_id, jd_tdb)
    position_km, velocity_km_per_day = segment.compute_and_differentiate(jd_tdb)
    position_au = [float(value) / AU_KM for value in position_km]
    velocity_au_per_day = [float(value) / AU_KM for value in velocity_km_per_day]
    return position_au, velocity_au_per_day


def compute_case(kernel, case: OracleCase):
    if case.common_center_id is None:
        return compute_direct(kernel, case.center_id, case.target_id, case.jd_tdb)

    target_position, target_velocity = compute_direct(kernel, case.common_center_id, case.target_id, case.jd_tdb)
    center_position, center_velocity = compute_direct(kernel, case.common_center_id, case.center_id, case.jd_tdb)
    position = [target_position[i] - center_position[i] for i in range(3)]
    velocity = [target_velocity[i] - center_velocity[i] for i in range(3)]
    return position, velocity


def fmt_double(value: float) -> str:
    return f"{value:.17e}"


def emit_case(case: OracleCase, position, velocity) -> None:
    common = "direct" if case.common_center_id is None else f"derived via common center {case.common_center_id}"
    print(f"    // {case.label}: jplephem {common}")
    print("    {")
    print(f"        \"{case.label}\",")
    print(f"        {case.path_symbol},")
    print(f"        {case.target_id}, {case.center_id}, {fmt_double(case.jd_tdb)},")
    print(
        "        { "
        + ", ".join(fmt_double(value) for value in position)
        + " },"
    )
    print(
        "        { "
        + ", ".join(fmt_double(value) for value in velocity)
        + " },"
    )
    print("        2.0e-13, 2.0e-13")
    print("    },")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data-root", type=Path, default=DEFAULT_DATA_ROOT)
    parser.add_argument("--strict", action="store_true", help="fail if any BSP file is absent")
    args = parser.parse_args()

    SPK = import_jplephem()

    print("// Baked with jplephem from local NASA BSP files.")
    print("// Re-run tools/generate_spk_jplephem_oracles.py to refresh.")
    for case in CASES:
        path = args.data_root / case.subpath
        if not path.exists():
            message = f"missing {path}"
            if args.strict:
                print(message, file=sys.stderr)
                return 1
            print(f"// skip {case.label}: {message}")
            continue

        kernel = SPK.open(str(path))
        try:
            position, velocity = compute_case(kernel, case)
        finally:
            kernel.close()
        emit_case(case, position, velocity)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
