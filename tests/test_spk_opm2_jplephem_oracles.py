#!/usr/bin/env python3

import math
import os
import subprocess
import sys
from collections import deque
from pathlib import Path

AU_KM = 149597870.700
JD_2024 = 2460310.5
JD_SATELLITE = 2460310.5


def skip(message):
    print(f"SKIP: {message}")
    return 0


try:
    from jplephem.spk import SPK
except Exception as exc:  # pragma: no cover - depends on local environment
    sys.exit(skip(f"jplephem is not installed: {exc}"))


class SegmentEdge:
    def __init__(self, kernel, segment):
        self.kernel = kernel
        self.segment = segment
        self.center = int(segment.center)
        self.target = int(segment.target)
        self.start_jd = float(segment.start_jd)
        self.end_jd = float(segment.end_jd)

    def covers(self, jd):
        return self.start_jd <= jd < self.end_jd

    def state_au(self, jd, direction):
        position_km, velocity_km_per_day = self.segment.compute_and_differentiate(jd)
        position = [direction * float(value) / AU_KM for value in position_km]
        velocity = [direction * float(value) / AU_KM for value in velocity_km_per_day]
        return position, velocity


def build_spk_edges(paths):
    edges = []
    kernels = []
    for path in paths:
        kernel = SPK.open(str(path))
        kernels.append(kernel)
        for segment in kernel.segments:
            edges.append(SegmentEdge(kernel, segment))
    return kernels, edges


def jplephem_state(paths, target_id, center_id, jd):
    kernels, edges = build_spk_edges(paths)
    try:
        if target_id == center_id:
            return [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]

        graph = {}
        for edge in edges:
            if not edge.covers(jd):
                continue
            graph.setdefault(edge.center, []).append((edge.target, edge, 1.0))
            graph.setdefault(edge.target, []).append((edge.center, edge, -1.0))

        queue = deque([(center_id, [])])
        visited = {center_id}
        selected_path = None
        while queue:
            node, path = queue.popleft()
            if node == target_id:
                selected_path = path
                break
            for next_node, edge, direction in graph.get(node, []):
                if next_node in visited:
                    continue
                visited.add(next_node)
                queue.append((next_node, path + [(edge, direction)]))

        if selected_path is None:
            raise AssertionError(f"no jplephem path for {target_id}/{center_id} at JD {jd}")

        position = [0.0, 0.0, 0.0]
        velocity = [0.0, 0.0, 0.0]
        for edge, direction in selected_path:
            step_position, step_velocity = edge.state_au(jd, direction)
            for i in range(3):
                position[i] += step_position[i]
                velocity[i] += step_velocity[i]
        return position, velocity
    finally:
        for kernel in kernels:
            kernel.close()


def run_taiyin(driver, source_paths, target_id, center_id, jd):
    source_arg = os.pathsep.join(str(path) for path in source_paths)
    completed = subprocess.run(
        [str(driver), source_arg, str(target_id), str(center_id), repr(float(jd))],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    parsed = {}
    for line in completed.stdout.splitlines():
        parts = line.split()
        if parts:
            parsed[parts[0]] = parts[1:]
    return {
        "position": [float(value) for value in parsed["position"]],
        "velocity": [float(value) for value in parsed["velocity"]],
        "descriptor": [int(value) for value in parsed["descriptor"]],
    }


def max_abs_delta(lhs, rhs):
    return max(abs(a - b) for a, b in zip(lhs, rhs))


def norm(values):
    return math.sqrt(sum(value * value for value in values))


def angular_delta_arcsec(lhs, rhs):
    lhs_norm = norm(lhs)
    rhs_norm = norm(rhs)
    if lhs_norm == 0.0 or rhs_norm == 0.0:
        return 0.0
    cross = [
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0],
    ]
    dot = sum(a * b for a, b in zip(lhs, rhs))
    return math.atan2(norm(cross), dot) * 206264.80624709636


def require_close(label, actual, expected, tolerance):
    delta = max_abs_delta(actual, expected)
    if not delta <= tolerance:
        raise AssertionError(
            f"{label}: max delta {delta:.17g} > tolerance {tolerance:.17g}\n"
            f"  actual   {actual}\n"
            f"  expected {expected}"
        )


def existing(paths):
    return all(Path(path).exists() for path in paths)


def run_case(driver, case):
    source_paths = case["source_paths"]
    oracle_paths = case["oracle_paths"]
    if not existing(source_paths + oracle_paths):
        print(f"SKIP case {case['label']}: missing local source/oracle file")
        return

    reference_position, reference_velocity = jplephem_state(
        oracle_paths,
        case["target"],
        case["center"],
        case["jd"],
    )
    actual = run_taiyin(
        driver,
        source_paths,
        case["target"],
        case["center"],
        case["jd"],
    )
    position_delta = [a - b for a, b in zip(actual["position"], reference_position)]
    velocity_delta = [a - b for a, b in zip(actual["velocity"], reference_velocity)]
    require_close(
        case["label"] + " position",
        actual["position"],
        reference_position,
        case["position_tolerance_au"],
    )
    require_close(
        case["label"] + " velocity",
        actual["velocity"],
        reference_velocity,
        case["velocity_tolerance_au_per_day"],
    )
    if "descriptor" in case and actual["descriptor"][:4] != case["descriptor"]:
        raise AssertionError(
            f"{case['label']} descriptor {actual['descriptor'][:4]} != {case['descriptor']}"
        )
    print(
        "PASS "
        f"{case['label']}: "
        f"pos_max={max_abs_delta(actual['position'], reference_position):.6e} AU "
        f"pos_norm={norm(position_delta) * AU_KM:.3f} km "
        f"ang={angular_delta_arcsec(actual['position'], reference_position):.6f} arcsec "
        f"vel_max={max_abs_delta(actual['velocity'], reference_velocity):.6e} AU/day "
        f"tol=({case['position_tolerance_au']:.6g}, {case['velocity_tolerance_au_per_day']:.6g})"
    )


def main():
    if len(sys.argv) != 2:
        print("usage: test_spk_opm2_jplephem_oracles.py <oracle_state_driver>", file=sys.stderr)
        return 2

    driver = Path(sys.argv[1])
    if not driver.exists():
        print(f"missing oracle driver: {driver}", file=sys.stderr)
        return 2

    nasa_root_env = os.environ.get("TAIYIN_NASA_BSP_ROOT")
    if not nasa_root_env:
        return skip("set TAIYIN_NASA_BSP_ROOT to run jplephem SPK/OPM2 oracle tests")

    repo_root = Path(__file__).resolve().parents[1]
    nasa_root = Path(nasa_root_env)
    de441 = nasa_root / "planetary" / "de441.bsp"
    jupiter_satellites = nasa_root / "satellites" / "jup365.bsp"
    asteroid_group = nasa_root / "asteroids" / "sb441-n16.bsp"

    opm2_major = repo_root / "data" / "ephemerides" / "opm2" / "major-bodies" / "600y"
    opm2_asteroids = repo_root / "data" / "ephemerides" / "opm2" / "asteroids" / "600y"

    spk_tight = {
        "position_tolerance_au": 2.0e-12,
        "velocity_tolerance_au_per_day": 2.0e-13,
    }
    opm2_tight_tol = {
        "position_tolerance_au": 1.0e-8,
        "velocity_tolerance_au_per_day": 1.0e-8,
    }
    opm2_asteroid_tol = {
        "position_tolerance_au": 5.0e-8,
        "velocity_tolerance_au_per_day": 1.0e-8,
    }

    cases = []
    for label, target, center in [
        ("SPK DE441 Sun/SSB", 10, 0),
        ("SPK DE441 Mercury barycenter/Sun", 1, 10),
        ("SPK DE441 Venus barycenter/Sun", 2, 10),
        ("SPK DE441 EMB/Sun", 3, 10),
        ("SPK DE441 Moon/Earth", 301, 399),
        ("SPK DE441 Mars barycenter/Sun", 4, 10),
        ("SPK DE441 Pluto barycenter/Sun", 9, 10),
    ]:
        cases.append({
            "label": label,
            "source_paths": [de441],
            "oracle_paths": [de441],
            "target": target,
            "center": center,
            "jd": JD_2024,
            **spk_tight,
        })

    cases.extend([
        {
            "label": "SPK Jupiter body/SSB from satellite COB path",
            "source_paths": [jupiter_satellites],
            "oracle_paths": [jupiter_satellites],
            "target": 599,
            "center": 0,
            "jd": JD_SATELLITE,
            **spk_tight,
        },
        {
            "label": "SPK unknown numeric satellite 505/Jupiter body",
            "source_paths": [jupiter_satellites],
            "oracle_paths": [jupiter_satellites],
            "target": 505,
            "center": 599,
            "jd": JD_SATELLITE,
            **spk_tight,
        },
    ])

    for label, target in [
        ("SPK Ceres asteroid/Sun", 2000001),
        ("SPK Pallas asteroid/Sun", 2000002),
        ("SPK Juno asteroid/Sun", 2000003),
        ("SPK Vesta asteroid/Sun", 2000004),
    ]:
        cases.append({
            "label": label,
            "source_paths": [asteroid_group],
            "oracle_paths": [asteroid_group],
            "target": target,
            "center": 10,
            "jd": JD_2024,
            **spk_tight,
        })

    for label, target, center in [
        ("OPM2 Sun/SSB", 10, 0),
        ("OPM2 Moon/Earth", 301, 399),
    ]:
        cases.append({
            "label": label,
            "source_paths": [opm2_major],
            "oracle_paths": [de441],
            "target": target,
            "center": center,
            "jd": JD_2024,
            **opm2_tight_tol,
        })

    for label, target in [
        ("OPM2 Mercury barycenter/Sun", 1),
        ("OPM2 Venus barycenter/Sun", 2),
        ("OPM2 EMB/Sun", 3),
        ("OPM2 Mars barycenter/Sun", 4),
    ]:
        cases.append({
            "label": label,
            "source_paths": [opm2_major],
            "oracle_paths": [de441],
            "target": target,
            "center": 10,
            "jd": JD_2024,
            **opm2_tight_tol,
        })

    # The local single-object Chiron/Pholus/Nessus/Lilith BSP files are SPK
    # type 21, which jplephem cannot evaluate. Use the type 2 asteroid group
    # kernel for jplephem-backed OPM2 asteroid oracles.
    for label, target in [
        ("OPM2 Ceres asteroid/Sun", 2000001),
        ("OPM2 Vesta asteroid/Sun", 2000004),
    ]:
        cases.append({
            "label": label,
            "source_paths": [opm2_asteroids],
            "oracle_paths": [asteroid_group],
            "target": target,
            "center": 10,
            "jd": JD_2024,
            **opm2_asteroid_tol,
        })

    for case in cases:
        run_case(driver, case)
    return 0


if __name__ == "__main__":
    sys.exit(main())
