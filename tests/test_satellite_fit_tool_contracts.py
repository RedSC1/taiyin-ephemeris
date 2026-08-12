#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / "tools"))

try:
    import numpy as np
    from prototype_galilean_three_angle_fit import validate_fit_masks
    from prototype_satellite_channel_chebyshev import (
        channel_training_mask,
        summarize_boundary_jumps,
        validate_final_validation,
    )
    from prototype_satellite_residual_chebyshev import inclusive_sample_grid
    from prototype_satellite_harmonic_fit import coverage
    from prototype_satellite_multangle_poisson_fit import fit_projection_basis
    from prototype_uranian_five_angle_fit import (
        URANIAN_IDS,
        fit_uranian_basic_angles,
        make_terms,
        serialize_channel_coefficients,
        validate_uranian_holdout_masks,
        validate_uranian_selection_masks,
    )
except ModuleNotFoundError as exc:  # pragma: no cover - optional offline-fit dependencies
    if exc.name not in {"jplephem", "numpy"}:
        raise
    print(f"SKIP: satellite fit tool dependencies are unavailable: {exc}")
    sys.exit(0)


class SatelliteFitToolContractTests(unittest.TestCase):
    def test_spk_coverage_keeps_union_end_past_nested_override(self) -> None:
        class Segment:
            def __init__(self, start: float, end: float) -> None:
                self.start_jd = start
                self.end_jd = end

        self.assertEqual(
            coverage([Segment(0.0, 100.0), Segment(20.0, 40.0)]),
            (0.0, 100.0),
        )

    def test_multangle_development_basis_excludes_holdout_epochs(self) -> None:
        jd = 2451545.0 + np.arange(8, dtype=np.float64)
        angle = np.arange(8, dtype=np.float64) * 0.1
        position = np.column_stack((np.cos(angle), np.sin(angle), angle * 0.0))
        velocity = np.column_stack((-np.sin(angle), np.cos(angle), angle * 0.0))
        fit = np.ones(8, dtype=bool)
        fit[-1] = False

        changed_position = position.copy()
        changed_velocity = velocity.copy()
        changed_position[-1] = [1000.0, -2000.0, 3000.0]
        changed_velocity[-1] = [-4000.0, 5000.0, -6000.0]
        original = fit_projection_basis(jd, position, velocity, fit, 1)
        changed = fit_projection_basis(
            jd, changed_position, changed_velocity, fit, 1)

        for original_axis, changed_axis in zip(original[0], changed[0]):
            np.testing.assert_allclose(original_axis, changed_axis)
        np.testing.assert_allclose(original[4], changed[4])
        np.testing.assert_allclose(original[5], changed[5])

    def test_uranian_development_basis_excludes_holdout_epochs(self) -> None:
        jd = 2451545.0 + np.arange(12, dtype=np.float64) * 0.1
        orbital_states = {}
        for index, body_id in enumerate(URANIAN_IDS):
            radius = 100000.0 + 20000.0 * index
            inclination = 0.01 * (index + 1)
            rate = 1.0 + 0.1 * index
            angle = rate * (jd - jd[0]) + 0.2 * index
            position = np.column_stack((
                radius * np.cos(angle),
                radius * np.sin(angle) * np.cos(inclination),
                radius * np.sin(angle) * np.sin(inclination),
            ))
            velocity = np.column_stack((
                -radius * rate * np.sin(angle),
                radius * rate * np.cos(angle) * np.cos(inclination),
                radius * rate * np.cos(angle) * np.sin(inclination),
            ))
            orbital_states[body_id] = (position, velocity)
        target_position, target_velocity = orbital_states[URANIAN_IDS[0]]
        fit = np.ones(jd.size, dtype=bool)
        fit[-1] = False

        changed_states = {
            body_id: (position.copy(), velocity.copy())
            for body_id, (position, velocity) in orbital_states.items()
        }
        for position, velocity in changed_states.values():
            position[-1] = [1.0e8, -2.0e8, 3.0e8]
            velocity[-1] = [-4.0e8, 5.0e8, -6.0e8]
        changed_target_position, changed_target_velocity = (
            changed_states[URANIAN_IDS[0]])
        original = fit_uranian_basic_angles(
            jd, target_position, target_velocity, orbital_states, fit, 1)
        changed = fit_uranian_basic_angles(
            jd,
            changed_target_position,
            changed_target_velocity,
            changed_states,
            fit,
            1,
        )

        for original_axis, changed_axis in zip(original[0], changed[0]):
            np.testing.assert_allclose(original_axis, changed_axis)
        for original_coefficients, changed_coefficients in zip(
                original[4], changed[4]):
            np.testing.assert_allclose(
                original_coefficients, changed_coefficients)

    def test_uranian_terms_have_uniform_basic_angle_width(self) -> None:
        base, candidates = make_terms(
            target_index=0,
            base_harmonics=2,
            k_max=1,
            l_max=1,
            angle_count=15,
        )
        self.assertTrue(base)
        self.assertTrue(candidates)
        self.assertTrue(all(len(term) == 15 for term in base + candidates))

    def test_residual_sample_grid_includes_irregular_endpoint(self) -> None:
        grid = inclusive_sample_grid(100.0, 101.0, 0.3)
        self.assertEqual(float(grid[0]), 100.0)
        self.assertEqual(float(grid[-1]), 101.0)
        self.assertTrue(np.all(np.diff(grid) > 0.0))

    def test_residual_sample_grid_does_not_duplicate_exact_endpoint(self) -> None:
        grid = inclusive_sample_grid(100.0, 101.0, 0.25)
        self.assertEqual(grid.size, 5)
        self.assertEqual(float(grid[-1]), 101.0)

    def test_galilean_fit_rejects_empty_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "validation interval"):
            validate_fit_masks(
                np.ones(200, dtype=bool),
                np.zeros(200, dtype=bool),
            )

    def test_galilean_fit_rejects_empty_training(self) -> None:
        with self.assertRaisesRegex(ValueError, "training"):
            validate_fit_masks(
                np.zeros(200, dtype=bool),
                np.ones(200, dtype=bool),
            )

    def test_channel_chebyshev_rejects_empty_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "validation interval"):
            validate_final_validation(100.0, 200.0, np.zeros(200, dtype=bool))

    def test_channel_chebyshev_rejects_reversed_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "reversed"):
            validate_final_validation(200.0, 100.0, np.ones(200, dtype=bool))

    def test_channel_chebyshev_accepts_nonempty_validation(self) -> None:
        mask = np.zeros(200, dtype=bool)
        mask[100] = True
        validate_final_validation(100.0, 200.0, mask)

    def test_channel_plane_training_excludes_both_holdouts(self) -> None:
        held_out = np.array([False, True, False, False])
        validation = np.array([False, False, True, False])
        self.assertEqual(
            channel_training_mask(held_out, validation).tolist(),
            [True, False, False, True],
        )

    def test_single_segment_has_zero_boundary_jump(self) -> None:
        self.assertEqual(
            summarize_boundary_jumps(np.asarray([], dtype=np.float64)),
            {"rms_km": 0.0, "p95_km": 0.0, "max_km": 0.0},
        )

    def test_uranian_coefficients_serialize_each_channel(self) -> None:
        coefficients = np.arange(18, dtype=np.float64).reshape(6, 3)
        serialized = serialize_channel_coefficients(coefficients)
        self.assertEqual(serialized["radius_km"], coefficients[:, 0].tolist())
        self.assertEqual(
            serialized["phase_residual_rad"], coefficients[:, 1].tolist())
        self.assertEqual(serialized["height_km"], coefficients[:, 2].tolist())

    def test_uranian_fit_rejects_empty_validation(self) -> None:
        with self.assertRaisesRegex(ValueError, "validation interval"):
            validate_uranian_holdout_masks(
                np.ones(200, dtype=bool),
                np.zeros(200, dtype=bool),
                3,
            )

    def test_uranian_fit_rejects_empty_selection_split(self) -> None:
        with self.assertRaisesRegex(ValueError, "candidate-selection"):
            validate_uranian_selection_masks(
                np.zeros(200, dtype=bool),
                np.ones(200, dtype=bool),
                3,
            )

    def test_uranian_fit_rejects_empty_development_split(self) -> None:
        with self.assertRaisesRegex(ValueError, "development samples"):
            validate_uranian_selection_masks(
                np.ones(200, dtype=bool),
                np.zeros(200, dtype=bool),
                3,
            )


if __name__ == "__main__":
    unittest.main()
