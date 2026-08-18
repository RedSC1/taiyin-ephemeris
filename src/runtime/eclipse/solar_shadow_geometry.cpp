#include "runtime/eclipse/solar_shadow_geometry.h"

#include "taiyin/angle.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

double quadratic_form_oblate(
    const Vector3& value,
    double inverse_axis_ratio2
) noexcept {
    return value.x * value.x
        + value.y * value.y
        + value.z * value.z * inverse_axis_ratio2;
}

double quadratic_form_oblate_mixed(
    const Vector3& a,
    const Vector3& b,
    double inverse_axis_ratio2
) noexcept {
    return a.x * b.x
        + a.y * b.y
        + a.z * b.z * inverse_axis_ratio2;
}

double normalize_signed_radians(double value) noexcept {
    const double two_pi = 2.0 * TAIYIN_PI;
    value = std::fmod(value + TAIYIN_PI, two_pi);
    if (value < 0.0) value += two_pi;
    return value - TAIYIN_PI;
}

Vector3 point_on_generator(
    const SolarShadowGenerator& generator,
    double parameter
) noexcept {
    return vector3_add(
        generator.origin,
        vector3_scale(generator.direction, parameter));
}

}  // namespace

SolarGeneratorEarthIntersection::SolarGeneratorEarthIntersection() noexcept
    : count(0),
      discriminant(std::numeric_limits<double>::quiet_NaN()),
      quadratic_a(NAN),
      vertex_parameter(NAN),
      normalized_discriminant(NAN),
      generator_direction_norm(NAN),
      parameter{NAN, NAN},
      point_shadow_frame{{NAN, NAN, NAN}, {NAN, NAN, NAN}},
      point_equatorial_frame{{NAN, NAN, NAN}, {NAN, NAN, NAN}} {}

SolarConeEarthPoint::SolarConeEarthPoint() noexcept
    : valid(false),
      generator_angle_rad(NAN),
      generator_parameter(NAN),
      line_discriminant(NAN),
      distance_to_parameter_one(NAN),
      point_shadow_frame{NAN, NAN, NAN},
      point_equatorial_frame{NAN, NAN, NAN},
      longitude_rad(NAN),
      latitude_rad(NAN) {}

SolarConeEarthTangency::SolarConeEarthTangency() noexcept
    : valid(false),
      generator_angle_rad(NAN),
      normalized_discriminant(NAN),
      vertex_parameter(NAN) {}

SolarShadowGenerator make_solar_circular_cone_generator(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double generator_angle_rad
) noexcept {
    const double nx = std::cos(generator_angle_rad);
    const double ny = std::sin(generator_angle_rad);
    const Vector3 origin{
        axis_x + moon_radius * nx,
        axis_y + moon_radius * ny,
        moon_z,
    };
    const Vector3 fundamental_point{
        axis_x + fundamental_radius * nx,
        axis_y + fundamental_radius * ny,
        0.0,
    };
    return SolarShadowGenerator{
        origin,
        vector3_subtract(fundamental_point, origin),
    };
}

bool intersect_solar_generator_with_oblate_earth(
    const SolarShadowGenerator& generator,
    double frame_rotation_rad,
    double earth_axis_ratio,
    SolarGeneratorEarthIntersection* out
) noexcept {
    if (out) *out = SolarGeneratorEarthIntersection();
    if (!out
        || !std::isfinite(frame_rotation_rad)
        || !std::isfinite(earth_axis_ratio)
        || !(earth_axis_ratio > 0.0)
        || !std::isfinite(generator.origin.x)
        || !std::isfinite(generator.origin.y)
        || !std::isfinite(generator.origin.z)
        || !std::isfinite(generator.direction.x)
        || !std::isfinite(generator.direction.y)
        || !std::isfinite(generator.direction.z)) {
        return false;
    }

    const Vector3 origin_equatorial = rotate_x(
        generator.origin, frame_rotation_rad);
    const Vector3 direction_equatorial = rotate_x(
        generator.direction, frame_rotation_rad);
    out->generator_direction_norm = vector3_norm(generator.direction);
    const double inverse_axis_ratio2 = 1.0
        / (earth_axis_ratio * earth_axis_ratio);
    const double a = quadratic_form_oblate(
        direction_equatorial, inverse_axis_ratio2);
    const double b_half = quadratic_form_oblate_mixed(
        origin_equatorial, direction_equatorial, inverse_axis_ratio2);
    const double c = quadratic_form_oblate(
        origin_equatorial, inverse_axis_ratio2) - 1.0;
    if (!(a > 0.0) || !std::isfinite(a)
        || !std::isfinite(b_half) || !std::isfinite(c)) {
        return false;
    }
    out->quadratic_a = a;
    out->vertex_parameter = -b_half / a;

    double discriminant = b_half * b_half - a * c;
    const double discriminant_scale = std::max(
        1.0, std::fabs(b_half * b_half) + std::fabs(a * c));
    const double tangent_tolerance = 64.0
        * std::numeric_limits<double>::epsilon() * discriminant_scale;
    if (discriminant < -tangent_tolerance) {
        out->discriminant = discriminant;
        out->normalized_discriminant = discriminant / a;
        return true;
    }
    if (discriminant < 0.0) discriminant = 0.0;
    out->discriminant = discriminant;
    out->normalized_discriminant = discriminant / a;

    if (discriminant == 0.0) {
        out->count = 1;
        out->parameter[0] = -b_half / a;
    } else {
        const double root = std::sqrt(discriminant);
        // The q formulation avoids cancellation for the root farther from
        // zero; the second root follows from the product c/a.
        const double q = -b_half - std::copysign(root, b_half);
        double t0 = q / a;
        double t1 = q != 0.0 ? c / q : (-b_half + root) / a;
        if (t1 < t0) std::swap(t0, t1);
        out->count = 2;
        out->parameter[0] = t0;
        out->parameter[1] = t1;
    }

    for (int index = 0; index < out->count; ++index) {
        out->point_shadow_frame[index] = point_on_generator(
            generator, out->parameter[index]);
        out->point_equatorial_frame[index] = rotate_x(
            out->point_shadow_frame[index], frame_rotation_rad);
    }
    return true;
}

bool select_solar_generator_earth_point(
    const SolarGeneratorEarthIntersection& intersection,
    double generator_angle_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept {
    if (out) *out = SolarConeEarthPoint();
    if (!out || intersection.count <= 0 || intersection.count > 2
        || !std::isfinite(generator_angle_rad)
        || !std::isfinite(longitude_rotation_rad)
        || !std::isfinite(earth_axis_ratio)
        || !(earth_axis_ratio > 0.0)) {
        return false;
    }

    int selected = 0;
    if (intersection.count == 2
        && std::fabs(intersection.parameter[1])
            < std::fabs(intersection.parameter[0])) {
        selected = 1;
    }
    const Vector3& point = intersection.point_equatorial_frame[selected];
    const double horizontal = std::hypot(point.x, point.y);
    if (!std::isfinite(horizontal) || !std::isfinite(point.z)) return false;

    out->valid = true;
    out->generator_angle_rad = generator_angle_rad;
    out->generator_parameter = intersection.parameter[selected];
    out->line_discriminant = intersection.discriminant;
    out->distance_to_parameter_one = intersection.generator_direction_norm
        * std::fabs(intersection.parameter[selected] - 1.0);
    out->point_shadow_frame = intersection.point_shadow_frame[selected];
    out->point_equatorial_frame = point;
    out->longitude_rad = normalize_signed_radians(
        std::atan2(point.y, point.x) + longitude_rotation_rad);
    out->latitude_rad = std::atan2(
        point.z / (earth_axis_ratio * earth_axis_ratio),
        horizontal);
    return std::isfinite(out->longitude_rad)
        && std::isfinite(out->latitude_rad);
}

bool intersect_solar_circular_cone_generator_with_oblate_earth(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double generator_angle_rad,
    double frame_rotation_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept {
    if (out) *out = SolarConeEarthPoint();
    if (!out) return false;
    const SolarShadowGenerator generator = make_solar_circular_cone_generator(
        axis_x,
        axis_y,
        moon_z,
        moon_radius,
        fundamental_radius,
        generator_angle_rad);
    SolarGeneratorEarthIntersection intersection;
    if (!intersect_solar_generator_with_oblate_earth(
            generator,
            frame_rotation_rad,
            earth_axis_ratio,
            &intersection)) {
        return false;
    }
    if (intersection.count == 0) {
        out->line_discriminant = intersection.discriminant;
        return true;
    }
    return select_solar_generator_earth_point(
        intersection,
        generator_angle_rad,
        longitude_rotation_rad,
        earth_axis_ratio,
        out);
}

bool intersect_solar_shadow_axis_with_oblate_earth(
    double axis_x,
    double axis_y,
    double frame_rotation_rad,
    double longitude_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthPoint* out
) noexcept {
    if (out) *out = SolarConeEarthPoint();
    if (!out || !std::isfinite(axis_x) || !std::isfinite(axis_y)) {
        return false;
    }
    const SolarShadowGenerator axis{
        Vector3{axis_x, axis_y, 2.0},
        Vector3{0.0, 0.0, -2.0},
    };
    SolarGeneratorEarthIntersection intersection;
    if (!intersect_solar_generator_with_oblate_earth(
            axis,
            frame_rotation_rad,
            earth_axis_ratio,
            &intersection)) {
        return false;
    }
    if (intersection.count == 0) {
        out->line_discriminant = intersection.discriminant;
        return true;
    }
    return select_solar_generator_earth_point(
        intersection,
        0.0,
        longitude_rotation_rad,
        earth_axis_ratio,
        out);
}

bool maximize_solar_circular_cone_earth_discriminant(
    double axis_x,
    double axis_y,
    double moon_z,
    double moon_radius,
    double fundamental_radius,
    double frame_rotation_rad,
    double earth_axis_ratio,
    SolarConeEarthTangency* out
) noexcept {
    if (out) *out = SolarConeEarthTangency();
    if (!out
        || !std::isfinite(axis_x)
        || !std::isfinite(axis_y)
        || !std::isfinite(moon_z)
        || !std::isfinite(moon_radius)
        || !std::isfinite(fundamental_radius)
        || !std::isfinite(frame_rotation_rad)
        || !std::isfinite(earth_axis_ratio)
        || !(earth_axis_ratio > 0.0)) {
        return false;
    }

    const auto evaluate = [&](double angle, SolarGeneratorEarthIntersection* intersection) {
        const SolarShadowGenerator generator = make_solar_circular_cone_generator(
            axis_x,
            axis_y,
            moon_z,
            moon_radius,
            fundamental_radius,
            angle);
        return intersect_solar_generator_with_oblate_earth(
            generator,
            frame_rotation_rad,
            earth_axis_ratio,
            intersection);
    };

    // Ellipsoid flattening introduces only low-order angular variation in
    // this scalar. A 15-degree periodic scan safely brackets the global peak;
    // the following refinement, rather than dense sampling, supplies the
    // grazing-contact accuracy.
    constexpr int coarse_count = 24;
    const double coarse_step = 2.0 * TAIYIN_PI / static_cast<double>(coarse_count);
    double best_angle = 0.0;
    double best_value = -std::numeric_limits<double>::infinity();
    SolarGeneratorEarthIntersection best_intersection;
    for (int index = 0; index < coarse_count; ++index) {
        const double angle = coarse_step * static_cast<double>(index);
        SolarGeneratorEarthIntersection intersection;
        if (!evaluate(angle, &intersection)) return false;
        if (!(intersection.vertex_parameter > 0.0)
            || !std::isfinite(intersection.normalized_discriminant)) {
            continue;
        }
        if (intersection.normalized_discriminant > best_value) {
            best_value = intersection.normalized_discriminant;
            best_angle = angle;
            best_intersection = intersection;
        }
    }
    if (!std::isfinite(best_value)) return true;

    // The discriminant is smooth and periodic. A bracket around the best
    // coarse generator followed by golden-section maximization is robust at
    // grazing contact and avoids differentiating near a double root.
    double lo = best_angle - coarse_step;
    double hi = best_angle + coarse_step;
    const double golden = (std::sqrt(5.0) - 1.0) * 0.5;
    double c = hi - golden * (hi - lo);
    double d = lo + golden * (hi - lo);
    SolarGeneratorEarthIntersection ci;
    SolarGeneratorEarthIntersection di;
    if (!evaluate(c, &ci) || !evaluate(d, &di)) return false;
    double fc = ci.vertex_parameter > 0.0
        ? ci.normalized_discriminant
        : -std::numeric_limits<double>::infinity();
    double fd = di.vertex_parameter > 0.0
        ? di.normalized_discriminant
        : -std::numeric_limits<double>::infinity();
    for (int iteration = 0; iteration < 28; ++iteration) {
        if (fc > fd) {
            hi = d;
            d = c;
            di = ci;
            fd = fc;
            c = hi - golden * (hi - lo);
            if (!evaluate(c, &ci)) return false;
            fc = ci.vertex_parameter > 0.0
                ? ci.normalized_discriminant
                : -std::numeric_limits<double>::infinity();
        } else {
            lo = c;
            c = d;
            ci = di;
            fc = fd;
            d = lo + golden * (hi - lo);
            if (!evaluate(d, &di)) return false;
            fd = di.vertex_parameter > 0.0
                ? di.normalized_discriminant
                : -std::numeric_limits<double>::infinity();
        }
    }
    if (fc > fd) {
        best_angle = c;
        best_intersection = ci;
        best_value = fc;
    } else {
        best_angle = d;
        best_intersection = di;
        best_value = fd;
    }
    if (!std::isfinite(best_value)) return true;

    out->valid = true;
    out->generator_angle_rad = normalize_signed_radians(best_angle);
    out->normalized_discriminant = best_value;
    out->vertex_parameter = best_intersection.vertex_parameter;
    return true;
}

}  // namespace runtime
}  // namespace taiyin
