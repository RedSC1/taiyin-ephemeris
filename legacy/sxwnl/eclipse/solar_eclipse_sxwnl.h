#ifndef TAIYIN_RUNTIME_SOLAR_ECLIPSE_SXWNL_H
#define TAIYIN_RUNTIME_SOLAR_ECLIPSE_SXWNL_H

#include "taiyin/time.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace taiyin {
namespace runtime {
namespace sxwnl {
namespace solar {

extern const double kPi;
extern const double kTwoPi;

double rad2rrad(double x) noexcept;

struct Vec3 {
    double x;
    double y;
    double z;
};

struct LineEllResult {
    bool valid;
    double discriminant;
    double x;
    double y;
    double z;
    double r1;
    double r2;
};

struct GeoPoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    double x;
    double y;
    double z;
    double r1;
    double r2;
    double discriminant;
};

struct BesselianFrame {
    double J_rad;   // eph.js I[0]
    double W_rad;   // eph.js I[1]
    double gst_rad; // eph.js I[2]
};

struct OvlResult {
    int n;
    double ax;
    double ay;
    double bx;
    double by;
    double r1;
    double r2;
};

struct ShadowRadii {
    double r1;
    double r2;
    double ar2;
    double sf;
};

struct SurfaceVelocity {
    double vx_surface;
    double vy_surface;
    double vx_relative;
    double vy_relative;
    double speed;
};

struct BoundaryPoint {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    double x;
    double y;
};

struct SolarContext {
    double bba;
    double k;
    double k2;
    double k0;
    double tanf1;
    double tanf2;
    double dyj;
    double delta_t;
    double obliquity_rad;
    bool (*bse_m)(SplitJulianDate jd, Vec3* out);
    bool (*bse)(SplitJulianDate jd, BesselianFrame* out);
    bool (*sun)(SplitJulianDate jd, Vec3* out_llr);
};

struct QrdResult {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    SplitJulianDate jd_tt;
};

enum EclipseType {
    EclipseNone,
    EclipsePartial,
    EclipsePartialNoCenter,
    EclipseCentralAnnular,
    EclipseCentralTotal,
    EclipseCentralAnnularTotal,
    EclipseCentralAnnularTotalBeginTotal,
    EclipseCentralAnnularTotalEndTotal,
    EclipseCentralPartial,
};

struct FeatureResult {
    SplitJulianDate jd_suo_tt;
    double delta_t;
    double ds;
    double vx;
    double vy;
    double ax;
    double ay;
    double v;
    double k;
    SplitJulianDate maximum_jd_tt;
    double xc;
    double yc;
    double zc;
    double D;
    double d;
    BesselianFrame I;
    bool center_valid;
    double center_longitude_rad;
    double center_latitude_rad;
    double center_r2;
    ShadowRadii Bc;
    ShadowRadii Bp;
    QrdResult gk1;
    QrdResult gk2;
    QrdResult gk3;
    QrdResult gk4;
    QrdResult gk5;
    double magnitude;
    EclipseType type;
    double sun_azimuth_rad;
    double sun_altitude_rad;
    double path_width_km;
    double duration_seconds;
};

struct MDianResult {
    bool found;
    double longitude_rad;
    double latitude_rad;
};

struct MQieState {
    int f2;
    int f;
};

struct MQieResult {
    bool valid;
    double longitude_rad;
    double latitude_rad;
    bool endpoint_valid;
    double endpoint_longitude_rad;
    double endpoint_latitude_rad;
    double endpoint_time_offset_days;
    bool endpoint_entering;
};

struct JieX3Row {
    bool penumbral_north_valid;
    double penumbral_north_longitude_rad;
    double penumbral_north_latitude_rad;
    bool core_north_valid;
    double core_north_longitude_rad;
    double core_north_latitude_rad;
    bool center_valid;
    double center_longitude_rad;
    double center_latitude_rad;
    bool core_south_valid;
    double core_south_longitude_rad;
    double core_south_latitude_rad;
    bool penumbral_south_valid;
    double penumbral_south_longitude_rad;
    double penumbral_south_latitude_rad;
};

struct CurvePoint {
    double longitude_rad;
    double latitude_rad;
    SplitJulianDate jd_tt;
};

struct CurveList {
    std::vector<CurvePoint> points;
    int f2;
    int f;
};

struct JieXResult {
    FeatureResult feature;
    CurveList p1;
    CurveList p2;
    CurveList p3;
    CurveList p4;
    CurveList q1;
    CurveList q2;
    CurveList q3;
    CurveList q4;
    CurveList L0;
    CurveList L1;
    CurveList L2;
    CurveList L3;
    CurveList L4;
    CurveList L5;
    CurveList L6;
};

struct JieX2Result {
    CurveList p1;
    CurveList p2;
    CurveList p3;
};

struct JieXSample {
    SplitJulianDate jd_tt;
    Vec3 M;
    ShadowRadii B;
    BesselianFrame I;
    Vec3 sun;
};

struct JieXContext {
    double bba;
    double k;
    double earth_axis_ratio;
    SplitJulianDate jd_suo_tt;
    void* user_data;
    bool (*sample)(void* user_data, SplitJulianDate jd_tt, JieXSample* out);
    bool refine_transition_endpoints;
};

struct ConeApex {
    double longitude_rad;
    double latitude_rad;
    double radius;
};

struct RsplContext {
    double bba;
    double earth_axis_ratio;
    void* user_data;
    bool (*sun_llr)(void* user_data, SplitJulianDate jd, Vec3* out);
    bool (*moon_llr)(void* user_data, SplitJulianDate jd, Vec3* out);
    bool (*gast)(void* user_data, SplitJulianDate jd_ut, SplitJulianDate jd_tt, double* out);
};

struct RsplState {
    Vec3 S;
    Vec3 M;
    double gast_rad;
    ConeApex A;
    ConeApex B;
};

struct RsplBoundary {
    bool valid;
    double longitude_rad;
    double latitude_rad;
};

struct RsplNbjResult {
    RsplBoundary center;
    uint32_t center_kind;
    RsplBoundary umbra_north;
    RsplBoundary umbra_south;
    RsplBoundary penumbra_north;
    RsplBoundary penumbra_south;
    double umbra_width_km;
};

Vec3 llr_to_xyz(double lon_rad, double lat_rad, double radius) noexcept;
Vec3 xyz_to_llr(const Vec3& v) noexcept;
Vec3 llrConv(const Vec3& llr, double angle_rad) noexcept;
LineEllResult lineEll(double x1, double y1, double z1, double x2, double y2, double z2, double e, double r) noexcept;
GeoPoint lineEar2(double x1, double y1, double z1, double x2, double y2, double z2, double e, double r, const BesselianFrame& I) noexcept;
OvlResult cirOvl(double R, double ba, double R2, double x0, double y0) noexcept;
OvlResult lineOvl(double x1, double y1, double dx, double dy, double r, double ba) noexcept;
GeoPoint bse2db(double x, double y, double z, const BesselianFrame& I, bool ellipsoid) noexcept;
GeoPoint bseXY2db(double x, double y, const BesselianFrame& I, bool ellipsoid) noexcept;
SurfaceVelocity Vxy(double x, double y, double s_rad, double vx, double vy) noexcept;
ShadowRadii rSM(double mR, double k, double k2, double k0, double tanf1, double tanf2, double dyj) noexcept;
BoundaryPoint nanbei(double Mx, double My, double Mz, double vx0, double vy0, int h, double r, const BesselianFrame& I, double k, double earth_axis_ratio) noexcept;
Vec3 CD2DP(const Vec3& z, double L_rad, double fa_rad, double gst_rad) noexcept;
QrdResult qrd(const SolarContext& ctx, SplitJulianDate jd, double dx, double dy, int fs) noexcept;
FeatureResult feature(const SolarContext& ctx, SplitJulianDate jd_suo) noexcept;
MDianResult mDian(double Mx, double My, double vx0, double vy0, bool AB, double r, const BesselianFrame& I, double bba) noexcept;
MQieResult mQie(double Mx, double My, double Mz, double vx0, double vy0, int h, double r, const BesselianFrame& I, double k, double earth_axis_ratio, double bba, MQieState* state) noexcept;
JieX3Row jieX3(double Mx, double My, double Mz, double vx, double vy, const ShadowRadii& B, const BesselianFrame& I, double k, double earth_axis_ratio, double bba) noexcept;
void push(const CurvePoint& z, CurveList* p);
void elmCpy(CurveList* a, int n, const CurveList& b, int m);
JieXResult jieX(
    const JieXContext& ctx,
    const FeatureResult& feature,
    std::size_t sample_count = 400);
JieX2Result jieX2(const JieXContext& ctx, SplitJulianDate jd_tt);

double lineT_contact(double t, double x, double y, double v, double u, double r, int n) noexcept;

bool rspl_zb0(const RsplContext& ctx, SplitJulianDate jd_tt, RsplState* out);
void rspl_zbXY(RsplState* state, double longitude_rad, double latitude_rad);
RsplBoundary rspl_lineEar(const Vec3& point_llr, const ConeApex& apex, double gst_rad);
RsplBoundary rspl_lineEar_llr(const Vec3& point_llr, const Vec3& target_llr, double gst_rad);
RsplBoundary rspl_pp0(const RsplState& state);
RsplBoundary rspl_p2p(const RsplContext& ctx, RsplState* state, double longitude_rad, double latitude_rad, bool use_umbra, int side, int iterations);
RsplNbjResult rspl_nbj(const RsplContext& ctx, SplitJulianDate jd_tt, double longitude_rad, double latitude_rad);

}  // namespace solar
}  // namespace sxwnl
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOLAR_ECLIPSE_SXWNL_H
