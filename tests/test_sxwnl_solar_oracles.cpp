#include "legacy/sxwnl/eclipse/solar_eclipse_sxwnl.h"

#include <cmath>
#include <cstdio>

namespace sx = taiyin::runtime::sxwnl::solar;

namespace {

constexpr double kCsREar = 6378.1366;
constexpr double kCsBa = 0.99664719;
constexpr double kCsK = 0.2725076;
constexpr double kCsK2 = 0.2722810;
constexpr double kCsK0 = 109.1222;

int g_failures = 0;

taiyin::SplitJulianDate split_jd(double value) {
    taiyin::SplitJulianDate result;
    taiyin::split_julian_date_from_double(value, &result);
    return result;
}

double scalar_jd(const taiyin::SplitJulianDate& value) {
    return taiyin::split_julian_date_to_double(value);
}

void expect_close(double actual, double expected, double tolerance, const char* label) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::printf("FAIL: %s actual=%.17g expected=%.17g diff=%.3g tolerance=%.3g\n",
                    label, actual, expected, actual - expected, tolerance);
        ++g_failures;
    }
}

void expect_close(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label
) {
    expect_close(scalar_jd(actual), expected, tolerance, label);
}

void expect_equal_int(int actual, int expected, const char* label) {
    if (actual != expected) {
        std::printf("FAIL: %s actual=%d expected=%d\n", label, actual, expected);
        ++g_failures;
    }
}

void expect_bool(bool actual, bool expected, const char* label) {
    if (actual != expected) {
        std::printf("FAIL: %s actual=%d expected=%d\n", label, actual ? 1 : 0, expected ? 1 : 0);
        ++g_failures;
    }
}

sx::BesselianFrame frame(double J, double W, double gst) {
    sx::BesselianFrame out{};
    out.J_rad = J;
    out.W_rad = W;
    out.gst_rad = gst;
    return out;
}

struct RsplMockData {
    sx::Vec3 S;
    sx::Vec3 M;
    double gast;
};

bool mock_sun_llr(void* user_data, taiyin::SplitJulianDate, sx::Vec3* out) {
    if (!user_data || !out) return false;
    *out = static_cast<RsplMockData*>(user_data)->S;
    return true;
}

bool mock_moon_llr(void* user_data, taiyin::SplitJulianDate, sx::Vec3* out) {
    if (!user_data || !out) return false;
    *out = static_cast<RsplMockData*>(user_data)->M;
    return true;
}

bool mock_gast(
    void* user_data,
    taiyin::SplitJulianDate,
    taiyin::SplitJulianDate,
    double* out
) {
    if (!user_data || !out) return false;
    *out = static_cast<RsplMockData*>(user_data)->gast;
    return true;
}

bool mock_bse_m(taiyin::SplitJulianDate jd_split, sx::Vec3* out) {
    if (!out) return false;
    const double jd = scalar_jd(jd_split);
    const double dt = jd - 2460409.25;
    *out = {0.18 + 0.42 * dt, -0.12 - 0.18 * dt, 58.0};
    return true;
}

bool mock_bse(taiyin::SplitJulianDate jd_split, sx::BesselianFrame* out) {
    if (!out) return false;
    const double jd = scalar_jd(jd_split);
    const double dt = jd - 2460409.25;
    *out = frame(1.2 + 0.01 * dt, 0.41 - 0.02 * dt, 0.8 + 0.03 * dt);
    return true;
}

bool mock_bse_m_feature(taiyin::SplitJulianDate jd_split, sx::Vec3* out) {
    if (!out) return false;
    const double jd = scalar_jd(jd_split);
    const double t = jd - 2460409.25;
    *out = {0.18 + 0.42 * t + 0.015 * t * t,
            -0.12 - 0.18 * t + 0.01 * t * t,
            58.0 + 0.2 * t};
    return true;
}

bool mock_sun_feature(taiyin::SplitJulianDate jd_split, sx::Vec3* out) {
    if (!out) return false;
    const double jd = scalar_jd(jd_split);
    const double t = jd - 2460409.25;
    *out = {1.05 + 0.02 * t, 0.12 - 0.01 * t, 1.0};
    return true;
}

bool mock_jiex_sample(void*, taiyin::SplitJulianDate jd, sx::JieXSample* out) {
    if (!out) return false;
    out->jd_tt = jd;
    out->M = {0.18, -0.12, 58.0};
    out->B.r1 = 0.5470676;
    out->B.r2 = 0.009160999999999975;
    out->B.ar2 = 0.009160999999999975;
    out->B.sf = 1.0232561551928188;
    out->I = frame(1.2, 0.41, 0.8);
    out->sun = {1.05, 0.12, 1.0};
    return true;
}

bool mock_jiex_sample_dynamic(void*, taiyin::SplitJulianDate jd, sx::JieXSample* out) {
    if (!out) return false;
    const double t = jd - split_jd(2460409.25);
    out->jd_tt = jd;
    out->M = {0.18 + 0.42 * t + 0.015 * t * t,
              -0.12 - 0.18 * t + 0.01 * t * t,
              58.0 + 0.2 * t};
    out->B = sx::rSM(out->M.z, kCsK, kCsK2, kCsK0, 0.0048, 0.0046, 23400.0);
    out->I = frame(1.2 + 0.01 * t, 0.41 - 0.02 * t, 0.8 + 0.03 * t);
    out->sun = {1.05 + 0.02 * t, 0.12 - 0.01 * t, 1.0};
    return true;
}

void test_line_ell() {
    {
        sx::LineEllResult r = sx::lineEll(0.2, -0.1, 2.0, 0.2, -0.1, -2.0, kCsBa, 1.0);
        expect_bool(r.valid, true, "lineEll[0] valid");
        expect_close(r.discriminant, 15.30244033006965, 1e-13, "lineEll[0] D");
        expect_close(r.x, 0.2, 1e-15, "lineEll[0] x");
        expect_close(r.y, -0.1, 1e-15, "lineEll[0] y");
        expect_close(r.z, 0.9714115195261748, 1e-14, "lineEll[0] z");
        expect_close(r.r1, 1.0285884804738252, 1e-14, "lineEll[0] R1");
        expect_close(r.r2, 2.9714115195261748, 1e-14, "lineEll[0] R2");
    }
    {
        sx::LineEllResult r = sx::lineEll(1.7, 0.2, 0.1, -1.2, 0.3, 0.5, kCsBa, 1.0);
        expect_bool(r.valid, true, "lineEll[1] valid");
        expect_close(r.discriminant, 7.066404104515218, 1e-13, "lineEll[1] D");
        expect_close(r.x, 0.9526309153823121, 1e-14, "lineEll[1] x");
        expect_close(r.y, 0.22577134774543753, 1e-14, "lineEll[1] y");
        expect_close(r.z, 0.20308539098175005, 1e-14, "lineEll[1] z");
        expect_close(r.r1, 0.7548849639784575, 1e-14, "lineEll[1] R1");
        expect_close(r.r2, 2.1742787391969043, 1e-14, "lineEll[1] R2");
    }
    {
        sx::LineEllResult r = sx::lineEll(2.0, 2.0, 2.0, 3.0, 2.5, 2.2, kCsBa, 1.0);
        expect_bool(r.valid, false, "lineEll[2] valid");
        expect_close(r.discriminant, -2.649409746750038, 1e-13, "lineEll[2] D");
    }
}

void test_line_ear2() {
    {
        sx::GeoPoint p = sx::lineEar2(0.15, -0.21, 2.0, 0.15, -0.21, 0.0, kCsBa, 1.0, frame(1.2, 0.41, 0.8));
        expect_bool(p.valid, true, "lineEar2[0] valid");
        expect_close(p.discriminant, 3.754575617691936, 1e-13, "lineEar2[0] D");
        expect_close(p.longitude_rad, -0.9163808793377655, 1e-14, "lineEar2[0] J");
        expect_close(p.latitude_rad, 0.9338978834336897, 1e-14, "lineEar2[0] W");
        expect_close(p.r1, 1.0361102051898943, 1e-14, "lineEar2[0] R1");
        expect_close(p.r2, 0.9638897948101057, 1e-14, "lineEar2[0] R2");
    }
    {
        sx::GeoPoint p = sx::lineEar2(1.4, 1.2, 2.0, 1.4, 1.2, 0.0, kCsBa, 1.0, frame(-2.2, -0.33, 0.1));
        expect_bool(p.valid, false, "lineEar2[1] valid");
        expect_close(p.discriminant, -9.661981690765344, 1e-13, "lineEar2[1] D");
    }
}

void test_line_ear() {
    {
        sx::RsplBoundary p = sx::rspl_lineEar_llr(
            {1.105, 0.118, 384400.0},
            {1.1, 0.12, 149747468.561691},
            1.4);
        expect_bool(p.valid, true, "lineEar[0] valid");
        expect_close(p.longitude_rad, 0.004702922050427993, 1e-13, "lineEar[0] J");
        expect_close(p.latitude_rad, -0.0066490067121262675, 1e-13, "lineEar[0] W");
    }
    {
        sx::RsplBoundary p = sx::rspl_lineEar_llr(
            {0.2, 0.1, 1000.0},
            {2.7, -0.2, 1000.0},
            0.3);
        expect_bool(p.valid, true, "lineEar[1] valid");
        expect_close(p.longitude_rad, -0.368887056988886, 1e-13, "lineEar[1] J");
        expect_close(p.latitude_rad, 0.15089206979849393, 1e-13, "lineEar[1] W");
    }
}

void test_overlap_and_bse() {
    sx::OvlResult c = sx::cirOvl(1.0, kCsBa, 0.35, 0.8, 0.15);
    expect_equal_int(c.n, 2, "cirOvl[0] n");
    expect_close(c.ax, 0.8690152763070201, 1e-14, "cirOvl[0] A.x");
    expect_close(c.ay, 0.4931280979987877, 1e-14, "cirOvl[0] A.y");
    expect_close(c.bx, 0.9894655419094583, 1e-14, "cirOvl[0] B.x");
    expect_close(c.by, -0.1442835510675974, 1e-14, "cirOvl[0] B.y");

    c = sx::cirOvl(1.0, kCsBa, 0.25, 1.8, 0.1);
    expect_equal_int(c.n, 0, "cirOvl[1] n");

    sx::OvlResult l = sx::lineOvl(0.2, -0.3, 0.6, 0.9, 1.0, kCsBa);
    expect_equal_int(l.n, 2, "lineOvl[0] n");
    expect_close(l.ax, 0.7992884589130218, 1e-14, "lineOvl[0] A.x");
    expect_close(l.ay, 0.5989326883695327, 1e-14, "lineOvl[0] A.y");
    expect_close(l.bx, -0.24429913315865548, 1e-14, "lineOvl[0] B.x");
    expect_close(l.by, -0.9664486997379833, 1e-14, "lineOvl[0] B.y");
    expect_close(l.r1, 1.080382633702347, 1e-14, "lineOvl[0] R1");
    expect_close(l.r2, 0.8009716531238675, 1e-14, "lineOvl[0] R2");

    sx::GeoPoint b = sx::bse2db(0.22, -0.31, 0.93, frame(1.2, 0.41, 0.8), true);
    expect_close(b.longitude_rad, -0.8467639579720414, 1e-14, "bse2db[0] lon");
    expect_close(b.latitude_rad, 0.8157668796727807, 1e-14, "bse2db[0] lat");

    b = sx::bseXY2db(0.15, -0.21, frame(1.2, 0.41, 0.8), true);
    expect_bool(b.valid, true, "bseXY2db[0] valid");
    expect_close(b.longitude_rad, -0.9163808793377655, 1e-14, "bseXY2db[0] lon");
    expect_close(b.latitude_rad, 0.9338978834336897, 1e-14, "bseXY2db[0] lat");
}

void test_vxy_rsm_llrconv() {
    sx::SurfaceVelocity v = sx::Vxy(0.15, -0.21, 0.41, 0.02, -0.03);
    expect_close(v.vx_surface, 3.6298107433916944, 1e-14, "Vxy[0] vx");
    expect_close(v.vy_surface, 0.8643660118244619, 1e-14, "Vxy[0] vy");
    expect_close(v.vx_relative, -3.6098107433916944, 1e-14, "Vxy[0] Vx");
    expect_close(v.vy_relative, -0.894366011824462, 1e-14, "Vxy[0] Vy");
    expect_close(v.speed, 3.7189547141922676, 1e-14, "Vxy[0] V");

    sx::ShadowRadii r = sx::rSM(57.2, kCsK, kCsK2, kCsK0, 0.0048, 0.0046, 23400.0);
    expect_close(r.r1, 0.5470676, 1e-15, "rSM[0] r1");
    expect_close(r.r2, 0.009160999999999975, 1e-15, "rSM[0] r2");
    expect_close(r.ar2, 0.009160999999999975, 1e-15, "rSM[0] ar2");
    expect_close(r.sf, 1.0232561551928188, 1e-14, "rSM[0] sf");

    sx::Vec3 llr = sx::llrConv({1.2, -0.4, 2.5}, 0.4091);
    expect_close(llr.x, 1.2304694279908381, 1e-14, "llrConv[0] lon");
    expect_close(llr.y, -0.015800513633645895, 1e-14, "llrConv[0] lat");
    expect_close(llr.z, 2.5, 1e-14, "llrConv[0] r");
}

void test_rspl_zbxy() {
    sx::RsplState state{};
    state.gast_rad = 1.4;
    state.S = {1.1, 0.12, 149747468.561691};
    state.M = {1.105, 0.118, 384400.0};
    sx::rspl_zbXY(&state, -1.3, 0.5);
    expect_close(state.S.x, 1.1000317058304512, 2e-11, "rspl_zbXY[0] S lon");
    expect_close(state.S.y, 0.11998226614122164, 2e-11, "rspl_zbXY[0] S lat");
    expect_close(state.S.z, 149744099.94833708, 2e-5, "rspl_zbXY[0] S r");
    expect_close(state.M.x, 1.1174852328953702, 2e-11, "rspl_zbXY[0] M lon");
    expect_close(state.M.y, 0.11099777210692721, 2e-11, "rspl_zbXY[0] M lat");
    expect_close(state.M.z, 381098.70583444345, 2e-7, "rspl_zbXY[0] M r");
}

void test_nanbei_mdian_mqie() {
    const sx::BesselianFrame I = frame(1.2, 0.41, 0.8);
    {
        sx::BoundaryPoint p = sx::nanbei(0.18, -0.12, 58.0, 0.42, -0.18, 1, 0.0092, I, kCsK, kCsBa);
        expect_bool(p.valid, true, "nanbei[0] valid");
        expect_close(p.longitude_rad, -0.8209276140592099, 1e-13, "nanbei[0] J");
        expect_close(p.latitude_rad, 1.0002428602571074, 1e-13, "nanbei[0] W");
        expect_close(p.x, 0.1837669238305215, 1e-14, "nanbei[0] x");
        expect_close(p.y, -0.12839346679596988, 1e-14, "nanbei[0] y");
    }
    {
        sx::BoundaryPoint p = sx::nanbei(0.18, -0.12, 58.0, 0.42, -0.18, -1, 0.0092, I, kCsK, kCsBa);
        expect_bool(p.valid, true, "nanbei[1] valid");
        expect_close(p.longitude_rad, -0.8273699575589886, 1e-13, "nanbei[1] J");
        expect_close(p.latitude_rad, 1.0277211505098431, 1e-13, "nanbei[1] W");
        expect_close(p.x, 0.17624311554881592, 1e-14, "nanbei[1] x");
        expect_close(p.y, -0.1116020348166683, 1e-14, "nanbei[1] y");
    }
    {
        sx::BoundaryPoint p = sx::nanbei(1.4, 1.2, 58.0, 0.42, -0.18, 1, 0.0092, frame(-2.2, -0.33, 0.1), kCsK, kCsBa);
        expect_bool(p.valid, false, "nanbei[2] valid");
        expect_close(p.x, 1.4068642910333364, 1e-14, "nanbei[2] x");
        expect_close(p.y, 1.206125480275836, 1e-14, "nanbei[2] y");
    }

    {
        sx::MDianResult p = sx::mDian(0.18, -0.12, 0.42, -0.18, true, 2.0, I, kCsBa);
        expect_bool(p.found, true, "mDian[0] found");
        expect_close(p.longitude_rad, -0.7302178261051706, 1e-13, "mDian[0] J");
        expect_close(p.latitude_rad, -0.3768593698879187, 1e-13, "mDian[0] W");
    }
    {
        sx::MDianResult p = sx::mDian(0.18, -0.12, 0.42, -0.18, false, 0.1, I, kCsBa);
        expect_bool(p.found, false, "mDian[1] found");
    }

    {
        sx::MQieState state{0, 0};
        sx::MQieResult p = sx::mQie(0.18, -0.12, 58.0, 0.42, -0.18, 1, 0.0092, I, kCsK, kCsBa, kCsBa, &state);
        expect_bool(p.valid, true, "mQie[0] valid");
        expect_bool(p.endpoint_valid, true, "mQie[0] endpoint valid");
        expect_equal_int(state.f, 1, "mQie[0] state f");
        expect_equal_int(state.f2, 1, "mQie[0] state f2");
        expect_close(p.longitude_rad, -0.8209276140592099, 1e-13, "mQie[0] J");
        expect_close(p.latitude_rad, 1.0002428602571074, 1e-13, "mQie[0] W");
        expect_close(p.endpoint_longitude_rad, 1.100198036095044, 1e-13, "mQie[0] endpoint J");
        expect_close(p.endpoint_latitude_rad, 0.14153174886680375, 1e-13, "mQie[0] endpoint W");
    }
    {
        sx::MQieState state{1, 1};
        sx::MQieResult p = sx::mQie(1.4, 1.2, 58.0, 0.42, -0.18, 1, 0.0092, frame(-2.2, -0.33, 0.1), kCsK, kCsBa, kCsBa, &state);
        expect_bool(p.valid, false, "mQie[1] valid");
        expect_bool(p.endpoint_valid, false, "mQie[1] endpoint valid");
        expect_equal_int(state.f, 0, "mQie[1] state f");
        expect_equal_int(state.f2, 0, "mQie[1] state f2");
    }
}

void test_jiex3() {
    sx::ShadowRadii B{};
    B.r1 = 0.5470676;
    B.r2 = 0.009160999999999975;

    {
        sx::JieX3Row row = sx::jieX3(0.18, -0.12, 58.0, 0.42, -0.18, B, frame(1.2, 0.41, 0.8), kCsK, kCsBa, kCsBa);
        expect_bool(row.penumbral_north_valid, true, "jieX3[0] penumbra north valid");
        expect_close(row.penumbral_north_longitude_rad, -0.38454906047939375, 1e-13, "jieX3[0] penumbra north J");
        expect_close(row.penumbral_north_latitude_rad, 0.4476710843048051, 1e-13, "jieX3[0] penumbra north W");
        expect_bool(row.core_north_valid, true, "jieX3[0] core north valid");
        expect_close(row.core_north_longitude_rad, -0.8209365344947952, 1e-13, "jieX3[0] core north J");
        expect_close(row.core_north_latitude_rad, 1.0002816121075169, 1e-13, "jieX3[0] core north W");
        expect_bool(row.center_valid, true, "jieX3[0] center valid");
        expect_close(row.center_longitude_rad, -0.8241156204847608, 1e-13, "jieX3[0] center J");
        expect_close(row.center_latitude_rad, 1.0139891510548116, 1e-13, "jieX3[0] center W");
        expect_bool(row.core_south_valid, true, "jieX3[0] core south valid");
        expect_close(row.core_south_longitude_rad, -0.8273607253996564, 1e-13, "jieX3[0] core south J");
        expect_close(row.core_south_latitude_rad, 1.0276825447178535, 1e-13, "jieX3[0] core south W");
        expect_bool(row.penumbral_south_valid, true, "jieX3[0] penumbra south valid");
        expect_close(row.penumbral_south_longitude_rad, 0.44425206788363525, 1e-13, "jieX3[0] penumbra south J");
        expect_close(row.penumbral_south_latitude_rad, 1.1736524924654512, 1e-13, "jieX3[0] penumbra south W");
    }

    {
        sx::JieX3Row row = sx::jieX3(1.4, 1.2, 58.0, 0.42, -0.18, B, frame(-2.2, -0.33, 0.1), kCsK, kCsBa, kCsBa);
        expect_bool(row.penumbral_north_valid, false, "jieX3[1] penumbra north valid");
        expect_bool(row.core_north_valid, false, "jieX3[1] core north valid");
        expect_bool(row.center_valid, false, "jieX3[1] center valid");
        expect_bool(row.core_south_valid, false, "jieX3[1] core south valid");
        expect_bool(row.penumbral_south_valid, false, "jieX3[1] penumbra south valid");
    }
}

void test_linet_contact() {
    {
        const double jd = sx::lineT_contact(2460409.25, 0.15, -0.08, 0.42, -0.18, 0.55, 0);
        expect_close(jd, 2460407.6760831643, 1e-11, "lineT[0] jd");
    }
    {
        const double jd = sx::lineT_contact(2460409.25, 0.15, -0.08, 0.42, -0.18, 0.55, 1);
        expect_close(jd, 2460410.0825375253, 1e-11, "lineT[1] jd");
    }
    {
        const double jd = sx::lineT_contact(2460409.25, 0.15, -0.08, 0.42, -0.18, 0.01, 0);
        expect_close(jd, 0.0, 0.0, "lineT[2] no solution");
    }
}

void test_rspl_pp0_p2p() {
    {
        sx::RsplState state{};
        state.gast_rad = 1.4;
        state.S = {1.1, 0.12, 149747468.561691};
        state.M = {1.105, 0.118, 384400.0};
        sx::RsplBoundary p = sx::rspl_pp0(state);
        expect_bool(p.valid, true, "rspl_pp0[0] valid");
        expect_close(p.longitude_rad, 0.004702922050427993, 1e-13, "rspl_pp0[0] J");
        expect_close(p.latitude_rad, -0.0066490067121262675, 1e-13, "rspl_pp0[0] W");
    }
    {
        sx::RsplState state{};
        state.gast_rad = 1.4;
        state.S = {1.1, 0.12, 149747468.561691};
        state.M = {1.105, 0.118, 384400.0};
        state.A = {1.102, 0.119, 224396806.03650004};
        state.B = {1.098, 0.121, 119678296.55280001};
        sx::RsplBoundary p = sx::rspl_p2p(sx::RsplContext{}, &state, -1.3, 0.5, true, 1, 2);
        expect_bool(p.valid, false, "rspl_p2p[0] valid");
    }
    {
        sx::RsplState state{};
        state.gast_rad = 1.4;
        state.S = {1.1, 0.12, 149747468.561691};
        state.M = {1.105, 0.118, 384400.0};
        state.A = {1.102, 0.119, 224396806.03650004};
        state.B = {1.098, 0.121, 119678296.55280001};
        sx::RsplBoundary p = sx::rspl_p2p(sx::RsplContext{}, &state, -1.3, 0.5, false, -1, 3);
        expect_bool(p.valid, false, "rspl_p2p[1] valid");
    }
}

void test_rad2rrad() {
    {
        const double r = sx::rad2rrad(0.0);
        expect_close(r, 0.0, 1e-15, "rad2rrad[0]");
    }
    {
        const double r = sx::rad2rrad(3.5);
        expect_close(r, -2.7831853071795862, 1e-14, "rad2rrad[1]");
    }
    {
        const double r = sx::rad2rrad(-2.0);
        expect_close(r, -2.0, 1e-15, "rad2rrad[2]");
    }
    {
        const double r = sx::rad2rrad(7.0);
        expect_close(r, 0.7168146928204138, 1e-14, "rad2rrad[3]");
    }
}

void test_llr_xyz() {
    {
        const sx::Vec3 v = sx::llr_to_xyz(1.2, -0.4, 2.5);
        expect_close(v.x, 0.834383983807346, 1e-14, "llr_to_xyz[0] x");
        expect_close(v.y, 2.1461621174262846, 1e-14, "llr_to_xyz[0] y");
        expect_close(v.z, -0.9735458557716263, 1e-14, "llr_to_xyz[0] z");
    }
    {
        const sx::Vec3 v = sx::llr_to_xyz(-2.5, 0.2, 1.0);
        expect_close(v.x, -0.7851740816484426, 1e-14, "llr_to_xyz[1] x");
        expect_close(v.y, -0.586542546205275, 1e-14, "llr_to_xyz[1] y");
        expect_close(v.z, 0.19866933079506122, 1e-14, "llr_to_xyz[1] z");
    }
    {
        const sx::Vec3 v = sx::llr_to_xyz(0.0, 0.0, 1.0);
        expect_close(v.x, 1.0, 1e-15, "llr_to_xyz[2] x");
        expect_close(v.y, 0.0, 1e-15, "llr_to_xyz[2] y");
        expect_close(v.z, 0.0, 1e-15, "llr_to_xyz[2] z");
    }
}

void test_xyz_llr() {
    {
        const sx::Vec3 v = sx::xyz_to_llr({0.5, -0.3, 2.1});
        expect_close(v.x, -0.5404195002705842, 1e-14, "xyz_to_llr[0] lon");
        expect_close(v.y, 1.2999547580272517, 1e-14, "xyz_to_llr[0] lat");
        expect_close(v.z, 2.179449471770337, 1e-14, "xyz_to_llr[0] r");
    }
    {
        const sx::Vec3 v = sx::xyz_to_llr({-1.0, 0.0, 0.0});
        expect_close(v.x, 3.141592653589793, 1e-15, "xyz_to_llr[1] lon");
        expect_close(v.y, 0.0, 1e-15, "xyz_to_llr[1] lat");
        expect_close(v.z, 1.0, 1e-15, "xyz_to_llr[1] r");
    }
    {
        const sx::Vec3 v = sx::xyz_to_llr({0.0, 0.0, 0.0});
        expect_close(v.x, 0.0, 1e-15, "xyz_to_llr[2] lon");
        expect_close(v.y, 0.0, 1e-15, "xyz_to_llr[2] lat");
        expect_close(v.z, 0.0, 1e-15, "xyz_to_llr[2] r");
    }
}

void test_cd2dp() {
    {
        const sx::Vec3 v = sx::CD2DP({1.5, 0.3, 1.0}, 0.5, 0.7, 2.1);
        expect_close(v.x, 4.650058726241285, 1e-14, "CD2DP[0] az");
        expect_close(v.y, 0.5489756911597727, 1e-14, "CD2DP[0] alt");
        expect_close(v.z, 1.0, 1e-15, "CD2DP[0] r");
    }
    {
        const sx::Vec3 v = sx::CD2DP({-0.8, -0.1, 1.0}, -1.2, 0.4, 0.3);
        expect_close(v.x, 2.9364830496833196, 1e-14, "CD2DP[1] az");
        expect_close(v.y, 1.0613283004403162, 1e-14, "CD2DP[1] alt");
        expect_close(v.z, 1.0, 1e-15, "CD2DP[1] r");
    }
}

void test_rspl_lineEar_cone() {
    {
        const sx::Vec3 point = {1.105, 0.118, 384400.0};
        const sx::ConeApex apex = {1.102, 0.119, 224396806.03650004};
        const sx::RsplBoundary p = sx::rspl_lineEar(point, apex, 1.4);
        expect_bool(p.valid, true, "rspl_lineEar[0] valid");
        expect_close(p.longitude_rad, -0.11686927726426088, 1e-13, "rspl_lineEar[0] J");
        expect_close(p.latitude_rad, 0.05706306403657893, 1e-13, "rspl_lineEar[0] W");
    }
    {
        const sx::Vec3 point = {0.2, 0.1, 1000.0};
        const sx::ConeApex apex = {2.7, -0.2, 1000.0};
        const sx::RsplBoundary p = sx::rspl_lineEar(point, apex, 0.3);
        expect_bool(p.valid, true, "rspl_lineEar[1] valid");
        expect_close(p.longitude_rad, -0.368887056988886, 1e-13, "rspl_lineEar[1] J");
        expect_close(p.latitude_rad, 0.15089206979849393, 1e-13, "rspl_lineEar[1] W");
    }
}

void test_rspl_zb0() {
    {
        RsplMockData data{{1.1, 0.12, 149747468.561691}, {1.105, 0.118, 384400.0}, 1.4};
        sx::RsplContext ctx{};
        ctx.user_data = &data;
        ctx.sun_llr = mock_sun_llr;
        ctx.moon_llr = mock_moon_llr;
        ctx.gast = mock_gast;
        sx::RsplState state{};
        expect_bool(
            sx::rspl_zb0(ctx, split_jd(2460409.25), &state),
            true,
            "rspl_zb0[0] succeeds");
        expect_close(state.S.x, 1.1, 1e-15, "rspl_zb0[0] S lon");
        expect_close(state.S.y, 0.12, 1e-15, "rspl_zb0[0] S lat");
        expect_close(state.S.z, 149747468.561691, 1e-6, "rspl_zb0[0] S r");
        expect_close(state.M.x, 1.105, 1e-15, "rspl_zb0[0] M lon");
        expect_close(state.M.y, 0.118, 1e-15, "rspl_zb0[0] M lat");
        expect_close(state.M.z, 384400.0, 1e-9, "rspl_zb0[0] M r");
        expect_close(state.gast_rad, 1.4, 1e-15, "rspl_zb0[0] gast");
        expect_close(state.A.longitude_rad, 1.105000023576846, 1e-14, "rspl_zb0[0] A lon");
        expect_close(state.A.latitude_rad, 0.11799999056008229, 1e-14, "rspl_zb0[0] A lat");
        expect_close(state.A.radius, 384398.19165952725, 1e-7, "rspl_zb0[0] A r");
        expect_close(state.B.longitude_rad, 1.1049999764233762, 1e-14, "rspl_zb0[0] B lon");
        expect_close(state.B.latitude_rad, 0.1180000094398286, 1e-14, "rspl_zb0[0] B lat");
        expect_close(state.B.radius, 384401.8083404292, 1e-7, "rspl_zb0[0] B r");
    }
    {
        RsplMockData data{{-2.4, -0.05, 148101891.98409}, {-2.392, -0.041, 405000.0}, 3.2};
        sx::RsplContext ctx{};
        ctx.user_data = &data;
        ctx.sun_llr = mock_sun_llr;
        ctx.moon_llr = mock_moon_llr;
        ctx.gast = mock_gast;
        sx::RsplState state{};
        expect_bool(
            sx::rspl_zb0(ctx, split_jd(2460409.25), &state),
            true,
            "rspl_zb0[1] succeeds");
        expect_close(state.S.x, -2.4, 1e-15, "rspl_zb0[1] S lon");
        expect_close(state.S.y, -0.05, 1e-15, "rspl_zb0[1] S lat");
        expect_close(state.S.z, 148101891.98409, 1e-6, "rspl_zb0[1] S r");
        expect_close(state.M.x, -2.392, 1e-15, "rspl_zb0[1] M lon");
        expect_close(state.M.y, -0.041, 1e-15, "rspl_zb0[1] M lat");
        expect_close(state.M.z, 405000.0, 1e-9, "rspl_zb0[1] M r");
        expect_close(state.gast_rad, 3.2, 1e-15, "rspl_zb0[1] gast");
        expect_close(state.A.longitude_rad, -2.39199996459553, 1e-14, "rspl_zb0[1] A lon");
        expect_close(state.A.latitude_rad, -0.040999960147958946, 1e-14, "rspl_zb0[1] A lat");
        expect_close(state.A.radius, 404998.21193614695, 1e-7, "rspl_zb0[1] A r");
        expect_close(state.B.longitude_rad, -2.3920000354041564, 1e-14, "rspl_zb0[1] B lon");
        expect_close(state.B.latitude_rad, -0.04100003985168814, 1e-14, "rspl_zb0[1] B lat");
        expect_close(state.B.radius, 405001.788063811, 1e-7, "rspl_zb0[1] B r");
    }
    {
        sx::RsplContext ctx{};
        sx::RsplState state{};
        expect_bool(
            sx::rspl_zb0(ctx, split_jd(2460409.25), &state),
            false,
            "rspl_zb0 rejects missing callbacks");
        expect_close(state.S.x, 0.0, 0.0, "rspl_zb0 clears failed state");
    }
}

void test_push_elmcpy() {
    {
        sx::CurveList arr1;
        sx::push({1.0, 2.0}, &arr1);
        sx::push({3.0, 4.0}, &arr1);
        sx::push({std::nan(""), 5.0}, &arr1);
        sx::CurveList arr2;
        sx::push({10.0, 20.0}, &arr2);
        sx::elmCpy(&arr2, 1, arr1, 0);
        sx::elmCpy(&arr2, 0, arr1, -1);
        expect_equal_int(static_cast<int>(arr1.points.size()), 2, "push_arr1 size");
        expect_equal_int(static_cast<int>(arr2.points.size()), 2, "push_arr2 size");
        expect_close(arr2.points[0].longitude_rad, 3.0, 1e-15, "elmCpy arr2[0] lon");
        expect_close(arr2.points[0].latitude_rad, 4.0, 1e-15, "elmCpy arr2[0] lat");
        expect_close(arr2.points[1].longitude_rad, 1.0, 1e-15, "elmCpy arr2[1] lon");
        expect_close(arr2.points[1].latitude_rad, 2.0, 1e-15, "elmCpy arr2[1] lat");
    }
}

void test_qrd() {
    sx::SolarContext ctx{};
    ctx.bba = kCsBa;
    ctx.k = kCsK;
    ctx.k2 = kCsK2;
    ctx.k0 = kCsK0;
    ctx.tanf1 = 0.0048;
    ctx.tanf2 = 0.0046;
    ctx.dyj = 23400.0;
    ctx.bse_m = mock_bse_m;
    ctx.bse = mock_bse;
    {
        const sx::QrdResult q = sx::qrd(ctx, split_jd(2460409.25), 0.42, -0.18, 1);
        expect_bool(q.valid, true, "qrd[0] valid");
        expect_close(q.longitude_rad, -0.2369590304448259, 1e-13, "qrd[0] lon");
        expect_close(q.latitude_rad, -0.06542785472872058, 1e-13, "qrd[0] lat");
        expect_close(q.jd_tt, 2460421.365737427, 1e-9, "qrd[0] jd");
    }
    {
        const sx::QrdResult q = sx::qrd(ctx, split_jd(2460409.3), 0.42, -0.18, 0);
        expect_bool(q.valid, true, "qrd[1] valid");
        expect_close(q.longitude_rad, -0.0916151719221805, 1e-13, "qrd[1] lon");
        expect_close(q.latitude_rad, -0.130840955437722, 1e-13, "qrd[1] lat");
        expect_close(q.jd_tt, 2460413.671018383, 1e-9, "qrd[1] jd");
    }
}

void test_rspl_nbj() {
    {
        sx::RsplContext ctx{};
        const sx::RsplNbjResult r = sx::rspl_nbj(ctx, split_jd(2460409.25), 0.0, 0.0);
        expect_bool(r.center.valid, false, "rspl_nbj rejects missing callbacks");
    }
    {
        RsplMockData data{{1.1, 0.12, 149747468.561691}, {1.105, 0.118, 384400.0}, 1.4};
        sx::RsplContext ctx{};
        ctx.user_data = &data;
        ctx.sun_llr = mock_sun_llr;
        ctx.moon_llr = mock_moon_llr;
        ctx.gast = mock_gast;
        const sx::RsplNbjResult r = sx::rspl_nbj(ctx, split_jd(2460409.25), -1.3, 0.5);
        expect_bool(r.center.valid, true, "rspl_nbj[0] center valid");
        expect_close(r.center.longitude_rad, 0.004702922050427993, 1e-13, "rspl_nbj[0] center lon");
        expect_close(r.center.latitude_rad, -0.0066490067121262675, 1e-13, "rspl_nbj[0] center lat");
        expect_equal_int(static_cast<int>(r.center_kind), 1, "rspl_nbj[0] center kind");
        expect_bool(r.umbra_north.valid, false, "rspl_nbj[0] un valid");
        expect_bool(r.umbra_south.valid, false, "rspl_nbj[0] us valid");
        expect_bool(r.penumbra_north.valid, false, "rspl_nbj[0] pn valid");
        expect_bool(r.penumbra_south.valid, false, "rspl_nbj[0] ps valid");
        expect_close(r.umbra_width_km, 0.0, 1e-15, "rspl_nbj[0] width");
    }
    {
        RsplMockData data{{-2.4, -0.05, 148101891.98409}, {-2.392, -0.041, 405000.0}, 3.2};
        sx::RsplContext ctx{};
        ctx.user_data = &data;
        ctx.sun_llr = mock_sun_llr;
        ctx.moon_llr = mock_moon_llr;
        ctx.gast = mock_gast;
        const sx::RsplNbjResult r = sx::rspl_nbj(ctx, split_jd(2460409.25), 2.1, -0.6);
        expect_bool(r.center.valid, true, "rspl_nbj[1] center valid");
        expect_close(r.center.longitude_rad, 1.3336974293479367, 1e-13, "rspl_nbj[1] center lon");
        expect_close(r.center.latitude_rad, 0.574338409441286, 1e-13, "rspl_nbj[1] center lat");
        expect_equal_int(static_cast<int>(r.center_kind), 1, "rspl_nbj[1] center kind");
        expect_bool(r.umbra_north.valid, false, "rspl_nbj[1] un valid");
        expect_bool(r.umbra_south.valid, false, "rspl_nbj[1] us valid");
        expect_bool(r.penumbra_north.valid, false, "rspl_nbj[1] pn valid");
        expect_bool(r.penumbra_south.valid, false, "rspl_nbj[1] ps valid");
        expect_close(r.umbra_width_km, 0.0, 1e-15, "rspl_nbj[1] width");
    }
}

void expect_curve_summary(const sx::CurveList& c, int size, double first_lon, double first_lat,
                          double mid_lon, double mid_lat, double last_lon, double last_lat,
                          const char* label) {
    expect_equal_int(static_cast<int>(c.points.size()), size, label);
    if (size == 0) return;
    const sx::CurvePoint& first = c.points.front();
    const sx::CurvePoint& mid = c.points[static_cast<size_t>(size / 2)];
    const sx::CurvePoint& last = c.points.back();
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s first lon", label);
    expect_close(first.longitude_rad, first_lon, 2e-12, buf);
    std::snprintf(buf, sizeof(buf), "%s first lat", label);
    expect_close(first.latitude_rad, first_lat, 2e-12, buf);
    std::snprintf(buf, sizeof(buf), "%s mid lon", label);
    expect_close(mid.longitude_rad, mid_lon, 2e-12, buf);
    std::snprintf(buf, sizeof(buf), "%s mid lat", label);
    expect_close(mid.latitude_rad, mid_lat, 2e-12, buf);
    std::snprintf(buf, sizeof(buf), "%s last lon", label);
    expect_close(last.longitude_rad, last_lon, 2e-12, buf);
    std::snprintf(buf, sizeof(buf), "%s last lat", label);
    expect_close(last.latitude_rad, last_lat, 2e-12, buf);
}

void test_jiex2() {
    sx::JieXContext ctx{};
    ctx.bba = kCsBa;
    ctx.k = kCsK;
    ctx.earth_axis_ratio = kCsBa;
    ctx.jd_suo_tt = split_jd(2460409.25);
    ctx.sample = mock_jiex_sample;
    {
        const sx::JieX2Result r = sx::jieX2(ctx, split_jd(2460409.25));
        expect_curve_summary(r.p1, 201, -0.7995334789371249, 1.0095085923071354,
                             -0.8490329029430899, 1.0181689886161747,
                             -0.7995334789371249, 1.0095085923071354, "jieX2[0] p1");
        expect_curve_summary(r.p2, 201, -0.08366880790632658, 0.6150652976726829,
                             -1.8204044147290808, 0.9293621704240873,
                             -0.08366880790632658, 0.6150652976726829, "jieX2[0] p2");
        expect_curve_summary(r.p3, 201, 1.8207963267948966, 0.0,
                             -1.3207963267948966, 0.0,
                             1.8207963267948966, 0.0, "jieX2[0] p3");
    }
    {
        const sx::JieX2Result r = sx::jieX2(ctx, split_jd(2460410.0));
        expect_equal_int(static_cast<int>(r.p1.points.size()), 0, "jieX2[1] p1 size");
        expect_equal_int(static_cast<int>(r.p2.points.size()), 0, "jieX2[1] p2 size");
        expect_equal_int(static_cast<int>(r.p3.points.size()), 0, "jieX2[1] p3 size");
    }
}

void test_feature() {
    sx::SolarContext ctx{};
    ctx.bba = kCsBa;
    ctx.k = kCsK;
    ctx.k2 = kCsK2;
    ctx.k0 = kCsK0;
    ctx.tanf1 = 0.0048;
    ctx.tanf2 = 0.0046;
    ctx.dyj = 23400.0;
    ctx.delta_t = 69.0 / 86400.0;
    ctx.obliquity_rad = 0.4091;
    ctx.bse_m = mock_bse_m_feature;
    ctx.bse = mock_bse;
    ctx.sun = mock_sun_feature;
    const sx::FeatureResult f = sx::feature(ctx, split_jd(2460409.25));
    expect_close(f.maximum_jd_tt, 2460408.784482759, 1e-9, "feature max jd");
    expect_close(f.vx, 0.42000000039115537, 1e-13, "feature vx");
    expect_close(f.vy, -0.1800000001676379, 1e-13, "feature vy");
    expect_close(f.ax, 0.0300000000558881, 1e-13, "feature ax");
    expect_close(f.ay, 0.020000000037247168, 1e-13, "feature ay");
    expect_close(f.v, 0.4569463867773988, 1e-13, "feature v");
    expect_close(f.k, -0.42857142857142827, 1e-13, "feature k");
    expect_close(f.xc, -0.015517241379310376, 1e-13, "feature xc");
    expect_close(f.yc, -0.03620689655172418, 1e-13, "feature yc");
    expect_close(f.zc, 57.61000891850782, 1e-13, "feature zc");
    expect_close(f.D, -0.03939192985791682, 1e-13, "feature D");
    expect_close(f.d, 0.03939192985791682, 1e-13, "feature d");
    expect_bool(f.center_valid, true, "feature center valid");
    expect_close(f.center_longitude_rad, -1.1968355598259142, 1e-13, "feature center lon");
    expect_close(f.center_latitude_rad, 1.1175711412246077, 1e-13, "feature center lat");
    expect_close(f.center_r2, 0.9965161205874615, 1e-13, "feature center r2");
    expect_close(f.Bc.r1, 0.5490356428088375, 1e-13, "feature Bc r1");
    expect_close(f.Bc.r2, 0.007274958974864021, 1e-13, "feature Bc r2");
    expect_close(f.Bc.sf, 1.0159914262710394, 1e-13, "feature Bc sf");
    expect_close(f.Bp.r1, 0.5442523654300178, 1e-13, "feature Bp r1");
    expect_close(f.Bp.r2, 0.011858933129566374, 1e-13, "feature Bp r2");
    expect_close(f.Bp.sf, 1.0338310839963434, 1e-13, "feature Bp sf");
    expect_bool(f.gk1.valid, true, "feature gk1 valid");
    expect_close(f.gk1.longitude_rad, -3.113044861554103, 1e-13, "feature gk1 lon");
    expect_close(f.gk1.latitude_rad, 0.20698187555681555, 1e-13, "feature gk1 lat");
    expect_close(f.gk1.jd_tt, 2460406.4455868322, 1e-9, "feature gk1 jd");
    expect_bool(f.gk2.valid, true, "feature gk2 valid");
    expect_close(f.gk2.longitude_rad, -0.007315552170515716, 1e-13, "feature gk2 lon");
    expect_close(f.gk2.latitude_rad, -0.14458825494619837, 1e-13, "feature gk2 lat");
    expect_close(f.gk2.jd_tt, 2460410.9088160354, 1e-9, "feature gk2 jd");
    expect_bool(f.gk3.valid, true, "feature gk3 valid");
    expect_close(f.gk3.longitude_rad, -3.046347216340944, 1e-13, "feature gk3 lon");
    expect_close(f.gk3.latitude_rad, 0.2074326256669488, 1e-13, "feature gk3 lat");
    expect_close(f.gk3.jd_tt, 2460404.8022295237, 1e-9, "feature gk3 jd");
    expect_bool(f.gk4.valid, true, "feature gk4 valid");
    expect_close(f.gk4.longitude_rad, -0.042309672949975674, 1e-13, "feature gk4 lon");
    expect_close(f.gk4.latitude_rad, -0.1338663913530367, 1e-13, "feature gk4 lat");
    expect_close(f.gk4.jd_tt, 2460412.408931058, 1e-9, "feature gk4 jd");
    expect_bool(f.gk5.valid, true, "feature gk5 valid");
    expect_close(f.gk5.longitude_rad, -1.162224898231451, 1e-13, "feature gk5 lon");
    expect_close(f.gk5.latitude_rad, 1.1119117740452271, 1e-13, "feature gk5 lat");
    expect_close(f.gk5.jd_tt, 2460408.821428572, 1e-9, "feature gk5 jd");
    expect_close(f.magnitude, 1.0338310839963434, 1e-13, "feature magnitude");
    expect_equal_int(static_cast<int>(f.type), static_cast<int>(sx::EclipseCentralTotal), "feature type");
    expect_close(f.sun_azimuth_rad, 1.6232606807141368, 1e-13, "feature sun az");
    expect_close(f.sun_altitude_rad, 0.1642289600522087, 1e-13, "feature sun alt");
    expect_close(f.path_width_km, 925.2810854873867, 1e-9, "feature width");
    expect_close(f.duration_seconds, 873.6300366090836, 1e-9, "feature duration");
}

void expect_empty_curve(const sx::CurveList& c, const char* label) {
    expect_equal_int(static_cast<int>(c.points.size()), 0, label);
}

void expect_empty_jiex(const sx::JieXResult& r, const char* prefix) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s p1", prefix);
    expect_empty_curve(r.p1, buf);
    std::snprintf(buf, sizeof(buf), "%s p2", prefix);
    expect_empty_curve(r.p2, buf);
    std::snprintf(buf, sizeof(buf), "%s p3", prefix);
    expect_empty_curve(r.p3, buf);
    std::snprintf(buf, sizeof(buf), "%s p4", prefix);
    expect_empty_curve(r.p4, buf);
    std::snprintf(buf, sizeof(buf), "%s q1", prefix);
    expect_empty_curve(r.q1, buf);
    std::snprintf(buf, sizeof(buf), "%s q2", prefix);
    expect_empty_curve(r.q2, buf);
    std::snprintf(buf, sizeof(buf), "%s q3", prefix);
    expect_empty_curve(r.q3, buf);
    std::snprintf(buf, sizeof(buf), "%s q4", prefix);
    expect_empty_curve(r.q4, buf);
    std::snprintf(buf, sizeof(buf), "%s L0", prefix);
    expect_empty_curve(r.L0, buf);
    std::snprintf(buf, sizeof(buf), "%s L1", prefix);
    expect_empty_curve(r.L1, buf);
    std::snprintf(buf, sizeof(buf), "%s L2", prefix);
    expect_empty_curve(r.L2, buf);
    std::snprintf(buf, sizeof(buf), "%s L3", prefix);
    expect_empty_curve(r.L3, buf);
    std::snprintf(buf, sizeof(buf), "%s L4", prefix);
    expect_empty_curve(r.L4, buf);
    std::snprintf(buf, sizeof(buf), "%s L5", prefix);
    expect_empty_curve(r.L5, buf);
    std::snprintf(buf, sizeof(buf), "%s L6", prefix);
    expect_empty_curve(r.L6, buf);
}

void test_jiex_guard() {
    sx::FeatureResult feature{};
    feature.v = 0.4569463867773988;
    sx::JieXContext no_sample{};
    const sx::JieXResult no_sample_result = sx::jieX(no_sample, feature);
    expect_empty_jiex(no_sample_result, "jieX guard no sample");

    sx::JieXContext ctx{};
    ctx.sample = mock_jiex_sample;
    sx::FeatureResult zero_velocity{};
    zero_velocity.v = 0.0;
    const sx::JieXResult zero_velocity_result = sx::jieX(ctx, zero_velocity);
    expect_empty_jiex(zero_velocity_result, "jieX guard zero velocity");
}

void test_jiex_full_summary() {
    sx::SolarContext solar{};
    solar.bba = kCsBa;
    solar.k = kCsK;
    solar.k2 = kCsK2;
    solar.k0 = kCsK0;
    solar.tanf1 = 0.0048;
    solar.tanf2 = 0.0046;
    solar.dyj = 23400.0;
    solar.delta_t = 69.0 / 86400.0;
    solar.obliquity_rad = 0.4091;
    solar.bse_m = mock_bse_m_feature;
    solar.bse = mock_bse;
    solar.sun = mock_sun_feature;
    const sx::FeatureResult feat = sx::feature(solar, split_jd(2460409.25));

    sx::JieXContext ctx{};
    ctx.bba = kCsBa;
    ctx.k = kCsK;
    ctx.earth_axis_ratio = kCsBa;
    ctx.jd_suo_tt = split_jd(2460409.25);
    ctx.sample = mock_jiex_sample_dynamic;
    const sx::JieXResult r = sx::jieX(ctx, feat);

    expect_curve_summary(r.p1, 144, -3.0934711129900929, 0.2245599144384394,
                         -2.6249471236807569, -0.030389405355295385,
                         -2.9753771163826368, 0.12310262415414577, "jieX p1");
    expect_curve_summary(r.p2, 144, 3.070017720084115, 0.27801962389194185,
                         2.6084684839646419, 0.40099080481309396,
                         3.1097804872767494, 0.20830330566316169, "jieX p2");
    expect_curve_summary(r.p3, 121, 0.066521643511372019, -0.13413209726104561,
                         0.51533513514946616, 0.058805406272784307,
                         0.04756829837263421, -0.10879823642195723, "jieX p3");
    expect_curve_summary(r.p4, 121, -0.19480665188959012, -0.23034893028344475,
                         -0.5653695051900085, -0.30996842863816021,
                         -0.027721550708903919, -0.1349194092815085, "jieX p4");
    expect_curve_summary(r.q1, 143, -3.1401205315432055, 0.24597738582683809,
                         -3.1257358817717709, 0.2134728930093846,
                         3.1237583639793742, 0.21906598164322325, "jieX q1");
    expect_curve_summary(r.q2, 130, -3.1401205315432055, 0.24597738582683809,
                         -3.1240226831314555, 0.21521435260449678,
                         -2.5724605565712926, 0.29824601910753951, "jieX q2");
    expect_curve_summary(r.q3, 123, 0.075675605670244117, -0.14108824588985411,
                         -0.010053338085126651, -0.14585175661494165,
                         -0.023864990403845265, -0.13360271042165808, "jieX q3");
    expect_curve_summary(r.q4, 117, 0.23187992120491741, -0.068677061612295323,
                         -0.010564472804158953, -0.14545358628555058,
                         -0.023864990403845265, -0.13360271042165808, "jieX q4");
    expect_curve_summary(r.L0, 243, -3.1181604398641518, 0.21271251920947393,
                         -1.295657213339138, 1.1354576743067466,
                         0.09370904483524356, -0.14547915610692685, "jieX L0");
    expect_curve_summary(r.L1, 111, 3.1237583639793742, 0.21906598164322325,
                         -0.5419876204965286, 0.70386897390173864,
                         0.075675605670244117, -0.14108824588985411, "jieX L1");
    expect_curve_summary(r.L2, 363, -2.5724605565712926, 0.29824601910753951,
                         0.95800648480880479, 1.3861117219874868,
                         0.23187992120491741, -0.068677061612295323, "jieX L2");
    expect_curve_summary(r.L3, 241, -3.1249551083422915, 0.21231405557831007,
                         -1.2941454186765613, 1.1248970087733332,
                         0.027527949549637221, -0.1456371362731072, "jieX L3");
    expect_curve_summary(r.L4, 244, -3.1107657525708614, 0.21314240582786378,
                         -1.2972547680313748, 1.1460159513910653,
                         0.043508691347880823, -0.14545148966959429, "jieX L4");
    expect_curve_summary(r.L5, 176, -3.1232114255042882, 0.20102885891985295,
                         -1.1530065746954818, 0.84981747631960824,
                         0.072300693678518435, -0.14856753096634115, "jieX L5");
    expect_curve_summary(r.L6, 308, -3.012735463526961, 0.23441959468330872,
                         -0.64372415120991544, 1.2907715207819783,
                         0.069942719815465626, -0.11857930245773841, "jieX L6");
}

}  // namespace

int main() {
    test_line_ell();
    test_line_ear2();
    test_line_ear();
    test_overlap_and_bse();
    test_vxy_rsm_llrconv();
    test_rspl_zbxy();
    test_nanbei_mdian_mqie();
    test_jiex3();
    test_linet_contact();
    test_rspl_pp0_p2p();
    test_rad2rrad();
    test_llr_xyz();
    test_xyz_llr();
    test_cd2dp();
    test_rspl_lineEar_cone();
    test_rspl_zb0();
    test_push_elmcpy();
    test_qrd();
    test_rspl_nbj();
    test_jiex2();
    test_feature();
    test_jiex_guard();
    test_jiex_full_summary();
    if (g_failures != 0) {
        std::printf("sxwnl solar oracle failures: %d\n", g_failures);
        return 1;
    }
    std::printf("sxwnl solar oracle tests passed\n");
    return 0;
}
