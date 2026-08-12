#include "legacy/sxwnl/eclipse/lunar_eclipse_sxwnl.h"

#include <cmath>
#include <cstdio>

namespace sx = taiyin::runtime::sxwnl::lunar;

namespace {

int g_failures = 0;

void expect_close(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::printf("FAIL: %s actual=%.17g expected=%.17g diff=%.3g tolerance=%.3g\n",
                    label, actual, expected, actual - expected, tolerance);
        ++g_failures;
    }
}

void expect_nan(double actual, const char* label) {
    if (!std::isnan(actual)) {
        std::printf("FAIL: %s actual=%.17g expected=nan\n", label, actual);
        ++g_failures;
    }
}

sx::LecInput input(
    double moon_lon,
    double moon_lat,
    double moon_dist_au,
    double sun_lon,
    double sun_lat,
    double sun_dist_au,
    double shadow_earth_scale,
    double shadow_sun_scale,
    double shadow_parallax_scale
) {
    return {moon_lon,
            moon_lat,
            moon_dist_au,
            sun_lon,
            sun_lat,
            sun_dist_au,
            6378.137,
            695700.0,
            1737.4,
            1737.4,
            1737.4,
            shadow_earth_scale,
            shadow_sun_scale,
            shadow_parallax_scale};
}

void expect_lec_geometry(const sx::LecGeometry& g, const sx::LecGeometry& e, const char* label) {
    char buf[128];
#define EXPECT_FIELD(field) \
    std::snprintf(buf, sizeof(buf), "%s.%s", label, #field); \
    expect_close(g.field, e.field, 1e-14, buf)
    EXPECT_FIELD(x_rad);
    EXPECT_FIELD(y_rad);
    EXPECT_FIELD(rmin_rad);
    EXPECT_FIELD(moon_radius_rad);
    EXPECT_FIELD(moon_radius_toward_shadow_rad);
    EXPECT_FIELD(moon_radius_away_from_shadow_rad);
    EXPECT_FIELD(umbra_radius_rad);
    EXPECT_FIELD(penumbra_radius_rad);
    EXPECT_FIELD(moon_dist_au);
    EXPECT_FIELD(sun_dist_au);
#undef EXPECT_FIELD
}

sx::LecGeometry geometry(
    double x,
    double y,
    double rmin,
    double moon_radius,
    double umbra_radius,
    double penumbra_radius,
    double moon_dist,
    double sun_dist
) {
    return {x, y, rmin, moon_radius, moon_radius, moon_radius,
            umbra_radius, penumbra_radius, moon_dist, sun_dist};
}

void test_lineT() {
    expect_close(sx::lineT(0.12, -0.08, -0.65, 0.21, 0.37, 0),
                 -0.33543750709237763, 1e-14, "lineT[0]");
    expect_close(sx::lineT(0.12, -0.08, -0.65, 0.21, 0.37, 1),
                 0.7417812704871483, 1e-14, "lineT[1]");
    expect_nan(sx::lineT(0.9, 0.7, 0.1, 0.2, 0.1, 0), "lineT[2]");
}

void test_lecXY() {
    sx::LecGeometry out{};
    sx::lecXY(input(3.2201, -0.0043, 0.002569555, 0.0784, 0.00012, 1.0032, 1.01, 1.0, 1.0), &out);
    expect_lec_geometry(out,
                        geometry(0.00010734614806184882,
                                 -0.00418,
                                 0.004181378145480712,
                                 0.004519740804790809,
                                 0.012164640250493264,
                                 0.021433224166694468,
                                 0.002569555,
                                 1.0032),
                        "lecXY[0]");

    sx::lecXY(input(0.02, 0.012, 0.00271, 3.14, -0.004, 0.985, 1.0, 1.0, 1.0), &out);
    expect_lec_geometry(out,
                        geometry(0.021591962628563274,
                                 0.008,
                                 0.023026351212323524,
                                 0.004285509434697878,
                                 0.011054098374603588,
                                 0.02049425198458518,
                                 0.00271,
                                 0.985),
                        "lecXY[1]");

    sx::lecXY(input(-2.9, 0.031, 0.00245, 0.24, -0.02, 1.015, 1.0, 1.0008, 0.998), &out);
    expect_lec_geometry(out,
                        geometry(0.0015921358063533566,
                                 0.011,
                                 0.011114625338978928,
                                 0.00474029170502435,
                                 0.012857933488407532,
                                 0.02202589262276697,
                                 0.00245,
                                 1.015),
                        "lecXY[2]");
}

void test_lecMax() {
    const sx::LecGeometry z1 = geometry(0.00010734614806184882,
                                        -0.00418,
                                        0.004181378145480712,
                                        0.004519740804790809,
                                        0.012164640250493264,
                                        0.021433224166694468,
                                        0.002569555,
                                        1.0032);
    const sx::LecGeometry z2 = geometry(0.000557345175400288,
                                        -0.0039900000000000005,
                                        0.004028738468123984,
                                        0.004519661654118948,
                                        0.012164346813670806,
                                        0.02143293082124382,
                                        0.0025696,
                                        1.0032);
    sx::LecMaxResult out{};
    sx::lecMax(z1, z2, 60.0 / 86400.0, &out);
    expect_lec_geometry(out.geometry, z1, "lecMax[0].geometry");
    expect_close(out.vx_rad_per_day, 0.6479985993673525, 1e-14, "lecMax[0].vx");
    expect_close(out.vy_rad_per_day, 0.27359999999999884, 1e-14, "lecMax[0].vy");
    expect_close(out.dt_days, 0.0021709307604240264, 1e-14, "lecMax[0].dt");

    const sx::LecGeometry a = geometry(0.021591962628563274,
                                       0.008,
                                       0.023026351212323524,
                                       0.004285509434697878,
                                       0.011054098374603588,
                                       0.02049425198458518,
                                       0.00271,
                                       0.985);
    const sx::LecGeometry b = geometry(0.021291937734790763,
                                       0.008199999999999999,
                                       0.02281636720650785,
                                       0.004285193188150016,
                                       0.011052937528275474,
                                       0.020493091483948848,
                                       0.0027102,
                                       0.985);
    sx::lecMax(a, b, 120.0 / 86400.0, &out);
    expect_lec_geometry(out.geometry, a, "lecMax[1].geometry");
    expect_close(out.vx_rad_per_day, -0.2160179235162074, 1e-14, "lecMax[1].vx");
    expect_close(out.vy_rad_per_day, 0.14399999999999913, 1e-14, "lecMax[1].vy");
    expect_close(out.dt_days, 0.05211074643203852, 1e-14, "lecMax[1].dt");
}

}  // namespace

int main() {
    test_lineT();
    test_lecXY();
    test_lecMax();

    if (g_failures != 0) {
        std::printf("sxwnl lunar oracle failures: %d\n", g_failures);
        return 1;
    }
    std::printf("sxwnl lunar oracle tests passed\n");
    return 0;
}
