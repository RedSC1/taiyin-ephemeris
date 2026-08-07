#!/usr/bin/env python3

import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def reject_non_finite(value, path="root"):
    if isinstance(value, float):
        require(math.isfinite(value), f"non-finite value at {path}")
    elif isinstance(value, list):
        for index, item in enumerate(value):
            reject_non_finite(item, f"{path}[{index}]")
    elif isinstance(value, dict):
        for key, item in value.items():
            reject_non_finite(item, f"{path}.{key}")


def curve_count(event):
    return sum(len(points) for points in event["curves"].values())


def polygon_count(event):
    return sum(len(points) for points in event["polygons"].values())


def point_list_differs(a, b, tolerance=1.0e-10):
    if len(a) != len(b):
        return True
    for left, right in zip(a, b):
        if (abs(left["latitude_deg"] - right["latitude_deg"]) > tolerance
                or abs(left["longitude_deg"] - right["longitude_deg"]) > tolerance):
            return True
    return False


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_eclipse_forecast_json.py EXPORT_BINARY DATA_ROOT")

    invalid_date = subprocess.run(
        [
            sys.argv[1],
            "--start",
            "1582-10-10",
            "--end",
            "1582-11-01",
        ],
        capture_output=True,
        text=True,
    )
    require(invalid_date.returncode == 2, "Gregorian reform gap should be rejected")

    with tempfile.TemporaryDirectory() as directory:
        output_path = Path(directory) / "empty-forecast.json"
        file_output = subprocess.run(
            [
                sys.argv[1],
                "--data-root",
                sys.argv[2],
                "--start",
                "2025-01-01",
                "--end",
                "2025-01-02",
                "--output",
                str(output_path),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        require(file_output.stdout == "", "file output should not also write stdout")
        require(file_output.stderr == "", f"unexpected file-output stderr: {file_output.stderr}")
        empty_document = json.loads(output_path.read_text(encoding="utf-8"))
        require(empty_document["events"] == [], "empty range should write an empty event list")

    completed = subprocess.run(
        [
            sys.argv[1],
            "--data-root",
            sys.argv[2],
            "--start",
            "2025-09-01",
            "--end",
            "2027-01-01",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(completed.stderr == "", f"unexpected exporter stderr: {completed.stderr}")
    require("NaN" not in completed.stdout, "JSON must not contain NaN")
    require("Infinity" not in completed.stdout, "JSON must not contain Infinity")

    def reject_constant(value):
        raise ValueError(f"invalid JSON constant {value}")

    document = json.loads(completed.stdout, parse_constant=reject_constant)
    reject_non_finite(document)

    require(document["schema"] == "taiyin.solar-eclipse-forecast", "schema name")
    require(document["schema_version"] == 1, "schema version")
    require(document["time_scale"]["name"] == "UT1_ESTIMATED", "time scale")
    require(document["models"]["earth_surface"] == "wgs84_sea_level", "Earth model")
    require(document["models"]["terrain"] == "none", "terrain model")
    require(document["models"]["lunar_limb"] == "smooth_mean", "lunar limb model")
    require(
        not document["models"]["lunar_limb_correction_enabled"],
        "smooth export lunar limb flag",
    )
    require(document["models"]["lunar_limb_source_id"] is None, "smooth source id")
    require(document["models"]["lunar_limb_generation"] is None, "smooth generation")
    require(document["models"]["eclipse_search_flags"] == 1 << 33, "smooth search flags")
    require(document["models"]["eclipse_route_flags"] == 0, "smooth route flags")
    require(document["models"]["route_sample_count"] == 400, "default route sample count")

    events = document["events"]
    require(len(events) == 3, "range should contain one 2025 and two 2026 eclipses")
    require(len({event["event_id"] for event in events}) == len(events), "event IDs must be unique")
    partial = next(event for event in events if event["maximum"]["calendar_ut"]["year"] == 2025)
    require("partial" in partial["kind"], "September 2025 eclipse should be partial")
    require(partial["route_product"]["available"], "partial route availability")
    require(partial["route_product"]["reason"] is None, "partial route reason")
    require(partial["route_product"]["polygon_available"], "partial polygon availability")
    require(partial["route_product"]["polygon_reason"] is None, "partial polygon reason")
    require(not partial["curves"]["center_line"], "partial eclipse has no center line")
    require(not partial["curves"]["core_north"], "partial eclipse has no north core limit")
    require(not partial["curves"]["core_south"], "partial eclipse has no south core limit")
    require(
        bool(partial["curves"]["penumbral_north"])
        != bool(partial["curves"]["penumbral_south"]),
        "partial eclipse should expose one physical penumbral limit",
    )
    require(
        any(partial["curves"][key] for key in (
            "sunrise_max_a",
            "sunrise_max_b",
            "sunset_max_a",
            "sunset_max_b",
        )),
        "partial eclipse should expose sunrise/sunset boundary curves",
    )
    require(len(partial["polygons"]["penumbral"]) > 0, "partial penumbral polygon")
    require(polygon_count(partial) > 0, "partial eclipse should return closed wide polygons")
    february = next(
        event
        for event in events
        if (event["maximum"]["calendar_ut"]["year"] == 2026
            and event["maximum"]["calendar_ut"]["month"] == 2)
    )
    require("annular" in february["kind"], "February 2026 eclipse should be annular")
    require("central" in february["kind"], "February 2026 eclipse should be central")
    require(
        len(february["curves"]["core_begin_horizon"]) >= 2,
        "February 2026 core begin cap",
    )
    require(
        len(february["curves"]["core_end_horizon"]) >= 2,
        "February 2026 core end cap",
    )
    february_core_kinds = {point["point_kind"] for point in february["polygons"]["core"]}
    require(7 in february_core_kinds, "February 2026 core begin cap polygon points")
    require(8 in february_core_kinds, "February 2026 core end cap polygon points")
    august = next(
        event
        for event in events
        if event["maximum"]["calendar_ut"]["month"] == 8
    )
    require("total" in august["kind"], "August 2026 eclipse should be total")
    require("central" in august["kind"], "August 2026 eclipse should be central")
    require(august["route_product"]["available"], "August route should be available")
    require(august["route_product"]["polygon_available"], "August polygons should be available")
    require(august["route_product"]["polygon_reason"] is None, "August polygon reason")
    for contact in ("p1", "c1", "greatest", "c4", "p4"):
        require(august["contacts"][contact] is not None, f"missing {contact}")

    summary = august["route_summary"]
    require(curve_count(august) == summary["curve_point_count"], "curve count mismatch")
    require(polygon_count(august) == summary["polygon_point_count"], "polygon count mismatch")
    require(len(august["curves"]["center_line"]) == summary["center_line_count"], "center count")
    require(
        len(august["curves"]["core_begin_horizon"])
        == summary["core_begin_horizon_count"],
        "core begin horizon count",
    )
    require(
        len(august["curves"]["core_end_horizon"])
        == summary["core_end_horizon_count"],
        "core end horizon count",
    )
    require(len(august["curves"]["core_begin_horizon"]) >= 2, "smooth core begin cap")
    require(len(august["curves"]["core_end_horizon"]) >= 2, "smooth core end cap")
    require(len(august["polygons"]["core"]) == summary["core_polygon_point_count"], "core polygon count")
    core_point_kinds = {point["point_kind"] for point in august["polygons"]["core"]}
    require(7 in core_point_kinds, "core polygon has begin horizon points")
    require(8 in core_point_kinds, "core polygon has end horizon points")
    require(
        len(august["polygons"]["penumbral"]) == summary["penumbral_polygon_point_count"],
        "penumbral polygon count",
    )
    require(
        len(august["polygons"]["half_magnitude"])
        == summary["half_magnitude_polygon_point_count"],
        "half-magnitude polygon count",
    )
    require(not august["curves"]["penumbral_north"], "polar total has no physical north penumbral limit")
    require(august["curves"]["sunrise_max_a"], "polar total north boundary uses sunrise maximum")
    require(len(august["polygons"]["penumbral"]) > 0, "polar total penumbral polygon")
    require(len(august["polygons"]["half_magnitude"]) > 0, "polar total half-magnitude polygon")

    missing_limb = subprocess.run(
        [
            sys.argv[1],
            "--data-root",
            sys.argv[2],
            "--lunar-limb",
            str(Path(sys.argv[2]) / "lunar-limb" / "missing.tll1"),
            "--start",
            "2026-08-01",
            "--end",
            "2026-09-01",
        ],
        capture_output=True,
        text=True,
    )
    require(missing_limb.returncode == 1, "missing lunar-limb model should fail")

    limb_path = Path(sys.argv[2]) / "lunar-limb" / "kaguya_lalt_16ppd.tll1"
    corrected = subprocess.run(
        [
            sys.argv[1],
            "--data-root",
            sys.argv[2],
            "--lunar-limb",
            str(limb_path),
            "--start",
            "2026-08-01",
            "--end",
            "2026-09-01",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(corrected.stderr == "", "corrected export should not write stderr")
    corrected_document = json.loads(corrected.stdout, parse_constant=reject_constant)
    reject_non_finite(corrected_document)
    corrected_models = corrected_document["models"]
    require(corrected_models["lunar_limb"] == "tll1", "corrected lunar limb model")
    require(corrected_models["lunar_limb_correction_enabled"], "corrected lunar limb flag")
    require(corrected_models["lunar_limb_source_id"] == 1, "corrected source id")
    require(corrected_models["lunar_limb_generation"] is not None, "corrected generation")
    require(
        corrected_models["eclipse_search_flags"] == (1 << 33) | (1 << 38),
        "corrected search flags",
    )
    require(corrected_models["eclipse_route_flags"] == 1 << 38, "corrected route flags")
    require(corrected_models["route_sample_count"] == 400, "corrected route sample count")
    require(len(corrected_document["events"]) == 1, "corrected range event count")
    corrected_august = corrected_document["events"][0]
    require(corrected_august["event_id"] == august["event_id"], "corrected event identity")
    smooth_limit = august["curves"]["core_north"]
    corrected_limit = corrected_august["curves"]["core_north"]
    require(smooth_limit and corrected_limit, "corrected core route availability")
    require(
        point_list_differs(smooth_limit, corrected_limit)
        or point_list_differs(august["curves"]["core_south"], corrected_august["curves"]["core_south"])
        or point_list_differs(august["polygons"]["core"], corrected_august["polygons"]["core"]),
        "lunar-limb route should differ from smooth route",
    )

    sampled = subprocess.run(
        [
            sys.argv[1],
            "--data-root",
            sys.argv[2],
            "--route-samples",
            "64",
            "--start",
            "2026-08-01",
            "--end",
            "2026-09-01",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    require(sampled.stderr == "", "sampled export should not write stderr")
    sampled_document = json.loads(sampled.stdout, parse_constant=reject_constant)
    reject_non_finite(sampled_document)
    require(sampled_document["models"]["route_sample_count"] == 64, "custom route sample count")
    require(len(sampled_document["events"]) == 1, "custom sampled range event count")


if __name__ == "__main__":
    main()
