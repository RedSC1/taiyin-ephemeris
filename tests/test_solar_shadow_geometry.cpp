#include "legacy/sxwnl/eclipse/solar_eclipse_sxwnl.h"
#include "runtime/eclipse/solar_route_geometry.h"
#include "runtime/eclipse/solar_shadow_geometry.h"

#include "taiyin/angle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

bool close(double actual, double expected, double tolerance) {
    return std::isfinite(actual)
        && std::isfinite(expected)
        && std::fabs(actual - expected) <= tolerance;
}

double signed_angle_delta(double actual, double expected) {
    double delta = actual - expected;
    while (delta > taiyin::TAIYIN_PI) delta -= 2.0 * taiyin::TAIYIN_PI;
    while (delta < -taiyin::TAIYIN_PI) delta += 2.0 * taiyin::TAIYIN_PI;
    return delta;
}

// Frozen pre-optimization path: each angle uses the full vector/ellipsoid
// intersection, including the original tangent tolerance and vertex filter.
bool reference_maximum(const std::array<double, 7>& p, double* best) {
    using namespace taiyin::runtime;
    const auto at = [&](double angle, double* value) {
        const auto generator = make_solar_circular_cone_generator(
            p[0], p[1], p[2], p[3], p[4], angle);
        SolarGeneratorEarthIntersection result;
        if (!intersect_solar_generator_with_oblate_earth(
                generator, p[5], p[6], &result)) return false;
        *value = result.vertex_parameter > 0.0
            && std::isfinite(result.normalized_discriminant)
            ? result.normalized_discriminant : -INFINITY;
        return true;
    };
    const double step = 2.0 * taiyin::TAIYIN_PI / 24.0;
    double angle = 0.0;
    *best = -INFINITY;
    for (int i = 0; i < 24; ++i) {
        double value;
        if (!at(i * step, &value)) return false;
        if (value > *best) {
            *best = value;
            angle = i * step;
        }
    }
    if (!std::isfinite(*best)) return true;
    double lo = angle - step, hi = angle + step;
    const double ratio = (std::sqrt(5.0) - 1.0) * 0.5;
    double a = hi - ratio * (hi - lo), b = lo + ratio * (hi - lo), fa, fb;
    if (!at(a, &fa) || !at(b, &fb)) return false;
    for (int i = 0; i < 28; ++i) {
        if (fa > fb) {
            hi = b; b = a; fb = fa;
            a = hi - ratio * (hi - lo);
            if (!at(a, &fa)) return false;
        } else {
            lo = a; a = b; fa = fb;
            b = lo + ratio * (hi - lo);
            if (!at(b, &fb)) return false;
        }
    }
    *best = std::max(fa, fb);
    return true;
}

bool compare_maximum(const std::array<double, 7>& p) {
    using namespace taiyin::runtime;
    SolarConeEarthTangency result;
    double expected;
    const bool reference_ok = reference_maximum(p, &expected);
    const bool ok = maximize_solar_circular_cone_earth_discriminant(
        p[0], p[1], p[2], p[3], p[4], p[5], p[6], &result);
    if (ok != reference_ok) return false;
    if (!ok) return !result.valid;
    if (result.valid != std::isfinite(expected)) return false;
    if (!result.valid) return true;
    // Round-off only, not a relaxed time/root solver tolerance. Angular maxima
    // can be flat, so validate the returned angle by its scalar, not angle delta.
    if (!close(result.normalized_discriminant, expected, 1.0e-10)) return false;
    const auto generator = make_solar_circular_cone_generator(
        p[0], p[1], p[2], p[3], p[4], result.generator_angle_rad);
    SolarGeneratorEarthIntersection selected;
    if (!intersect_solar_generator_with_oblate_earth(generator, p[5], p[6], &selected)) {
        return false;
    }
    return close(result.normalized_discriminant, selected.normalized_discriminant, 1.0e-10)
        && close(result.vertex_parameter, selected.vertex_parameter, 1.0e-10);
}

}  // namespace

int main() {
    using namespace taiyin;
    using namespace taiyin::runtime;

    constexpr double earth_axis_ratio = 0.99664719;

    // Deterministic differential coverage: oblate/spherical Earth, both signs
    // of cone radius and axis depth, tilted frames, misses and intersections.
    {
        uint32_t state = 0x19350105u;
        const auto random = [&]() {
            state = state * 1664525u + 1013904223u;
            return static_cast<double>(state) / 4294967296.0;
        };
        for (int i = 0; i < 640; ++i) {
            const std::array<double, 7> p{{
                4.0 * random() - 2.0, 4.0 * random() - 2.0,
                (45.0 + 30.0 * random()) * (i % 2 ? 1.0 : -1.0),
                0.24 + 0.06 * random(), -0.1 + 1.6 * random(),
                (2.0 * random() - 1.0) * taiyin::TAIYIN_PI,
                i % 3 ? earth_axis_ratio : 0.7 + 0.3 * random(),
            }};
            if (!compare_maximum(p)) return fail("factored cone vs vector maximum");
        }
        for (double gap : {-1.0e-7, -1.0e-10, 0.0, 1.0e-10, 1.0e-7}) {
            if (!compare_maximum({{1.25 + gap, 0, 60, .25, .25, 0, 1}})) {
                return fail("factored cone near grazing contact");
            }
        }
        for (const auto& p : {
                std::array<double, 7>{{0, 0, 60, 0, 0, 0, 1}},
                std::array<double, 7>{{0, 0, 0, .25, .25, 0, 1}},
                std::array<double, 7>{{0, 0, 0, .25, .75, 0, 1}},
                std::array<double, 7>{{1.0e200, 0, 60, .25, .75, 0, 1}},
                std::array<double, 7>{{0, 0, 60, .25, .75, 0, 1.0e-200}}}) {
            if (!compare_maximum(p)) return fail("factored degenerate cone behavior");
        }
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        for (int field = 0; field < 7; ++field) {
            for (double invalid : {nan, infinity, -infinity}) {
                std::array<double, 7> p{{0, 0, 60, .25, .75, 0, 1}};
                p[field] = invalid;
                SolarConeEarthTangency out;
                out.valid = true;
                if (maximize_solar_circular_cone_earth_discriminant(
                        p[0], p[1], p[2], p[3], p[4], p[5], p[6], &out)
                    || out.valid) return fail("invalid cone input must clear output");
            }
        }
        SolarConeEarthTangency out;
        if (maximize_solar_circular_cone_earth_discriminant(0, 0, 60, .25, .75, 0, 0, &out)
            || maximize_solar_circular_cone_earth_discriminant(0, 0, 60, .25, .75, 0, -1, &out)
            || maximize_solar_circular_cone_earth_discriminant(0, 0, 60, .25, .75, 0, 1, nullptr)) {
            return fail("invalid cone ratio or output pointer");
        }
    }

    // Both intersections of a shallow overlap may lie inside one angular
    // sampling cell.  Route geometry must still report both curve points.
    {
        const solar_route_geometry::ProjectedIntersection overlap =
            solar_route_geometry::intersect_projected_ellipse_circle(
                1.0,
                earth_axis_ratio,
                0.4737231662,
                1.4722878835,
                -0.0641691783);
        if (overlap.count != 2) return fail("shallow projected overlap root count");
    }

    // A line tangent to the unit equator has one stable root.
    {
        const SolarShadowGenerator tangent{
            Vector3{1.0, -2.0, 0.0},
            Vector3{0.0, 1.0, 0.0},
        };
        SolarGeneratorEarthIntersection intersection;
        if (!intersect_solar_generator_with_oblate_earth(
                tangent, 0.0, earth_axis_ratio, &intersection)) {
            return fail("tangent line evaluation");
        }
        if (intersection.count != 1) return fail("tangent line root count");
        if (!close(intersection.parameter[0], 2.0, 1.0e-14)) {
            return fail("tangent line parameter");
        }
        if (!close(intersection.point_equatorial_frame[0].x, 1.0, 1.0e-14)
            || !close(intersection.point_equatorial_frame[0].y, 0.0, 1.0e-14)
            || !close(intersection.point_equatorial_frame[0].z, 0.0, 1.0e-14)) {
            return fail("tangent line point");
        }
    }

    // A missed generator is a successful geometric evaluation with no roots.
    {
        const SolarShadowGenerator miss{
            Vector3{2.0, -1.0, 0.0},
            Vector3{0.0, 1.0, 0.0},
        };
        SolarGeneratorEarthIntersection intersection;
        if (!intersect_solar_generator_with_oblate_earth(
                miss, 0.0, earth_axis_ratio, &intersection)) {
            return fail("missed line evaluation");
        }
        if (intersection.count != 0) return fail("missed line root count");
    }

    // The global-contact scalar must change sign exactly when a circular
    // shadow cylinder becomes tangent to a spherical Earth. This isolates
    // the generator maximization from ephemeris and Besselian calculations.
    {
        const auto tangency_scalar = [](double radius, double* out_scalar) {
            SolarConeEarthTangency tangency;
            if (!maximize_solar_circular_cone_earth_discriminant(
                    0.0,
                    0.0,
                    10.0,
                    radius,
                    radius,
                    0.0,
                    1.0,
                    &tangency)
                || !tangency.valid) {
                return false;
            }
            *out_scalar = tangency.normalized_discriminant;
            return true;
        };
        double inside = NAN;
        double tangent = NAN;
        double outside = NAN;
        if (!tangency_scalar(0.9, &inside)
            || !tangency_scalar(1.0, &tangent)
            || !tangency_scalar(1.1, &outside)) {
            return fail("circular cone tangency maximization");
        }
        if (!(inside > 0.0)) return fail("intersecting cylinder discriminant sign");
        if (!close(tangent, 0.0, 1.0e-14)) {
            return fail("tangent cylinder discriminant");
        }
        if (!(outside < 0.0)) return fail("missed cylinder discriminant sign");
    }

    // The coarse bracket must find the same global angular maximum as a dense
    // independent generator scan for an offset, rotated penumbral cone.
    {
        SolarConeEarthTangency optimized;
        if (!maximize_solar_circular_cone_earth_discriminant(
                0.37,
                -0.62,
                57.5,
                0.2725076,
                0.91,
                1.17,
                earth_axis_ratio,
                &optimized)
            || !optimized.valid) {
            return fail("offset cone tangency maximization");
        }
        double dense_best = -INFINITY;
        for (int index = 0; index < 8192; ++index) {
            const double angle = 2.0 * taiyin::TAIYIN_PI
                * static_cast<double>(index) / 8192.0;
            const SolarShadowGenerator generator =
                make_solar_circular_cone_generator(
                    0.37, -0.62, 57.5, 0.2725076, 0.91, angle);
            SolarGeneratorEarthIntersection intersection;
            if (!intersect_solar_generator_with_oblate_earth(
                    generator, 1.17, earth_axis_ratio, &intersection)) {
                return fail("dense cone tangency scan");
            }
            if (intersection.vertex_parameter > 0.0) {
                dense_best = std::max(
                    dense_best, intersection.normalized_discriminant);
            }
        }
        if (!std::isfinite(dense_best)
            || optimized.normalized_discriminant + 1.0e-12 < dense_best
            || optimized.normalized_discriminant - dense_best > 1.0e-7) {
            return fail("optimized cone tangency differs from dense scan");
        }
    }

    // Compare the native generator/ellipsoid kernel with the archived SXWNL
    // lineEar2 geometry over an entire cone. This freezes the established
    // Besselian frame convention while production code moves off the legacy
    // helper.
    {
        const double axis_x = -0.18;
        const double axis_y = 0.12;
        const double moon_z = 57.5;
        const double moon_radius = 0.2725076;
        const double fundamental_radius = 0.78;
        const double frame_rotation = 1.23;
        const double j = -0.42;
        const double gast = 2.17;
        const sxwnl::solar::BesselianFrame legacy_frame{
            j, frame_rotation, gast,
        };

        int compared = 0;
        for (int index = 0; index < 1440; ++index) {
            const double theta = 2.0 * taiyin::TAIYIN_PI
                * static_cast<double>(index) / 1440.0;
            const SolarShadowGenerator generator =
                make_solar_circular_cone_generator(
                    axis_x,
                    axis_y,
                    moon_z,
                    moon_radius,
                    fundamental_radius,
                    theta);
            const Vector3 target = vector3_add(
                generator.origin, generator.direction);
            const sxwnl::solar::GeoPoint legacy = sxwnl::solar::lineEar2(
                generator.origin.x,
                generator.origin.y,
                generator.origin.z,
                target.x,
                target.y,
                target.z,
                earth_axis_ratio,
                1.0,
                legacy_frame);

            SolarConeEarthPoint current;
            if (!intersect_solar_circular_cone_generator_with_oblate_earth(
                    axis_x,
                    axis_y,
                    moon_z,
                    moon_radius,
                    fundamental_radius,
                    theta,
                    frame_rotation,
                    j - gast,
                    earth_axis_ratio,
                    &current)) {
                return fail("cone generator evaluation");
            }
            if (current.valid != legacy.valid) {
                return fail("cone generator validity differs from legacy");
            }
            if (!current.valid) continue;
            ++compared;

            if (std::fabs(signed_angle_delta(
                    current.longitude_rad, legacy.longitude_rad)) > 2.0e-11
                || !close(current.latitude_rad, legacy.latitude_rad, 2.0e-11)) {
                std::fprintf(
                    stderr,
                    "theta=%.17g current=(%.17g, %.17g) legacy=(%.17g, %.17g) delta=(%.17g, %.17g) t=%.17g\n",
                    theta,
                    current.longitude_rad,
                    current.latitude_rad,
                    legacy.longitude_rad,
                    legacy.latitude_rad,
                    signed_angle_delta(current.longitude_rad, legacy.longitude_rad),
                    current.latitude_rad - legacy.latitude_rad,
                    current.generator_parameter);
                return fail("cone generator geodetic point differs from legacy");
            }
            const Vector3& point = current.point_equatorial_frame;
            const double residual = point.x * point.x + point.y * point.y
                + point.z * point.z / (earth_axis_ratio * earth_axis_ratio)
                - 1.0;
            if (std::fabs(residual) > 2.0e-11) {
                return fail("cone generator point is off WGS84 ellipsoid");
            }
        }
        if (compared < 100) return fail("too few cone generators reached Earth");
    }

    // Negative fundamental radii describe the antumbral/core branch. Keep its
    // generator convention locked to the archived implementation as well.
    {
        const double axis_x = -0.18;
        const double axis_y = 0.12;
        const double moon_z = 57.5;
        const double moon_radius = 0.2725076;
        const double fundamental_radius = -0.06;
        const double frame_rotation = 1.23;
        const double j = -0.42;
        const double gast = 2.17;
        const sxwnl::solar::BesselianFrame legacy_frame{
            j, frame_rotation, gast,
        };
        int compared = 0;
        for (int index = 0; index < 720; ++index) {
            const double theta = 2.0 * taiyin::TAIYIN_PI
                * static_cast<double>(index) / 720.0;
            const SolarShadowGenerator generator =
                make_solar_circular_cone_generator(
                    axis_x,
                    axis_y,
                    moon_z,
                    moon_radius,
                    fundamental_radius,
                    theta);
            const Vector3 target = vector3_add(
                generator.origin, generator.direction);
            const sxwnl::solar::GeoPoint legacy = sxwnl::solar::lineEar2(
                generator.origin.x,
                generator.origin.y,
                generator.origin.z,
                target.x,
                target.y,
                target.z,
                earth_axis_ratio,
                1.0,
                legacy_frame);
            SolarConeEarthPoint current;
            if (!intersect_solar_circular_cone_generator_with_oblate_earth(
                    axis_x,
                    axis_y,
                    moon_z,
                    moon_radius,
                    fundamental_radius,
                    theta,
                    frame_rotation,
                    j - gast,
                    earth_axis_ratio,
                    &current)) {
                return fail("negative-radius cone generator evaluation");
            }
            if (current.valid != legacy.valid) {
                return fail("negative-radius generator validity differs from legacy");
            }
            if (!current.valid) continue;
            ++compared;
            if (std::fabs(signed_angle_delta(
                    current.longitude_rad, legacy.longitude_rad)) > 2.0e-11
                || !close(current.latitude_rad, legacy.latitude_rad, 2.0e-11)) {
                return fail("negative-radius cone generator differs from legacy");
            }
        }
        if (compared < 100) {
            return fail("too few negative-radius generators reached Earth");
        }
    }

    std::puts("solar shadow geometry tests passed");
    return 0;
}
