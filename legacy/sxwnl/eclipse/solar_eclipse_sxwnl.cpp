#include "solar_eclipse_sxwnl.h"

#include "taiyin/geodetic_constants.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace taiyin {
namespace runtime {
namespace sxwnl {
namespace solar {

// Direct-port workspace for 寿星万年历 solar-eclipse algorithms.
//
// Source reference: 寿星万年历 eph.js (ysPL/rsPL solar-eclipse routines).
//
// Target functions to port here before wiring back to the public Taiyin API:
//   - ysPL.feature()
//   - ysPL.qrd()
//   - ysPL.Vxy()
//   - ysPL.nanbei()
//   - ysPL.mQie()
//   - ysPL.mDian()
//   - ysPL.jieX()
//   - ysPL.jieX2()
//   - ysPL.jieX3()
//
// Keep this file close to the original 寿星 variable names during the raw-port
// phase.  Only after the port is verified should a thin Taiyin-shaped API layer
// translate the raw structs into SolarEclipseResult/SolarEclipseRouteRow.

const double kPi = 3.141592653589793238462643383279502884;
const double kTwoPi = 2.0 * kPi;
constexpr double kSxwnlEarthRadiusKm = 6378.1366;
constexpr double kSxwnlEarthAxisRatio = 0.99664719;
constexpr double kSxwnlAuKm = 1.49597870691e8;

double rad2rrad(double x) noexcept {
    x = std::fmod(x + kPi, kTwoPi);
    if (x < 0.0) x += kTwoPi;
    return x - kPi;
}

Vec3 llr_to_xyz(double lon_rad, double lat_rad, double radius) noexcept {
    const double cos_lat = std::cos(lat_rad);
    return {
        radius * cos_lat * std::cos(lon_rad),
        radius * cos_lat * std::sin(lon_rad),
        radius * std::sin(lat_rad)
    };
}

Vec3 xyz_to_llr(const Vec3& v) noexcept {
    return {
        std::atan2(v.y, v.x),
        std::atan2(v.z, std::hypot(v.x, v.y)),
        std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
    };
}

Vec3 llrConv(const Vec3& llr, double angle_rad) noexcept {
    // Port of 寿星万年历 llrConv() for rotating spherical coordinates between
    // equatorial-like planes.  Input/output are (lon, lat, radius).
    const Vec3 xyz = llr_to_xyz(llr.x, llr.y, llr.z);
    const double c = std::cos(angle_rad);
    const double s = std::sin(angle_rad);
    return xyz_to_llr({xyz.x, c * xyz.y - s * xyz.z, s * xyz.y + c * xyz.z});
}

LineEllResult lineEll(
    double x1,
    double y1,
    double z1,
    double x2,
    double y2,
    double z2,
    double e,
    double r
) noexcept {
    // Algorithm source: 寿星万年历 eph.js lineEll().
    // Solve the intersection of the segment-defined line with an oblate Earth,
    // returning the intersection closest to point 1 using the original root
    // selection rule.
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double dz = z2 - z1;
    const double e2 = e * e;
    const double A = dx * dx + dy * dy + dz * dz / e2;
    const double B = x1 * dx + y1 * dy + z1 * dz / e2;
    const double C = x1 * x1 + y1 * y1 + z1 * z1 / e2 - r * r;
    const double D = B * B - A * C;
    LineEllResult out{};
    out.discriminant = D;
    if (D < 0.0 || !(A > 0.0)) return out;
    double root = std::sqrt(D);
    if (B < 0.0) root = -root;
    const double t = (-B + root) / A;
    out.valid = true;
    out.x = x1 + dx * t;
    out.y = y1 + dy * t;
    out.z = z1 + dz * t;
    const double R = std::sqrt(dx * dx + dy * dy + dz * dz);
    out.r1 = R * std::fabs(t);
    out.r2 = R * std::fabs(t - 1.0);
    return out;
}

GeoPoint lineEar2(
    double x1,
    double y1,
    double z1,
    double x2,
    double y2,
    double z2,
    double e,
    double r,
    const BesselianFrame& I
) noexcept {
    // Algorithm source: 寿星万年历 eph.js lineEar2().
    const double P = std::cos(I.W_rad);
    const double Q = std::sin(I.W_rad);
    const double X1 = x1;
    const double Y1 = P * y1 - Q * z1;
    const double Z1 = Q * y1 + P * z1;
    const double X2 = x2;
    const double Y2 = P * y2 - Q * z2;
    const double Z2 = Q * y2 + P * z2;
    const LineEllResult p = lineEll(X1, Y1, Z1, X2, Y2, Z2, e, r);
    GeoPoint out{};
    out.discriminant = p.discriminant;
    if (!p.valid) return out;
    out.valid = true;
    out.x = p.x;
    out.y = p.y;
    out.z = p.z;
    out.r1 = p.r1;
    out.r2 = p.r2;
    out.longitude_rad = rad2rrad(std::atan2(p.y, p.x) + I.J_rad - I.gst_rad);
    out.latitude_rad = std::atan(p.z / (e * e) / std::hypot(p.x, p.y));
    return out;
}

OvlResult cirOvl(double R, double ba, double R2, double x0, double y0) noexcept {
    // Algorithm source: 寿星万年历 eph.js cirOvl().
    OvlResult out{};
    const double d = std::hypot(x0, y0);
    if (!(d > 0.0)) return out;
    const double sinB = y0 / d;
    const double cosB = x0 / d;
    double cosA = (R * R + d * d - R2 * R2) / (2.0 * d * R);
    if (std::fabs(cosA) > 1.0) return out;
    double sinA = std::sqrt(std::max(0.0, 1.0 - cosA * cosA));
    const double ba2 = ba * ba;
    for (int k = -1; k <= 1; k += 2) {
        double S = cosA * sinB + sinA * cosB * static_cast<double>(k);
        const double g = R - S * S * (1.0 / ba2 - 1.0) / 2.0;
        cosA = (g * g + d * d - R2 * R2) / (2.0 * d * g);
        if (std::fabs(cosA) > 1.0) {
            out.n = 0;
            return out;
        }
        sinA = std::sqrt(std::max(0.0, 1.0 - cosA * cosA));
        const double C = cosA * cosB - sinA * sinB * static_cast<double>(k);
        S = cosA * sinB + sinA * cosB * static_cast<double>(k);
        if (k == 1) {
            out.ax = g * C;
            out.ay = g * S;
        } else {
            out.bx = g * C;
            out.by = g * S;
        }
    }
    out.n = 2;
    return out;
}

OvlResult lineOvl(double x1, double y1, double dx, double dy, double r, double ba) noexcept {
    // Algorithm source: 寿星万年历 eph.js lineOvl().
    OvlResult out{};
    const double f = ba * ba;
    const double A = dx * dx + dy * dy / f;
    const double B = x1 * dx + y1 * dy / f;
    const double C = x1 * x1 + y1 * y1 / f - r * r;
    const double D = B * B - A * C;
    if (D < 0.0 || !(A > 0.0)) return out;
    out.n = D == 0.0 ? 1 : 2;
    const double root = std::sqrt(std::max(0.0, D));
    const double t1 = (-B + root) / A;
    const double t2 = (-B - root) / A;
    out.ax = x1 + dx * t1;
    out.ay = y1 + dy * t1;
    out.bx = x1 + dx * t2;
    out.by = y1 + dy * t2;
    const double L = std::hypot(dx, dy);
    out.r1 = L * std::fabs(t1);
    out.r2 = L * std::fabs(t2);
    return out;
}

GeoPoint bse2db(double x, double y, double z, const BesselianFrame& I, bool ellipsoid) noexcept {
    // Algorithm source: 寿星万年历 eph.js bse2db().
    Vec3 r = xyz_to_llr({x, y, z});
    r = llrConv(r, I.W_rad);
    GeoPoint out{};
    out.valid = true;
    out.longitude_rad = rad2rrad(r.x + I.J_rad - I.gst_rad);
    out.latitude_rad = r.y;
    if (ellipsoid) {
        const double ba = kSxwnlEarthAxisRatio;
        out.latitude_rad = std::atan(std::tan(r.y) / (ba * ba));
    }
    return out;
}

GeoPoint bseXY2db(double x, double y, const BesselianFrame& I, bool ellipsoid) noexcept {
    // Algorithm source: 寿星万年历 eph.js bseXY2db().
    const double b = ellipsoid ? kSxwnlEarthAxisRatio : 1.0;
    return lineEar2(x, y, 2.0, x, y, 0.0, b, 1.0, I);
}

SurfaceVelocity Vxy(double x, double y, double s_rad, double vx, double vy) noexcept {
    // Algorithm source: 寿星万年历 eph.js Vxy().
    SurfaceVelocity out{};
    double h = 1.0 - x * x - y * y;
    h = h < 0.0 ? 0.0 : std::sqrt(h);
    out.vx_surface = kTwoPi * (std::sin(s_rad) * h - std::cos(s_rad) * y);
    out.vy_surface = kTwoPi * x * std::cos(s_rad);
    out.vx_relative = vx - out.vx_surface;
    out.vy_relative = vy - out.vy_surface;
    out.speed = std::hypot(out.vx_relative, out.vy_relative);
    return out;
}

ShadowRadii rSM(
    double mR,
    double k,
    double k2,
    double k0,
    double tanf1,
    double tanf2,
    double dyj
) noexcept {
    // Algorithm source: 寿星万年历 eph.js rSM().
    ShadowRadii out{};
    out.r1 = k + tanf1 * mR;
    out.r2 = k2 - tanf2 * mR;
    out.ar2 = std::fabs(out.r2);
    out.sf = k2 / mR / k0 * (dyj + mR);
    return out;
}

BoundaryPoint nanbei(
    double Mx,
    double My,
    double Mz,
    double vx0,
    double vy0,
    int h,
    double r,
    const BesselianFrame& I,
    double k,
    double earth_axis_ratio
) noexcept {
    // Algorithm source: 寿星万年历 eph.js nanbei().
    BoundaryPoint out{};
    if (h == 0 || !std::isfinite(r) || !(std::fabs(vx0) > 0.0)) return out;
    double x = Mx - vy0 / vx0 * r * static_cast<double>(h);
    double y = My + static_cast<double>(h) * r;
    double sinA = 0.0;
    double cosA = 1.0;
    int clipped = 0;
    for (int i = 0; i < 3; ++i) {
        double z = 1.0 - x * x - y * y;
        if (z < 0.0) {
            if (clipped) break;
            z = 0.0;
            ++clipped;
        } else {
            z = std::sqrt(z);
        }
        if (std::fabs(Mz) > 1e-14) {
            x -= (x - Mx) * z / Mz;
            y -= (y - My) * z / Mz;
        }
        const double vx = vx0 - kTwoPi * (std::sin(I.W_rad) * z - std::cos(I.W_rad) * y);
        const double vy = vy0 - kTwoPi * std::cos(I.W_rad) * x;
        const double v = std::hypot(vx, vy);
        if (!(v > 0.0)) return out;
        sinA = static_cast<double>(h) * vy / v;
        cosA = static_cast<double>(h) * vx / v;
        x = Mx - r * sinA;
        y = My + r * cosA;
    }
    const double X = Mx - k * sinA;
    const double Y = My + k * cosA;
    const GeoPoint p = lineEar2(X, Y, Mz, x, y, 0.0, earth_axis_ratio, 1.0, I);
    out.x = x;
    out.y = y;
    if (!p.valid) return out;
    out.valid = true;
    out.longitude_rad = p.longitude_rad;
    out.latitude_rad = p.latitude_rad;
    return out;
}

// ---------------------------------------------------------------------------
// CD2DP: convert equatorial coordinates to horizontal at a given location.
// Algorithm source: 寿星万年历 eph0.js CD2DP().
// Input z is (RA, Dec, radius) in radians.  Returns (azimuth, altitude).
// ---------------------------------------------------------------------------
Vec3 CD2DP(const Vec3& z, double L_rad, double fa_rad, double gst_rad) noexcept {
    Vec3 a = {z.x + kPi / 2.0 - gst_rad - L_rad, z.y, z.z};
    a = llrConv(a, kPi / 2.0 - fa_rad);
    double azimuth = std::fmod(-kPi / 2.0 - a.x + kTwoPi, kTwoPi);
    return {azimuth, a.y, a.z};
}

QrdResult qrd(
    const SolarContext& ctx,
    SplitJulianDate jd,
    double dx,
    double dy,
    int fs
) noexcept {
    // Algorithm source: 寿星万年历 eph.js qrd().
    // fs == 1  -> penumbral contact (r = B.r1)
    // fs != 1  -> umbral contact (r = 0)
    const double ba2 = ctx.bba * ctx.bba;
    Vec3 M;
    if (!ctx.bse_m || !ctx.bse_m(jd, &M)) {
        QrdResult out{};
        return out;
    }
    double x = M.x;
    double y = M.y;
    const ShadowRadii B = rSM(M.z, ctx.k, ctx.k2, ctx.k0, ctx.tanf1, ctx.tanf2, ctx.dyj);
    double r = 0.0;
    if (fs == 1) r = B.r1;
    double d = 1.0 - (1.0 / ba2 - 1.0) * y * y / (x * x + y * y) * 0.5 + r;
    double t = (d * d - x * x - y * y) / (dx * x + dy * y) * 0.5;
    x += t * dx;
    y += t * dy;
    jd += t;

    const double c = (1.0 - ba2) * r * x * y / (d * d * d);
    x += c * y;
    y -= c * x;

    BesselianFrame I;
    if (!ctx.bse || !ctx.bse(jd, &I)) {
        QrdResult out{};
        return out;
    }
    const GeoPoint re = bse2db(x / d, y / d, 0.0, I, true);
    QrdResult out{};
    out.valid = true;
    out.longitude_rad = re.longitude_rad;
    out.latitude_rad = re.latitude_rad;
    out.jd_tt = jd;
    return out;
}

FeatureResult feature(const SolarContext& ctx, SplitJulianDate jd_suo) noexcept {
    // Algorithm source: 寿星万年历 eph.js feature().
    FeatureResult re{};
    re.jd_suo_tt = jd_suo;
    re.delta_t = ctx.delta_t;
    re.ds = ctx.obliquity_rad;

    const double tg = 0.04;
    Vec3 a_vec, b_vec, c_vec;
    if (!ctx.bse_m(jd_suo - tg, &a_vec)) return re;
    if (!ctx.bse_m(jd_suo, &b_vec)) return re;
    if (!ctx.bse_m(jd_suo + tg, &c_vec)) return re;

    const double vx = (c_vec.x - a_vec.x) / tg * 0.5;
    const double vy = (c_vec.y - a_vec.y) / tg * 0.5;
    const double vz = (c_vec.z - a_vec.z) / tg * 0.5;
    const double ax = (c_vec.x + a_vec.x - 2.0 * b_vec.x) / (tg * tg);
    const double ay = (c_vec.y + a_vec.y - 2.0 * b_vec.y) / (tg * tg);
    const double v = std::hypot(vx, vy);
    const double v2 = v * v;

    re.vx = vx;
    re.vy = vy;
    re.ax = ax;
    re.ay = ay;
    re.v = v;
    re.k = (std::fabs(vx) > 1e-14) ? vy / vx : 0.0;

    const double t0 = (v2 > 1e-28) ? -(b_vec.x * vx + b_vec.y * vy) / v2 : 0.0;
    re.maximum_jd_tt = jd_suo + t0;
    re.xc = b_vec.x + vx * t0;
    re.yc = b_vec.y + vy * t0;
    re.zc = b_vec.z + vz * t0 - 1.37 * t0 * t0;
    re.D = (std::fabs(v) > 1e-14) ? (vx * b_vec.y - vy * b_vec.x) / v : 0.0;
    re.d = std::fabs(re.D);

    BesselianFrame I_max;
    if (!ctx.bse(re.maximum_jd_tt, &I_max)) return re;
    re.I = I_max;

    // Shadow-axis intersection with Earth (lineEar2 in eph.js)
    const GeoPoint F = lineEar2(re.xc, re.yc, 2.0, re.xc, re.yc, 0.0, ctx.bba, 1.0, I_max);

    // Shadow radii
    ShadowRadii Bc = rSM(re.zc, ctx.k, ctx.k2, ctx.k0, ctx.tanf1, ctx.tanf2, ctx.dyj);
    ShadowRadii Bp = Bc;
    if (F.valid) {
        Bp = rSM(re.zc - F.r2, ctx.k, ctx.k2, ctx.k0, ctx.tanf1, ctx.tanf2, ctx.dyj);
    }
    re.Bc = Bc;
    re.Bp = Bp;

    ShadowRadii B2 = Bc, B3 = Bc;
    double dt_c = 0.0, t2 = 0.0, t3 = 0.0;
    if (re.d < 1.0) {
        dt_c = std::sqrt(std::max(0.0, 1.0 - re.d * re.d)) / v;
        t2 = t0 - dt_c;
        t3 = t0 + dt_c;
        B2 = rSM(t2 * vz + b_vec.z - 1.37 * t2 * t2, ctx.k, ctx.k2, ctx.k0, ctx.tanf1, ctx.tanf2, ctx.dyj);
        B3 = rSM(t3 * vz + b_vec.z - 1.37 * t3 * t3, ctx.k, ctx.k2, ctx.k0, ctx.tanf1, ctx.tanf2, ctx.dyj);
    }

    // Partial eclipse contact times
    double ls = 1.0;
    double dt_p = 0.0;
    if (re.d < ls) dt_p = std::sqrt(std::max(0.0, ls * ls - re.d * re.d)) / v;
    double t4 = t0 - dt_p;
    double t5 = t0 + dt_p;

    // Local apparent noon parameter
    const double t6 = (std::fabs(vx) > 1e-14) ? -b_vec.x / vx : 0.0;

    // Key-point contacts via qrd()
    if (re.d < 1.0) {
        re.gk1 = qrd(ctx, t2 + jd_suo, vx, vy, 0);
        re.gk2 = qrd(ctx, t3 + jd_suo, vx, vy, 0);
    }
    re.gk3 = qrd(ctx, t4 + jd_suo, vx, vy, 1);
    re.gk4 = qrd(ctx, t5 + jd_suo, vx, vy, 1);

    // gk5: local apparent noon
    {
        BesselianFrame I_noon;
        if (ctx.bse(t6 + jd_suo, &I_noon)) {
            const GeoPoint gp = bseXY2db(t6 * vx + b_vec.x, t6 * vy + b_vec.y, I_noon, true);
            re.gk5.valid = true;
            re.gk5.longitude_rad = gp.longitude_rad;
            re.gk5.latitude_rad = gp.latitude_rad;
            re.gk5.jd_tt = t6 + jd_suo;
        }
    }

    // Type, magnitude, center location
    re.center_valid = F.valid;
    if (!F.valid) {
        const GeoPoint ls_pt = bse2db(re.xc, re.yc, 0.0, I_max, false);
        re.center_longitude_rad = ls_pt.longitude_rad;
        re.center_latitude_rad = ls_pt.latitude_rad;
        re.magnitude = (Bc.r1 - (re.d - 0.9972)) / (Bc.r1 - Bc.r2);
        if (re.d > 0.9972 + Bc.r1) {
            re.type = EclipseNone;
        } else if (re.d > 0.9972 + Bc.ar2) {
            re.type = EclipsePartial;
        } else {
            re.type = (Bp.sf < 1.0) ? EclipsePartialNoCenter : EclipseCentralAnnularTotal;
        }
    } else {
        re.center_longitude_rad = F.longitude_rad;
        re.center_latitude_rad = F.latitude_rad;
        re.center_r2 = F.r2;
        re.magnitude = Bp.sf;
        if (re.d > 0.9966 - Bp.ar2) {
            re.type = EclipseCentralPartial;
        } else {
            if (Bp.sf >= 1.0) {
                re.type = EclipseCentralAnnularTotal;
                if (B2.sf > 1.0 && B3.sf > 1.0) {
                    re.type = EclipseCentralTotal;
                } else if (B2.sf > 1.0) {
                    re.type = EclipseCentralAnnularTotalBeginTotal;
                } else if (B3.sf > 1.0) {
                    re.type = EclipseCentralAnnularTotalEndTotal;
                }
            } else {
                re.type = EclipseCentralAnnular;
            }
        }
    }

    // Sun horizontal coordinates at center point
    {
        Vec3 sun_llr = {I_max.J_rad, I_max.W_rad, 1.0};
        if (ctx.sun && ctx.sun(re.maximum_jd_tt, &sun_llr)) {
            Vec3 sdp = CD2DP(sun_llr, re.center_longitude_rad, re.center_latitude_rad, I_max.gst_rad);
            re.sun_azimuth_rad = sdp.x;
            re.sun_altitude_rad = sdp.y;
        } else {
            Vec3 sdp = CD2DP(sun_llr, re.center_longitude_rad, re.center_latitude_rad, I_max.gst_rad);
            re.sun_azimuth_rad = sdp.x;
            re.sun_altitude_rad = sdp.y;
        }
    }

    // Path width and duration
    if (F.valid) {
        const double earth_r_km = kSxwnlEarthRadiusKm;
        const double sin_alt = std::sin(re.sun_altitude_rad);
        if (std::fabs(sin_alt) > 1e-12) {
            re.path_width_km = std::fabs(2.0 * Bp.r2 * earth_r_km) / sin_alt;
        }
        const SurfaceVelocity sv = Vxy(re.xc, re.yc, I_max.W_rad, re.vx, re.vy);
        if (sv.speed > 1e-14) {
            re.duration_seconds = 2.0 * std::fabs(Bp.r2) / sv.speed * 86400.0;
        }
    }

    return re;
}

// ---------------------------------------------------------------------------
// mDian: sunrise/sunset maximum eclipse line.
// Algorithm source: 寿星万年历 eph.js mDian().
// Returns true if a valid contact point was found.
// ---------------------------------------------------------------------------
MDianResult mDian(
    double Mx,
    double My,
    double vx0,
    double vy0,
    bool AB,
    double r,
    const BesselianFrame& I,
    double bba
) noexcept {
    MDianResult out{};
    double ax = Mx;
    double ay = My;
    OvlResult p;
    for (int i = 0; i < 2; ++i) {
        const SurfaceVelocity c = Vxy(ax, ay, I.W_rad, vx0, vy0);
        p = lineOvl(Mx, My, c.vy_relative, -c.vx_relative, 1.0, bba);
        if (p.n == 0) return out;
        if (AB) {
            ax = p.ax;
            ay = p.ay;
        } else {
            ax = p.bx;
            ay = p.by;
        }
    }
    const double R = AB ? p.r1 : p.r2;
    if (p.n > 0 && R <= r) {
        const GeoPoint a = bse2db(ax, ay, 0.0, I, true);
        if (a.valid) {
            out.found = true;
            out.longitude_rad = a.longitude_rad;
            out.latitude_rad = a.latitude_rad;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// mQie: boundary-line sample with endpoint stitching.
// Algorithm source: 寿星万年历 eph.js mQie().
// h == +1  -> northern limit
// h == -1  -> southern limit
// ---------------------------------------------------------------------------
MQieResult mQie(
    double Mx,
    double My,
    double Mz,
    double vx0,
    double vy0,
    int h,
    double r,
    const BesselianFrame& I,
    double k,
    double earth_axis_ratio,
    double bba,
    MQieState* state
) noexcept {
    // Algorithm source: 寿星万年历 eph.js mQie().
    MQieResult out{};
    const BoundaryPoint p = nanbei(Mx, My, Mz, vx0, vy0, h, r, I, k, earth_axis_ratio);

    if (state) {
        if (!state->f2) state->f2 = 0;
        state->f = p.valid ? 1 : 0;

        if (state->f2 != state->f) {
            const OvlResult g = lineOvl(p.x, p.y, vx0, vy0, 1.0, bba);
            if (g.n > 0) {
                const double dj = state->f ? g.r2 : g.r1;
                const double Fx = state->f ? g.bx : g.ax;
                const double Fy = state->f ? g.by : g.ay;
                const double v_mag = std::hypot(vx0, vy0);
                if (v_mag > 1e-14) {
                    const double gst_offset = dj / v_mag * 6.28;
                    BesselianFrame I2 = {I.J_rad, I.W_rad, I.gst_rad - gst_offset};
                    const GeoPoint gp = bse2db(Fx, Fy, 0.0, I2, true);
                    if (gp.valid) {
                        out.endpoint_valid = true;
                        out.endpoint_longitude_rad = gp.longitude_rad;
                        out.endpoint_latitude_rad = gp.latitude_rad;
                        out.endpoint_time_offset_days = -dj / v_mag;
                        out.endpoint_entering = state->f != 0;
                    }
                }
            }
        }
        state->f2 = state->f;
    }

    if (p.valid) {
        out.valid = true;
        out.longitude_rad = p.longitude_rad;
        out.latitude_rad = p.latitude_rad;
    }
    return out;
}

// ---------------------------------------------------------------------------
// JieX3Result: single-time boundary table row.
// Algorithm source: 寿星万年历 eph.js jieX3().
// ---------------------------------------------------------------------------
JieX3Row jieX3(
    double Mx,
    double My,
    double Mz,
    double vx,
    double vy,
    const ShadowRadii& B,
    const BesselianFrame& I,
    double k,
    double earth_axis_ratio,
    double bba
) noexcept {
    // Algorithm source: 寿星万年历 eph.js jieX3().
    JieX3Row out{};

    const double r = B.r1;

    BoundaryPoint pn = nanbei(Mx, My, Mz, vx, vy, +1, r, I, k, earth_axis_ratio);
    if (pn.valid) {
        out.penumbral_north_valid = true;
        out.penumbral_north_longitude_rad = pn.longitude_rad;
        out.penumbral_north_latitude_rad = pn.latitude_rad;
    }

    BoundaryPoint cn = nanbei(Mx, My, Mz, vx, vy, +1, B.r2, I, k, earth_axis_ratio);
    if (cn.valid) {
        out.core_north_valid = true;
        out.core_north_longitude_rad = cn.longitude_rad;
        out.core_north_latitude_rad = cn.latitude_rad;
    }

    GeoPoint center = bseXY2db(Mx, My, I, true);
    if (center.valid) {
        out.center_valid = true;
        out.center_longitude_rad = center.longitude_rad;
        out.center_latitude_rad = center.latitude_rad;
    }

    BoundaryPoint cs = nanbei(Mx, My, Mz, vx, vy, -1, B.r2, I, k, earth_axis_ratio);
    if (cs.valid) {
        out.core_south_valid = true;
        out.core_south_longitude_rad = cs.longitude_rad;
        out.core_south_latitude_rad = cs.latitude_rad;
    }

    BoundaryPoint ps = nanbei(Mx, My, Mz, vx, vy, -1, r, I, k, earth_axis_ratio);
    if (ps.valid) {
        out.penumbral_south_valid = true;
        out.penumbral_south_longitude_rad = ps.longitude_rad;
        out.penumbral_south_latitude_rad = ps.latitude_rad;
    }

    return out;
}

void push(const CurvePoint& z, CurveList* p) {
    if (!p || !std::isfinite(z.longitude_rad) || !std::isfinite(z.latitude_rad)) return;
    p->points.push_back(z);
}

void elmCpy(CurveList* a, int n, const CurveList& b, int m) {
    if (!a || b.points.empty()) return;
    int ai = n;
    if (ai == -2) {
        ai = static_cast<int>(a->points.size());
    } else if (ai == -1) {
        ai = static_cast<int>(a->points.size()) - 1;
    }
    int bi = m;
    if (bi == -2) {
        bi = static_cast<int>(b.points.size());
    } else if (bi == -1) {
        bi = static_cast<int>(b.points.size()) - 1;
    }
    if (bi < 0 || bi >= static_cast<int>(b.points.size())) return;
    if (ai < 0 || ai > static_cast<int>(a->points.size())) return;
    if (ai == static_cast<int>(a->points.size())) {
        a->points.push_back(b.points[static_cast<size_t>(bi)]);
    } else {
        a->points[static_cast<size_t>(ai)] = b.points[static_cast<size_t>(bi)];
    }
}

JieXResult jieX(
    const JieXContext& ctx,
    const FeatureResult& feature,
    size_t sample_count
) {
    // Algorithm source: 寿星万年历 eph.js jieX().
    JieXResult re{};
    re.feature = feature;
    if (!ctx.sample || !(feature.v > 0.0) || sample_count == 0) return re;

    double T = 1.7 * 1.7 - feature.d * feature.d;
    if (T < 0.0) T = 0.0;
    T = std::sqrt(T) / feature.v + 0.01;
    SplitJulianDate t = feature.maximum_jd_tt - T;
    const size_t N = sample_count;
    const double dt = 2.0 * T / static_cast<double>(N);
    int n1 = 0;
    int n4 = 0;

    CurveList* Ua = &re.q1;
    CurveList* Ub = &re.q2;
    MQieState L1_state{};
    MQieState L2_state{};
    MQieState L3_state{};
    MQieState L4_state{};
    MQieState L5_state{};
    MQieState L6_state{};
    auto push_refined_transition = [&] (
        const MQieResult& mq,
        SplitJulianDate sample_jd_tt,
        int side,
        int radius_kind,
        CurveList* curve
    ) {
        if (!mq.endpoint_valid || !curve) return;
        CurvePoint endpoint{
            mq.endpoint_longitude_rad,
            mq.endpoint_latitude_rad,
            sample_jd_tt + mq.endpoint_time_offset_days,
        };
        if (!ctx.refine_transition_endpoints || radius_kind != 1) {
            push(endpoint, curve);
            return;
        }
        SplitJulianDate left = sample_jd_tt - dt;
        SplitJulianDate right = sample_jd_tt;
        bool refined = false;
        for (int iteration = 0; iteration < 40; ++iteration) {
            const SplitJulianDate mid = left + 0.5 * (right - left);
            JieXSample transition_sample{};
            if (!ctx.sample(ctx.user_data, mid, &transition_sample)) break;
            const double transition_vx = feature.vx
                + feature.ax * (mid - feature.jd_suo_tt);
            const double transition_vy = feature.vy
                + feature.ay * (mid - feature.jd_suo_tt);
            const BoundaryPoint boundary = nanbei(
                transition_sample.M.x,
                transition_sample.M.y,
                transition_sample.M.z,
                transition_vx,
                transition_vy,
                side,
                transition_sample.B.r2,
                transition_sample.I,
                ctx.k,
                ctx.earth_axis_ratio);
            if (mq.endpoint_entering) {
                if (boundary.valid) {
                    right = mid;
                    endpoint = {boundary.longitude_rad, boundary.latitude_rad, mid};
                    refined = true;
                } else {
                    left = mid;
                }
            } else {
                if (boundary.valid) {
                    left = mid;
                    endpoint = {boundary.longitude_rad, boundary.latitude_rad, mid};
                    refined = true;
                } else {
                    right = mid;
                }
            }
        }
        if (!refined
            && !(endpoint.jd_tt >= sample_jd_tt - dt && endpoint.jd_tt <= sample_jd_tt)) {
            return;
        }

        CurvePoint adjacent{};
        bool has_adjacent = false;
        if (mq.endpoint_entering && mq.valid) {
            adjacent = {mq.longitude_rad, mq.latitude_rad, sample_jd_tt};
            has_adjacent = true;
        } else if (!mq.endpoint_entering && !curve->points.empty()) {
            adjacent = curve->points.back();
            has_adjacent = true;
        }
        if (!has_adjacent) {
            push(endpoint, curve);
            return;
        }

        const double max_segment_angle = 0.5 * M_PI / 180.0;
        auto angular_distance = [] (const CurvePoint& a, const CurvePoint& b) {
            const double longitude_delta = std::atan2(
                std::sin(b.longitude_rad - a.longitude_rad),
                std::cos(b.longitude_rad - a.longitude_rad));
            const double latitude_sine = std::sin(0.5 * (b.latitude_rad - a.latitude_rad));
            const double longitude_sine = std::sin(0.5 * longitude_delta);
            const double haversine = latitude_sine * latitude_sine
                + std::cos(a.latitude_rad) * std::cos(b.latitude_rad)
                    * longitude_sine * longitude_sine;
            return 2.0 * std::asin(
                std::min(1.0, std::sqrt(std::max(0.0, haversine))));
        };
        auto eval_intermediate = [&] (SplitJulianDate jd, CurvePoint* out_point) {
            if (!out_point) return false;
            JieXSample intermediate_sample{};
            if (!ctx.sample(ctx.user_data, jd, &intermediate_sample)) return false;
            const double intermediate_vx = feature.vx
                + feature.ax * (jd - feature.jd_suo_tt);
            const double intermediate_vy = feature.vy
                + feature.ay * (jd - feature.jd_suo_tt);
            const BoundaryPoint intermediate = nanbei(
                intermediate_sample.M.x,
                intermediate_sample.M.y,
                intermediate_sample.M.z,
                intermediate_vx,
                intermediate_vy,
                side,
                intermediate_sample.B.r2,
                intermediate_sample.I,
                ctx.k,
                ctx.earth_axis_ratio);
            if (!intermediate.valid) return false;
            *out_point = {intermediate.longitude_rad, intermediate.latitude_rad, jd};
            return true;
        };
        std::function<void(const CurvePoint&, const CurvePoint&, int)> append_between;
        append_between = [&] (const CurvePoint& start, const CurvePoint& end, int depth) {
            if (depth >= 14 || angular_distance(start, end) <= max_segment_angle) return;
            CurvePoint midpoint{};
            if (!eval_intermediate(
                    start.jd_tt + 0.5 * (end.jd_tt - start.jd_tt), &midpoint)) return;
            append_between(start, midpoint, depth + 1);
            push(midpoint, curve);
            append_between(midpoint, end, depth + 1);
        };

        const CurvePoint start = mq.endpoint_entering ? endpoint : adjacent;
        const CurvePoint end = mq.endpoint_entering ? adjacent : endpoint;
        if (mq.endpoint_entering) push(endpoint, curve);
        append_between(start, end, 0);
        if (!mq.endpoint_entering) push(endpoint, curve);
    };
    auto push_refined_center_transition = [&] (
        const GeoPoint& current_center,
        SplitJulianDate sample_jd_tt,
        bool entering
    ) {
        auto eval_center = [&] (SplitJulianDate jd, CurvePoint* out_point) {
            if (!out_point) return false;
            JieXSample center_sample{};
            if (!ctx.sample(ctx.user_data, jd, &center_sample)) return false;
            const GeoPoint point = bseXY2db(
                center_sample.M.x, center_sample.M.y, center_sample.I, true);
            if (!point.valid) return false;
            *out_point = {point.longitude_rad, point.latitude_rad, jd};
            return true;
        };

        CurvePoint endpoint{};
        if (entering) {
            endpoint = {
                current_center.longitude_rad,
                current_center.latitude_rad,
                sample_jd_tt,
            };
        } else if (!re.L0.points.empty()) {
            endpoint = re.L0.points.back();
        } else {
            return;
        }
        SplitJulianDate left = sample_jd_tt - dt;
        SplitJulianDate right = sample_jd_tt;
        for (int iteration = 0; iteration < 40; ++iteration) {
            const SplitJulianDate mid = left + 0.5 * (right - left);
            CurvePoint point{};
            const bool valid = eval_center(mid, &point);
            if (entering) {
                if (valid) {
                    right = mid;
                    endpoint = point;
                } else {
                    left = mid;
                }
            } else {
                if (valid) {
                    left = mid;
                    endpoint = point;
                } else {
                    right = mid;
                }
            }
        }

        CurvePoint adjacent{};
        if (entering) {
            adjacent = {
                current_center.longitude_rad,
                current_center.latitude_rad,
                sample_jd_tt,
            };
        } else {
            adjacent = re.L0.points.back();
        }
        auto angular_distance = [] (const CurvePoint& a, const CurvePoint& b) {
            const double longitude_delta = std::atan2(
                std::sin(b.longitude_rad - a.longitude_rad),
                std::cos(b.longitude_rad - a.longitude_rad));
            const double latitude_sine = std::sin(0.5 * (b.latitude_rad - a.latitude_rad));
            const double longitude_sine = std::sin(0.5 * longitude_delta);
            const double haversine = latitude_sine * latitude_sine
                + std::cos(a.latitude_rad) * std::cos(b.latitude_rad)
                    * longitude_sine * longitude_sine;
            return 2.0 * std::asin(
                std::min(1.0, std::sqrt(std::max(0.0, haversine))));
        };
        constexpr double kMaxCenterSegmentAngle = 0.5 * M_PI / 180.0;
        std::function<void(const CurvePoint&, const CurvePoint&, int)> append_between;
        append_between = [&] (const CurvePoint& start, const CurvePoint& end, int depth) {
            if (depth >= 14 || angular_distance(start, end) <= kMaxCenterSegmentAngle) return;
            CurvePoint midpoint{};
            if (!eval_center(
                    start.jd_tt + 0.5 * (end.jd_tt - start.jd_tt), &midpoint)) return;
            append_between(start, midpoint, depth + 1);
            push(midpoint, &re.L0);
            append_between(midpoint, end, depth + 1);
        };

        const CurvePoint start = entering ? endpoint : adjacent;
        const CurvePoint end = entering ? adjacent : endpoint;
        if (entering) push(endpoint, &re.L0);
        append_between(start, end, 0);
        if (!entering) push(endpoint, &re.L0);
    };
    push({0.0, 0.0, t}, &re.q2);
    push({0.0, 0.0, t}, &re.q3);
    push({0.0, 0.0, t}, &re.q4);

    for (size_t i = 0; i <= N; ++i, t += dt) {
        JieXSample sample{};
        if (!ctx.sample(ctx.user_data, t, &sample)) continue;
        const double vx = feature.vx + feature.ax * (t - feature.jd_suo_tt);
        const double vy = feature.vy + feature.ay * (t - feature.jd_suo_tt);
        const Vec3& M = sample.M;
        const ShadowRadii& B = sample.B;
        const BesselianFrame& I = sample.I;
        const double r = B.r1;

        OvlResult p = cirOvl(1.0, ctx.bba, r, M.x, M.y);
        if (n1 % 2) {
            if (!p.n) ++n1;
        } else {
            if (p.n) ++n1;
        }
        if (p.n) {
            GeoPoint a = bse2db(p.ax, p.ay, 0.0, I, true);
            GeoPoint b = bse2db(p.bx, p.by, 0.0, I, true);
            if (n1 == 1) {
                if (a.valid) push({a.longitude_rad, a.latitude_rad, t}, &re.p1);
                if (b.valid) push({b.longitude_rad, b.latitude_rad, t}, &re.p2);
            }
            if (n1 == 3) {
                if (a.valid) push({a.longitude_rad, a.latitude_rad, t}, &re.p3);
                if (b.valid) push({b.longitude_rad, b.latitude_rad, t}, &re.p4);
            }
        }

        MDianResult md = mDian(M.x, M.y, vx, vy, false, r, I, ctx.bba);
        if (!md.found) {
            if (!Ua->points.empty()) Ua = &re.q3;
        } else {
            push({md.longitude_rad, md.latitude_rad, t}, Ua);
        }
        md = mDian(M.x, M.y, vx, vy, true, r, I, ctx.bba);
        if (!md.found) {
            if (Ub->points.size() > 1) Ub = &re.q4;
        } else {
            push({md.longitude_rad, md.latitude_rad, t}, Ub);
        }
        if (t > feature.maximum_jd_tt) {
            if (Ua->points.empty()) Ua = &re.q3;
            if (Ub->points.size() == 1) Ub = &re.q4;
        }

        GeoPoint center = bseXY2db(M.x, M.y, I, true);
        if ((center.valid && n4 == 0) || (!center.valid && n4 == 1)) {
            if (ctx.refine_transition_endpoints) {
                push_refined_center_transition(center, t, center.valid);
            } else {
                const OvlResult ls0 = lineOvl(M.x, M.y, vx, vy, 1.0, ctx.bba);
                if (ls0.n) {
                    double dj;
                    double lx;
                    double ly;
                    if (n4 == 0) {
                        dj = ls0.r2;
                        lx = ls0.bx;
                        ly = ls0.by;
                    } else {
                        dj = ls0.r1;
                        lx = ls0.ax;
                        ly = ls0.ay;
                    }
                    const double v_mag = std::hypot(vx, vy);
                    if (v_mag > 1e-14) {
                        BesselianFrame I2 = {I.J_rad, I.W_rad, I.gst_rad - dj / v_mag * 6.28};
                        GeoPoint gp = bse2db(lx, ly, 0.0, I2, true);
                        if (gp.valid) push({gp.longitude_rad, gp.latitude_rad, t}, &re.L0);
                    }
                }
            }
            ++n4;
        }
        if (center.valid) push({center.longitude_rad, center.latitude_rad, t}, &re.L0);

        MQieResult mq = mQie(M.x, M.y, M.z, vx, vy, +1, r, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L1_state);
        push_refined_transition(mq, t, +1, 0, &re.L1);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L1);
        mq = mQie(M.x, M.y, M.z, vx, vy, -1, r, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L2_state);
        push_refined_transition(mq, t, -1, 0, &re.L2);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L2);
        mq = mQie(M.x, M.y, M.z, vx, vy, +1, B.r2, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L3_state);
        push_refined_transition(mq, t, +1, 1, &re.L3);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L3);
        mq = mQie(M.x, M.y, M.z, vx, vy, -1, B.r2, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L4_state);
        push_refined_transition(mq, t, -1, 1, &re.L4);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L4);
        mq = mQie(M.x, M.y, M.z, vx, vy, +1, (r + B.r2) / 2.0, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L5_state);
        push_refined_transition(mq, t, +1, 2, &re.L5);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L5);
        mq = mQie(M.x, M.y, M.z, vx, vy, -1, (r + B.r2) / 2.0, I, ctx.k, ctx.earth_axis_ratio, ctx.bba, &L6_state);
        push_refined_transition(mq, t, -1, 2, &re.L6);
        if (mq.valid) push({mq.longitude_rad, mq.latitude_rad, t}, &re.L6);
    }

    elmCpy(&re.q3, 0, re.q1, -1);
    elmCpy(&re.q4, 0, re.q2, -1);
    elmCpy(&re.q1, -2, re.L1, 0);
    elmCpy(&re.q2, -2, re.L2, 0);
    elmCpy(&re.q3, 0, re.L1, -1);
    elmCpy(&re.q4, 0, re.L2, -1);
    elmCpy(&re.q2, 0, re.q1, 0);
    elmCpy(&re.q3, -2, re.q4, -1);
    return re;
}

JieX2Result jieX2(const JieXContext& ctx, SplitJulianDate jd_tt) {
    // Algorithm source: 寿星万年历 eph.js jieX2().
    JieX2Result re{};
    if (!ctx.sample) return re;
    if (std::fabs(jd_tt - ctx.jd_suo_tt) > 0.5) return re;
    JieXSample sample{};
    if (!ctx.sample(ctx.user_data, jd_tt, &sample)) return re;
    const Vec3& M = sample.M;
    const ShadowRadii& B = sample.B;
    const BesselianFrame& I = sample.I;
    const double Z = M.z;
    const double a0 = M.x * M.x + M.y * M.y;
    const double a1 = a0 - B.r2 * B.r2;
    const double a2 = a0 - B.r1 * B.r1;
    const int N = 200;
    for (int i = 0; i < N; ++i) {
        const double s = static_cast<double>(i) / static_cast<double>(N) * kTwoPi;
        const double cosS = std::cos(s);
        const double sinS = std::sin(s);
        const double X = M.x + ctx.k * cosS;
        const double Y = M.y + ctx.k * sinS;

        double x = M.x + B.r2 * cosS;
        double y = M.y + B.r2 * sinS;
        GeoPoint p = lineEar2(X, Y, Z, x, y, 0.0, ctx.earth_axis_ratio, 1.0, I);
        if (p.valid) {
            push({p.longitude_rad, p.latitude_rad}, &re.p1);
        } else if (std::hypot(x, y) > a1) {
            p = bse2db(x, y, 0.0, I, true);
            if (p.valid) push({p.longitude_rad, p.latitude_rad}, &re.p1);
        }

        x = M.x + B.r1 * cosS;
        y = M.y + B.r1 * sinS;
        p = lineEar2(X, Y, Z, x, y, 0.0, ctx.earth_axis_ratio, 1.0, I);
        if (p.valid) {
            push({p.longitude_rad, p.latitude_rad}, &re.p2);
        } else if (std::hypot(x, y) > a2) {
            p = bse2db(x, y, 0.0, I, true);
            if (p.valid) push({p.longitude_rad, p.latitude_rad}, &re.p2);
        }

        Vec3 dawn = llrConv({s, 0.0, 0.0}, kPi / 2.0 - sample.sun.y);
        dawn.x = rad2rrad(dawn.x + sample.sun.x + kPi / 2.0 - I.gst_rad);
        push({dawn.x, dawn.y}, &re.p3);
    }
    if (!re.p1.points.empty()) push(re.p1.points.front(), &re.p1);
    if (!re.p2.points.empty()) push(re.p2.points.front(), &re.p2);
    if (!re.p3.points.empty()) push(re.p3.points.front(), &re.p3);
    return re;
}

// ---------------------------------------------------------------------------
// rsPL local north/south boundary functions (cone apex geometry)
// Algorithm source: 寿星万年历 eph.js rsPL.zb0/zbXY/p2p/pp0/nbj/lineEar
// ---------------------------------------------------------------------------

bool rspl_zb0(const RsplContext& ctx, SplitJulianDate jd_tt, RsplState* out) {
    // Algorithm source: 寿星万年历 eph.js rsPL.zb0().
    if (!out) return false;
    *out = RsplState{};
    if (!ctx.sun_llr || !ctx.moon_llr) return false;

    Vec3 S, M;
    if (!ctx.sun_llr(ctx.user_data, jd_tt, &S)) return false;
    if (!ctx.moon_llr(ctx.user_data, jd_tt, &M)) return false;
    out->S = S;
    out->M = M;

    const SplitJulianDate jd_ut = jd_tt;
    if (!ctx.gast || !ctx.gast(ctx.user_data, jd_ut, jd_tt, &out->gast_rad)) {
        out->gast_rad = 0.0;
    }

    const Vec3 s_xyz = llr_to_xyz(S.x, S.y, S.z);
    const Vec3 m_xyz = llr_to_xyz(M.x, M.y, M.z);

    const double k_ratio = 959.63 / (0.2725076 * kSxwnlEarthRadiusKm) * kSxwnlAuKm;

    {
        const double fx = (s_xyz.x - m_xyz.x) / (1.0 - k_ratio) + m_xyz.x;
        const double fy = (s_xyz.y - m_xyz.y) / (1.0 - k_ratio) + m_xyz.y;
        const double fz = (s_xyz.z - m_xyz.z) / (1.0 - k_ratio) + m_xyz.z;
        out->A.longitude_rad = std::atan2(fy, fx);
        out->A.latitude_rad = std::atan2(fz, std::hypot(fx, fy));
        out->A.radius = std::sqrt(fx * fx + fy * fy + fz * fz);
    }
    {
        const double fx = (s_xyz.x - m_xyz.x) / (1.0 + k_ratio) + m_xyz.x;
        const double fy = (s_xyz.y - m_xyz.y) / (1.0 + k_ratio) + m_xyz.y;
        const double fz = (s_xyz.z - m_xyz.z) / (1.0 + k_ratio) + m_xyz.z;
        out->B.longitude_rad = std::atan2(fy, fx);
        out->B.latitude_rad = std::atan2(fz, std::hypot(fx, fy));
        out->B.radius = std::sqrt(fx * fx + fy * fy + fz * fz);
    }
    return true;
}

void rspl_zbXY(RsplState* state, double longitude_rad, double latitude_rad) {
    // Algorithm source: 寿星万年历 eph.js rsPL.zbXY().
    if (!state) return;

    Vec3 s = state->S;
    Vec3 m = state->M;

    const double ha_s = state->gast_rad + longitude_rad - s.x;
    const double ha_m = state->gast_rad + longitude_rad - m.x;

    auto parallax = [&](Vec3& llr, double ha) {
        const double f = kSxwnlEarthAxisRatio;
        const double u = std::atan(f * std::tan(latitude_rad));
        const double g = llr.x + ha;
        const double r0 = kSxwnlEarthRadiusKm * std::cos(u);
        const double z0 = kSxwnlEarthRadiusKm * std::sin(u) * f;

        Vec3 xyz = llr_to_xyz(llr.x, llr.y, llr.z);
        xyz.x -= r0 * std::cos(g);
        xyz.y -= r0 * std::sin(g);
        xyz.z -= z0;
        llr = xyz_to_llr(xyz);
    };

    parallax(s, ha_s);
    parallax(m, ha_m);

    state->S = s;
    state->M = m;
}

RsplBoundary rspl_lineEar(const Vec3& point_llr, const ConeApex& apex, double gst_rad) {
    // Algorithm source: 寿星万年历 eph.js lineEar() (old interface).
    RsplBoundary out{};

    const Vec3 p_xyz = llr_to_xyz(point_llr.x, point_llr.y, point_llr.z);
    const Vec3 a_xyz = llr_to_xyz(apex.longitude_rad, apex.latitude_rad, apex.radius);

    const double dx = a_xyz.x - p_xyz.x;
    const double dy = a_xyz.y - p_xyz.y;
    const double dz = a_xyz.z - p_xyz.z;

    LineEllResult le = lineEll(
        p_xyz.x, p_xyz.y, p_xyz.z,
        a_xyz.x, a_xyz.y, a_xyz.z,
        kSxwnlEarthAxisRatio,
        kSxwnlEarthRadiusKm);

    if (le.valid) {
        const double fixed_x = le.x * std::cos(gst_rad) + le.y * std::sin(gst_rad);
        const double fixed_y = -le.x * std::sin(gst_rad) + le.y * std::cos(gst_rad);
        out.valid = true;
        out.longitude_rad = std::atan2(fixed_y, fixed_x);
        out.latitude_rad = std::atan(le.z / (kSxwnlEarthAxisRatio * kSxwnlEarthAxisRatio) / std::hypot(le.x, le.y));
    }
    return out;
}

RsplBoundary rspl_lineEar_llr(const Vec3& point_llr, const Vec3& target_llr, double gst_rad) {
    // Algorithm source: 寿星万年历 eph.js lineEar() (original llr-to-llr call).
    RsplBoundary out{};
    const Vec3 p_xyz = llr_to_xyz(point_llr.x, point_llr.y, point_llr.z);
    const Vec3 t_xyz = llr_to_xyz(target_llr.x, target_llr.y, target_llr.z);

    LineEllResult le = lineEll(
        p_xyz.x, p_xyz.y, p_xyz.z,
        t_xyz.x, t_xyz.y, t_xyz.z,
        kSxwnlEarthAxisRatio,
        kSxwnlEarthRadiusKm);

    if (le.valid) {
        const double fixed_x = le.x * std::cos(gst_rad) + le.y * std::sin(gst_rad);
        const double fixed_y = -le.x * std::sin(gst_rad) + le.y * std::cos(gst_rad);
        out.valid = true;
        out.longitude_rad = std::atan2(fixed_y, fixed_x);
        out.latitude_rad = std::atan(le.z / (kSxwnlEarthAxisRatio * kSxwnlEarthAxisRatio) / std::hypot(le.x, le.y));
    }
    return out;
}

RsplBoundary rspl_pp0(const RsplState& state) {
    // Algorithm source: 寿星万年历 eph.js rsPL.pp0().
    return rspl_lineEar_llr(state.M, state.S, state.gast_rad);
}

RsplBoundary rspl_p2p(const RsplContext& ctx, RsplState* state, double longitude_rad, double latitude_rad, bool use_umbra, int side, int iterations) {
    // Algorithm source: 寿星万年历 eph.js rsPL.p2p().
    RsplBoundary out{};
    if (!state) return out;

    double lon = longitude_rad;
    double lat = latitude_rad;

    for (int iter = 0; iter < iterations; ++iter) {
        RsplState local = *state;
        rspl_zbXY(&local, lon, lat);

        const double u = local.M.y - local.S.y;
        const double v = local.M.x - local.S.x;
        const double a = std::hypot(u, v);
        if (a < 1e-14) break;

        const double r_sun = 959.63 / local.S.z * kSxwnlAuKm / kSxwnlEarthRadiusKm / M_PI * 180.0;

        const double W = local.S.y + side * r_sun * v / a;
        const double J = local.S.x - side * r_sun * u / a / std::cos((W + local.S.y) / 2.0);
        const double R = local.S.z;

        Vec3 point = {J, W, R};
        const ConeApex& apex = use_umbra ? state->A : state->B;
        RsplBoundary pt = rspl_lineEar(point, apex, state->gast_rad);

        if (pt.valid) {
            out = pt;
            lon = pt.longitude_rad;
            lat = pt.latitude_rad;
        } else {
            break;
        }
    }
    return out;
}

RsplNbjResult rspl_nbj(const RsplContext& ctx, SplitJulianDate jd_tt, double longitude_rad, double latitude_rad) {
    // Algorithm source: 寿星万年历 eph.js rsPL.nbj().
    RsplNbjResult out{};

    RsplState state{};
    if (!rspl_zb0(ctx, jd_tt, &state)) return out;

    out.center = rspl_pp0(state);
    if (out.center.valid) {
        RsplState local = state;
        rspl_zbXY(&local, longitude_rad, latitude_rad);
        out.center_kind = local.M.z >= local.S.z ? 2 : 1;
    }

    out.umbra_north = rspl_p2p(ctx, &state, longitude_rad, latitude_rad, true, 1, 2);
    out.umbra_south = rspl_p2p(ctx, &state, longitude_rad, latitude_rad, true, -1, 2);
    out.penumbra_north = rspl_p2p(ctx, &state, longitude_rad, latitude_rad, false, -1, 3);
    out.penumbra_south = rspl_p2p(ctx, &state, longitude_rad, latitude_rad, false, 1, 3);

    if (out.umbra_north.valid && out.umbra_south.valid) {
        const double dlon = out.umbra_north.longitude_rad - out.umbra_south.longitude_rad;
        const double dlat = out.umbra_north.latitude_rad - out.umbra_south.latitude_rad;
        const double avg_lat = (out.umbra_north.latitude_rad + out.umbra_south.latitude_rad) / 2.0;
        out.umbra_width_km = kSxwnlEarthRadiusKm * std::sqrt(
            dlon * dlon * std::cos(avg_lat) * std::cos(avg_lat) + dlat * dlat);
    }

    return out;
}

double lineT_contact(double t, double x, double y, double v, double u, double r, int n) noexcept {
    const double b = y * v - x * u;
    const double A = u * u + v * v;
    const double B = u * b;
    const double C = b * b - r * r * v * v;
    const double D = B * B - A * C;
    if (D < 0.0) return 0.0;
    double sqrtD = std::sqrt(D);
    if (!n) sqrtD = -sqrtD;
    return t + ((-B + sqrtD) / A - x) / v;
}

}  // namespace solar
}  // namespace sxwnl
}  // namespace runtime
}  // namespace taiyin
