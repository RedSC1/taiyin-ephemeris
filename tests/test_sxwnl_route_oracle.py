#!/usr/bin/env python3

import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


MAX_POINT_ERROR_DEG = 0.75


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def angular_distance_deg(a, b):
    lon_a, lat_a = a
    lon_b, lat_b = b
    delta_lon = (lon_a - lon_b + math.pi) % (2.0 * math.pi) - math.pi
    haversine = (
        math.sin(0.5 * (lat_a - lat_b)) ** 2
        + math.cos(lat_a) * math.cos(lat_b) * math.sin(0.5 * delta_lon) ** 2
    )
    return math.degrees(2.0 * math.asin(min(1.0, math.sqrt(max(0.0, haversine)))))


def sxwnl_points(points):
    return [(point["longitude_rad"], point["latitude_rad"]) for point in points]


def taiyin_points(points):
    return [
        (math.radians(point["longitude_deg"]), math.radians(point["latitude_deg"]))
        for point in points
    ]


def directed_nearest_error(points, reference):
    return max(
        min(angular_distance_deg(point, candidate) for candidate in reference)
        for point in points
    )


def densify_spherical_curve(points, subdivisions=8):
    if len(points) < 2:
        return list(points)
    dense = []
    for index in range(len(points) - 1):
        lon_a, lat_a = points[index]
        lon_b, lat_b = points[index + 1]
        a = (
            math.cos(lat_a) * math.cos(lon_a),
            math.cos(lat_a) * math.sin(lon_a),
            math.sin(lat_a),
        )
        b = (
            math.cos(lat_b) * math.cos(lon_b),
            math.cos(lat_b) * math.sin(lon_b),
            math.sin(lat_b),
        )
        dot = max(-1.0, min(1.0, sum(x * y for x, y in zip(a, b))))
        angle = math.acos(dot)
        for subdivision in range(subdivisions):
            fraction = subdivision / subdivisions
            if angle < 1.0e-12:
                vector = a
            else:
                denominator = math.sin(angle)
                weight_a = math.sin((1.0 - fraction) * angle) / denominator
                weight_b = math.sin(fraction * angle) / denominator
                vector = tuple(weight_a * x + weight_b * y for x, y in zip(a, b))
            longitude = math.atan2(vector[1], vector[0])
            latitude = math.atan2(vector[2], math.hypot(vector[0], vector[1]))
            dense.append((longitude, latitude))
    dense.append(points[-1])
    return dense


def compare_curve(label, expected, actual, allow_count_delta=0):
    require(
        abs(len(expected) - len(actual)) <= allow_count_delta,
        f"{label}: point count {len(actual)} != sxwnl {len(expected)}",
    )
    if not expected or not actual:
        require(not expected and not actual, f"{label}: only one implementation returned points")
        return 0.0

    if len(expected) == len(actual):
        maximum_error = max(
            angular_distance_deg(expected_point, actual_point)
            for expected_point, actual_point in zip(expected, actual)
        )
    else:
        expected_dense = densify_spherical_curve(expected)
        actual_dense = densify_spherical_curve(actual)
        maximum_error = max(
            directed_nearest_error(expected, actual_dense),
            directed_nearest_error(actual, expected_dense),
            angular_distance_deg(expected[0], actual[0]),
            angular_distance_deg(expected[-1], actual[-1]),
        )
    require(
        maximum_error <= MAX_POINT_ERROR_DEG,
        f"{label}: maximum point error {maximum_error:.6f} deg exceeds "
        f"{MAX_POINT_ERROR_DEG:.2f} deg",
    )
    return maximum_error


def verify_fixture(generator_path, fixture_path):
    source_root = os.environ.get("TAIYIN_SXWNL_SOURCE_ROOT")
    if not source_root:
        return
    node = shutil.which("node")
    require(node is not None, "TAIYIN_SXWNL_SOURCE_ROOT is set but node is unavailable")
    with tempfile.TemporaryDirectory() as directory:
        regenerated_path = Path(directory) / "sxwnl-route.json"
        subprocess.run(
            [node, str(generator_path), source_root, str(regenerated_path)],
            check=True,
        )
        with regenerated_path.open("r", encoding="utf-8") as regenerated_file:
            regenerated = json.load(regenerated_file)
        with fixture_path.open("r", encoding="utf-8") as fixture_file:
            fixture = json.load(fixture_file)
        require(regenerated == fixture, "committed sxwnl route fixture is stale")


def main():
    require(len(sys.argv) == 5, "expected exporter, data root, fixture, and generator paths")
    exporter = Path(sys.argv[1])
    data_root = Path(sys.argv[2])
    fixture_path = Path(sys.argv[3])
    generator_path = Path(sys.argv[4])
    verify_fixture(generator_path, fixture_path)

    with fixture_path.open("r", encoding="utf-8") as fixture_file:
        fixture = json.load(fixture_file)
    require(fixture["event"]["calendar_date"] == "2026-02-17", "unexpected oracle date")
    require(fixture["event"]["sample_count"] == 400, "unexpected oracle sample count")

    completed = subprocess.run(
        [
            str(exporter),
            "--data-root",
            str(data_root),
            "--start",
            "2026-02-01",
            "--end",
            "2026-03-01",
            "--route-samples",
            "400",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(completed.stderr == "", "Taiyin route exporter wrote to stderr")
    document = json.loads(completed.stdout)
    require(len(document["events"]) == 1, "expected one February 2026 eclipse")
    event = document["events"][0]
    require(event["event_id"] == "solar-2026-02-17", "unexpected Taiyin event")

    # The 2026 event is annular, so signed Besselian l2 swaps sxwnl L3/L4
    # into geographic south/north core limits respectively. Taiyin may add up
    # to 32 spatially adaptive points around each high-latitude transition.
    mappings = [
        ("p1", "partial_begin_a", 0),
        ("p2", "partial_begin_b", 0),
        ("p3", "partial_end_a", 0),
        ("p4", "partial_end_b", 0),
        ("q1", "sunrise_max_a", 0),
        ("q2", "sunrise_max_b", 0),
        ("q3", "sunset_max_a", 0),
        ("q4", "sunset_max_b", 0),
        ("L0", "center_line", 32),
        ("L1", "penumbral_north", 0),
        ("L2", "penumbral_south", 0),
        ("L3", "core_south", 32),
        ("L4", "core_north", 32),
        ("L5", "half_magnitude_north", 0),
        ("L6", "half_magnitude_south", 0),
    ]
    errors = []
    for sxwnl_name, taiyin_name, count_delta in mappings:
        expected = sxwnl_points(fixture["curves"][sxwnl_name])
        actual = taiyin_points(event["curves"][taiyin_name])
        error = compare_curve(sxwnl_name, expected, actual, count_delta)
        errors.append((sxwnl_name, len(expected), len(actual), error))

    for name, expected_count, actual_count, error in errors:
        print(
            f"{name:>2}: sxwnl={expected_count:3d} taiyin={actual_count:3d} "
            f"max_error={error:.6f} deg"
        )


if __name__ == "__main__":
    main()
