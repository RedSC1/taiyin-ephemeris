#ifndef TAIYIN_PIPELINE_H
#define TAIYIN_PIPELINE_H

#include "internal/ephemeris_block.h"
#include "observer.h"
#include "vector3.h"

#include <cstddef>
#include <vector>

namespace taiyin {
namespace pipeline {

using internal::EphemerisPositionFn;
using internal::EphemerisVelocityFn;
using internal::EphemerisAccelerationFn;

// --- Step IDs ---

enum StepId {
    STEP_LIGHT_TIME = 0,
    STEP_DEFLECTION = 1,
    STEP_ABERRATION = 2,
    STEP_FRAME_TRANSFORM = 3,
    STEP_OBSERVER_GEOCENTRIC = 4,
    STEP_TOPOCENTRIC = 5,
    STEP_DIURNAL_ABERRATION = 8,
    STEP_HORIZONTAL = 6,
    STEP_REFRACTION = 7,
    STEP_EARTH_SHADOW_RADII = 10,
    STEP_ECLIPTIC_TO_FUNDAMENTAL = 11,
    STEP_BESSEL_TO_GEODETIC = 12,
    STEP_LINE_SPHEROID_INTERSECT = 13,
    STEP_QUADRATIC_CONTACT = 14,
    STEP_ECLIPTIC_PROJECTION = 15,
    STEP_EVAL_BODY = 16,
    STEP_EVAL_BODY_POSITION = 17,
    STEP_EVAL_BODY_VELOCITY = 18,
    STEP_EVAL_BODY_ACCELERATION = 19,
    STEP_CUSTOM_START = 1000,
};

// --- Pipeline context ---

struct PipelineContext {
    std::vector<void*> step_data_in;
    std::vector<void*> step_data_out;
};

// --- Pipeline Error Codes ---
constexpr int OK = 0;
constexpr int ERR_GENERIC = -1;
constexpr int ERR_NULL_CONTEXT = -2;
constexpr int ERR_STEP_DATA_SIZE = -3;
constexpr int ERR_STEP_NOT_FOUND = -4;
constexpr int ERR_WIRING_FAILED = -5;
constexpr int ERR_STEP_EXECUTION_FAILED = -6;

typedef int (*PipelineStepFn)(PipelineContext* ctx, int step_index);

// --- Stack-Only Memory Constraint Base ---

struct StackOnly {
    void* operator new(size_t) = delete;
    void* operator new[](size_t) = delete;
};

// --- Step data structs ---

struct LightTimeStepData : public StackOnly {
    double jd_tdb;
    Vector3 observer_position_au;
    Vector3 observer_velocity_au_per_day;
    // target eval via ephemeris block
    const void* target_block;
    EphemerisPositionFn target_position_fn;
    EphemerisVelocityFn target_velocity_fn;
    int max_iterations;
    double tolerance_days;
    // out
    Vector3 observed_position_au;
    Vector3 observed_velocity_au_per_day;
    double tau_days;
    double tau_rate;
};

struct DeflectionStepData : public StackOnly {
    Vector3 observer_heliocentric_position_au;
    Vector3 observer_heliocentric_velocity_au_per_day;
    Vector3 sun_heliocentric_position_au;
    Vector3 sun_heliocentric_velocity_au_per_day;
    double schwarzschild_radius_au;
    double deflection_limit;
    // in/out
    Vector3 apparent_position_au;
    Vector3 apparent_velocity_au_per_day;
};

struct AberrationStepData : public StackOnly {
    Vector3 observer_heliocentric_position_au;
    Vector3 observer_heliocentric_velocity_au_per_day;
    Vector3 observer_heliocentric_acceleration_au_per_day2;
    Vector3 observer_barycentric_velocity_au_per_day;
    Vector3 observer_barycentric_acceleration_au_per_day2;
    double light_time_days_per_au;
    double solar_schwarzschild_radius_au;
    // in/out
    Vector3 apparent_position_au;
    Vector3 apparent_velocity_au_per_day;
};

struct FrameTransformStepData : public StackOnly {
    double jd_ut1;
    double jd_tt;
    int frame_route;
    // EOP for frame route dispatch
    double xp_rad;
    double yp_rad;
    double sp_rad;
    double dx_rad;
    double dy_rad;
    int precession_model;
    int nutation_model;
    // in/out
    Vector3 position_au;
    Vector3 velocity_au_per_day;
};

struct ObserverGeocentricStepData : public StackOnly {
    double jd_ut1;
    double jd_tt;
    double longitude_rad;
    double latitude_rad;
    double height_m;
    double xp_rad;
    double yp_rad;
    double sp_rad;
    int frame_route;
    double xp_rate_rad_per_day;
    double yp_rate_rad_per_day;
    double sp_rate_rad_per_day;
    double dut1_rate_seconds_per_day;
    double lod_seconds;
    double derivative_step_days;
    // out
    Vector3 observer_position_au;
    Vector3 observer_velocity_au_per_day;
};

struct TopocentricStepData : public StackOnly {
    // in
    Vector3 target_position_au;
    Vector3 target_velocity_au_per_day;
    Vector3 observer_position_au;
    Vector3 observer_velocity_au_per_day;
    // out
    Vector3 topocentric_position_au;
    Vector3 topocentric_velocity_au_per_day;
};

struct DiurnalAberrationStepData : public StackOnly {
    // in/out
    Vector3 topocentric_position_au;
    Vector3 topocentric_velocity_au_per_day;
    // in
    Vector3 observer_velocity_au_per_day;
    Vector3 observer_acceleration_au_per_day2;
};

struct HorizontalStepData : public StackOnly {
    // in
    Vector3 topocentric_position_au;
    double local_sidereal_rad;
    double latitude_rad;
    // out
    HorizontalCoordinates horizontal;
};

struct RefractionStepData : public StackOnly {
    // in
    int refraction_model;
    double pressure_mbar;
    double temperature_c;
    double relative_humidity;
    double wavelength_micrometer;
    int max_iterations;
    double tolerance;
    // in/out
    HorizontalCoordinates horizontal;
};

struct EarthShadowRadiiStepData : public StackOnly {
    // in
    double moon_distance_equatorial_radii;
    double sun_distance_au;
    double earth_scale;
    double sun_scale;
    double parallax_scale;
    // out
    double umbra_rad;
    double penumbra_rad;
};

struct EclipticToFundamentalStepData : public StackOnly {
    // in
    double moon_longitude_rad;
    double moon_latitude_rad;
    double sun_longitude_rad;
    double sun_latitude_rad;
    // out
    double fundamental_x;
    double fundamental_y;
};

struct BesselToGeodeticStepData : public StackOnly {
    // in
    Vector3 intersection;
    double axis_ratio;
    double bessel_ra;
    double bessel_dec;
    double gst;
    // out
    double longitude_rad;
    double latitude_rad;
};

struct LineSpheroidIntersectStepData : public StackOnly {
    // in
    Vector3 p1;
    Vector3 p2;
    double axis_ratio;
    double equator_radius;
    // out
    Vector3 intersection;
    double dist1;
    double dist2;
};

struct QuadraticContactStepData : public StackOnly {
    // in
    double t0;
    double x0;
    double y0;
    double vx;
    double vy;
    double contact_radius;
    // out
    double t1;
    double t2;
};

struct EclipticProjectionStepData : public StackOnly {
    // in
    Vector3 position_au;
    Vector3 velocity_au_per_day;
    // out
    double longitude_rad;
    double latitude_rad;
    double radius_au;
    double longitude_rate_rad_per_day;
    double latitude_rate_rad_per_day;
    double radius_rate_au_per_day;
};

struct EvalBodyStepData : public StackOnly {
    // in
    int body_id;
    double jd_tdb;
    const void* target_block;
    EphemerisPositionFn target_position_fn;
    EphemerisVelocityFn target_velocity_fn;
    EphemerisAccelerationFn target_acceleration_fn;
    // out
    Vector3 position_au;
    Vector3 velocity_au_per_day;
    Vector3 acceleration_au_per_day2;
};

struct EvalBodyPositionStepData : public StackOnly {
    // in
    int body_id;
    double jd_tdb;
    const void* target_block;
    EphemerisPositionFn target_position_fn;
    // out
    Vector3 position_au;
};

struct EvalBodyVelocityStepData : public StackOnly {
    // in
    int body_id;
    double jd_tdb;
    const void* target_block;
    EphemerisVelocityFn target_velocity_fn;
    // out
    Vector3 velocity_au_per_day;
};

struct EvalBodyAccelerationStepData : public StackOnly {
    // in
    int body_id;
    double jd_tdb;
    const void* target_block;
    EphemerisAccelerationFn target_acceleration_fn;
    // out
    Vector3 acceleration_au_per_day2;
};

// --- Step registration ---

void register_step(int id, PipelineStepFn fn);
PipelineStepFn lookup_step(int id);

// --- Built-in step implementations ---

int step_light_time(PipelineContext* ctx, int step_index);
int step_deflection(PipelineContext* ctx, int step_index);
int step_aberration(PipelineContext* ctx, int step_index);
int step_frame_transform(PipelineContext* ctx, int step_index);
int step_observer_geocentric(PipelineContext* ctx, int step_index);
int step_topocentric(PipelineContext* ctx, int step_index);
int step_diurnal_aberration(PipelineContext* ctx, int step_index);
int step_horizontal(PipelineContext* ctx, int step_index);
int step_refraction(PipelineContext* ctx, int step_index);
int step_earth_shadow_radii(PipelineContext* ctx, int step_index);
int step_ecliptic_to_fundamental(PipelineContext* ctx, int step_index);
int step_bessel_to_geodetic(PipelineContext* ctx, int step_index);
int step_line_spheroid_intersect(PipelineContext* ctx, int step_index);
int step_quadratic_contact(PipelineContext* ctx, int step_index);
int step_ecliptic_projection(PipelineContext* ctx, int step_index);
int step_eval_body(PipelineContext* ctx, int step_index);
int step_eval_body_position(PipelineContext* ctx, int step_index);
int step_eval_body_velocity(PipelineContext* ctx, int step_index);
int step_eval_body_acceleration(PipelineContext* ctx, int step_index);

// --- Pipeline execution ---

int run_pipeline(const int* step_ids, int step_count, PipelineContext* ctx);

// --- Preset pipelines ---

extern const int kEquinoxPipeline[];
extern const int kEquinoxPipelineStepCount;

extern const int kCirsPipeline[];
extern const int kCirsPipelineStepCount;

}  // namespace pipeline
}  // namespace taiyin

#endif  // TAIYIN_PIPELINE_H
