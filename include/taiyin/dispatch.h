#ifndef TAIYIN_DISPATCH_H
#define TAIYIN_DISPATCH_H

#include "coordinates.h"
#include "vector3.h"

#include <cstddef>
#include <cstdint>

namespace taiyin {
class SplitJulianDate;

namespace dispatch {

// --- Model ID constants ---

enum ModelSelectionId {
    MODEL_SELECTION_DEFAULT = -1,
};

// Monotonically changes whenever a dispatch registration or selection policy
// capable of affecting cached time scales or apparent matrices changes.
// Context-local caches use this to reject values produced by a callback that
// has since been replaced under the same model ID.
uint64_t model_registry_generation() noexcept;

enum RefractionModelId {
    REFRACTION_BENNETT = 0,
    REFRACTION_SKYFIELD = 1,
    REFRACTION_HYBRID = 2,
    REFRACTION_AUER_STANDISH = 3,
    REFRACTION_SOFA = 4,
    REFRACTION_CUSTOM_START = 1000,
};

// Heliacal-visibility profiles bundle the empirical twilight, extinction, and
// visual-threshold assumptions used by the runtime visibility evaluator.
enum HeliacalVisibilityModelId {
    HELIACAL_VISIBILITY_BELOKRYLOV_2011 = 0,
    HELIACAL_VISIBILITY_SCHAEFER_1993 = 1,
    HELIACAL_VISIBILITY_CUSTOM_START = 1000,
};

enum HeliacalExtinctionModelId {
    HELIACAL_EXTINCTION_BELOKRYLOV_2011 = 0,
    HELIACAL_EXTINCTION_SCHAEFER_2000 = 1,
    HELIACAL_EXTINCTION_CUSTOM_START = 1000,
};

enum HeliacalTwilightModelId {
    HELIACAL_TWILIGHT_BELOKRYLOV_2011 = 0,
    HELIACAL_TWILIGHT_SCHAEFER_1993 = 1,
    HELIACAL_TWILIGHT_CUSTOM_START = 1000,
};

enum HeliacalMoonlightModelId {
    HELIACAL_MOONLIGHT_NONE = 0,
    HELIACAL_MOONLIGHT_KRISCIUNAS_SCHAEFER_1991 = 1,
    HELIACAL_MOONLIGHT_CUSTOM_START = 1000,
};

enum HeliacalVisualThresholdModelId {
    HELIACAL_VISUAL_THRESHOLD_BELOKRYLOV_2011 = 0,
    HELIACAL_VISUAL_THRESHOLD_HECHT_1947 = 1,
    HELIACAL_VISUAL_THRESHOLD_CUSTOM_START = 1000,
};

enum PrecessionModelId {
    PRECESSION_VONDRAK2011 = 0,
    PRECESSION_IAU2006 = 1,
    PRECESSION_IAU1976 = 2,
    PRECESSION_NEWCOMB1895 = 3,
    PRECESSION_CUSTOM_START = 1000,
};

enum NutationModelId {
    NUTATION_IAU2000B = 0,
    NUTATION_IAU2000A = 1,
    NUTATION_CUSTOM_START = 1000,
};

enum TdbModelId {
    TDB_FAST_PERIODIC = 0,
    TDB_SOFA_FULL = 1,
    TDB_CUSTOM_START = 1000,
};

enum FrameRouteId {
    FRAME_ROUTE_EQUINOX = 0,
    FRAME_ROUTE_CIRS = 1,
    FRAME_ROUTE_CUSTOM_START = 1000,
};

enum DeltaTModelId {
    DELTA_T_ESTIMATED_DEFAULT = 0,
    DELTA_T_CUSTOM_START = 1000,
};

enum EphemerisFamilyId {
    EPHEMERIS_FAMILY_UNKNOWN = 0,
    EPHEMERIS_FAMILY_DE441 = 441,
    EPHEMERIS_FAMILY_DE431 = 431,
    EPHEMERIS_FAMILY_CUSTOM_START = 1000,
};

enum DeltaTEphemerisCorrectionId {
    DELTA_T_EPHEMERIS_CORRECTION_NONE = 0,
    DELTA_T_EPHEMERIS_CORRECTION_CUSTOM_START = 1000,
};

enum AberrationModelId {
    ABERRATION_ANNUAL_RELATIVISTIC = 0,
    ABERRATION_CUSTOM_START = 1000,
};

enum LunarEclipseShadowModelId {
    ECLIPSE_SHADOW_NASA_DANJON = 0,    // NASA empirical 1% enlargement
    ECLIPSE_SHADOW_CHAUVENET   = 1,    // 2% enlargement + Earth oblateness
    ECLIPSE_SHADOW_GEOMETRIC   = 2,     // pure geometry, no atmosphere
    ECLIPSE_SHADOW_RAW_DANJON  = 3,     // Danjon 1/85 atmospheric enlargement
    ECLIPSE_SHADOW_CUSTOM_START = 1000,
};

enum LunarEclipseMoonRadiusModelId {
    ECLIPSE_MOON_ALMANAC = 0,    // almanac ratio (0.2725076 * R_earth)
    ECLIPSE_MOON_MEAN    = 1,    // IAU mean radius (1737.4 km)
    ECLIPSE_MOON_CUSTOM_START = 1000,
};

// --- Lunar Eclipse Shadow Model ---

struct EclipseShadowModelEntry {
    int model_id;
    double earth_scale;
    double sun_scale;
    double parallax_scale;

    EclipseShadowModelEntry()
        : model_id(0), earth_scale(1.0), sun_scale(1.0), parallax_scale(1.0) {}

    EclipseShadowModelEntry(int id, double earth, double sun, double parallax)
        : model_id(id), earth_scale(earth), sun_scale(sun), parallax_scale(parallax) {}
};

bool add_eclipse_shadow_model(const EclipseShadowModelEntry& entry) noexcept;
bool find_eclipse_shadow_model(int id, EclipseShadowModelEntry* out) noexcept;
bool set_eclipse_shadow_priority_order(const int* model_ids, size_t count) noexcept;
bool select_eclipse_shadow_model(int requested_id, EclipseShadowModelEntry* out) noexcept;

// --- Lunar Eclipse Moon Radius Model ---

struct EclipseMoonRadiusModelEntry {
    int model_id;
    double radius_km;

    EclipseMoonRadiusModelEntry()
        : model_id(0), radius_km(1737.4) {}

    EclipseMoonRadiusModelEntry(int id, double radius)
        : model_id(id), radius_km(radius) {}
};

bool add_eclipse_moon_radius_model(const EclipseMoonRadiusModelEntry& entry) noexcept;
bool find_eclipse_moon_radius_model(int id, EclipseMoonRadiusModelEntry* out) noexcept;

// --- Refraction ---

struct RefractionDispatchData {
    double altitude_rad;
    double pressure_mbar;
    double temperature_c;
    double relative_humidity;
    double wavelength_micrometer;
    int max_iterations;
    double tolerance;
};

typedef double (*RefractionFn)(const void* data);

void register_refraction_model(int id, RefractionFn fn);
bool has_refraction_model(int id) noexcept;
double eval_refraction(int id, const void* data);

// --- Heliacal visibility ---

// The runtime owns the concrete input/output structures. Keeping this
// dispatch boundary type-erased lets applications register a profile without
// coupling the core model registry to a runtime header.
typedef bool (*HeliacalVisibilityFn)(const void* input, void* output);

struct HeliacalVisibilityModelEntry {
    int model_id;
    HeliacalVisibilityFn eval;
    int extinction_model_id;
    int twilight_model_id;
    int moonlight_model_id;
    int visual_threshold_model_id;
    double default_extinction_mag_per_airmass;

    HeliacalVisibilityModelEntry()
        : model_id(0),
          eval(nullptr),
          extinction_model_id(0),
          twilight_model_id(0),
          moonlight_model_id(HELIACAL_MOONLIGHT_NONE),
          visual_threshold_model_id(0),
          default_extinction_mag_per_airmass(0.25) {}

    HeliacalVisibilityModelEntry(
        int model_id_value,
        HeliacalVisibilityFn eval_value,
        int extinction_model_id_value,
        int twilight_model_id_value,
        int visual_threshold_model_id_value,
        double default_extinction_mag_per_airmass_value
    )
        : model_id(model_id_value),
          eval(eval_value),
          extinction_model_id(extinction_model_id_value),
          twilight_model_id(twilight_model_id_value),
          moonlight_model_id(HELIACAL_MOONLIGHT_NONE),
          visual_threshold_model_id(visual_threshold_model_id_value),
          default_extinction_mag_per_airmass(default_extinction_mag_per_airmass_value) {}

    HeliacalVisibilityModelEntry(
        int model_id_value,
        HeliacalVisibilityFn eval_value,
        int extinction_model_id_value,
        int twilight_model_id_value,
        int moonlight_model_id_value,
        int visual_threshold_model_id_value,
        double default_extinction_mag_per_airmass_value
    )
        : model_id(model_id_value),
          eval(eval_value),
          extinction_model_id(extinction_model_id_value),
          twilight_model_id(twilight_model_id_value),
          moonlight_model_id(moonlight_model_id_value),
          visual_threshold_model_id(visual_threshold_model_id_value),
          default_extinction_mag_per_airmass(default_extinction_mag_per_airmass_value) {}
};

void register_heliacal_visibility_model(int id, HeliacalVisibilityFn fn);
void register_heliacal_visibility_model(const HeliacalVisibilityModelEntry& entry);
bool add_heliacal_visibility_model(const HeliacalVisibilityModelEntry& entry) noexcept;
bool find_heliacal_visibility_model(int id, HeliacalVisibilityModelEntry* out) noexcept;
bool has_heliacal_visibility_model(int id) noexcept;
bool eval_heliacal_visibility(int id, const void* input, void* output) noexcept;

// --- Precession ---

typedef bool (*PrecessionFn)(const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad);

struct PrecessionModelEntry {
    int model_id;
    PrecessionFn eval;

    PrecessionModelEntry()
        : model_id(0), eval(nullptr) {}

    PrecessionModelEntry(int model_id_value, PrecessionFn eval_value)
        : model_id(model_id_value), eval(eval_value) {}
};

void register_precession_model(int id, PrecessionFn fn);
bool add_precession_model(const PrecessionModelEntry& entry) noexcept;
bool find_precession_model(int id, PrecessionModelEntry* out) noexcept;
bool set_precession_priority_order(const int* model_ids, size_t count) noexcept;
bool push_precession_priority_model(int id) noexcept;
bool insert_precession_priority_model(size_t index, int id) noexcept;
bool remove_precession_priority_model(int id) noexcept;
bool select_precession_model(int requested_id, PrecessionModelEntry* out) noexcept;
bool eval_precession(int id, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr);
bool eval_selected_precession(int requested_id, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr);

// --- Nutation ---

typedef bool (*NutationFn)(const SplitJulianDate& jd_tt, const void* data, NutationAngles* out);

struct NutationModelEntry {
    int model_id;
    NutationFn eval;

    NutationModelEntry()
        : model_id(0), eval(nullptr) {}

    NutationModelEntry(int model_id_value, NutationFn eval_value)
        : model_id(model_id_value), eval(eval_value) {}
};

void register_nutation_model(int id, NutationFn fn);
bool add_nutation_model(const NutationModelEntry& entry) noexcept;
bool find_nutation_model(int id, NutationModelEntry* out) noexcept;
bool set_nutation_priority_order(const int* model_ids, size_t count) noexcept;
bool push_nutation_priority_model(int id) noexcept;
bool insert_nutation_priority_model(size_t index, int id) noexcept;
bool remove_nutation_priority_model(int id) noexcept;
bool select_nutation_model(int requested_id, NutationModelEntry* out) noexcept;
bool eval_nutation(int id, const SplitJulianDate& jd_tt, const void* data, NutationAngles* out);
bool eval_selected_nutation(int requested_id, const SplitJulianDate& jd_tt, const void* data, NutationAngles* out);

// --- TDB ---

typedef double (*TdbFn)(const SplitJulianDate& jd_tt, const void* data);

void register_tdb_model(int id, TdbFn fn);
double eval_tdb(int id, const SplitJulianDate& jd_tt, const void* data);

struct TdbInverseDispatchData {
    int max_iterations;
    double tolerance_days;
};

typedef bool (*TdbInverseFn)(
    const SplitJulianDate& jd_tdb,
    const void* data,
    SplitJulianDate* out_jd_tt
);

void register_tdb_inverse_model(int id, TdbInverseFn fn);
bool eval_tdb_inverse(
    int id,
    const SplitJulianDate& jd_tdb,
    const void* data,
    SplitJulianDate* out_jd_tt
);

// --- Frame Route ---

struct FrameRouteDispatchData {
    double xp_rad;
    double yp_rad;
    double sp_rad;
    double dx_rad;
    double dy_rad;
    int precession_model;
    int nutation_model;
};

typedef bool (*FrameRouteFn)(const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out);

void register_frame_route(int id, FrameRouteFn fn);
bool eval_frame_route(int id, const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out);

// --- Delta-T ---

typedef double (*DeltaTFn)(const SplitJulianDate& jd_ut, const void* data);

struct DeltaTModelEntry {
    int model_id;
    DeltaTFn eval;

    DeltaTModelEntry()
        : model_id(0), eval(nullptr) {}

    DeltaTModelEntry(int model_id_value, DeltaTFn eval_value)
        : model_id(model_id_value), eval(eval_value) {}
};

void register_delta_t_model(int id, DeltaTFn fn);
bool add_delta_t_model(const DeltaTModelEntry& entry) noexcept;
bool find_delta_t_model(int id, DeltaTModelEntry* out) noexcept;
double eval_delta_t(int id, const SplitJulianDate& jd_ut, const void* data);
double eval_delta_t_with_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    const SplitJulianDate& jd_ut,
    const void* delta_t_data,
    const void* correction_data
);

// --- Aberration ---

struct AberrationDispatchData {
    Vector3 source_geocentric_position_au;
    Vector3 source_geocentric_velocity_au_per_day;
    Vector3 source_geocentric_acceleration_au_per_day2;
    Vector3 observer_heliocentric_position_au;
    Vector3 observer_heliocentric_velocity_au_per_day;
    Vector3 observer_heliocentric_acceleration_au_per_day2;
    Vector3 observer_barycentric_velocity_au_per_day;
    Vector3 observer_barycentric_acceleration_au_per_day2;
    double light_time_days_per_au;
    double solar_schwarzschild_radius_au;
    bool compute_acceleration;
};

typedef bool (*AberrationFn)(const AberrationDispatchData* data, Vector3* out_position_au, Vector3* out_velocity_au_per_day, Vector3* out_acceleration_au_per_day2);

struct AberrationModelEntry {
    int model_id;
    AberrationFn eval;

    AberrationModelEntry()
        : model_id(0), eval(nullptr) {}

    AberrationModelEntry(int model_id_value, AberrationFn eval_value)
        : model_id(model_id_value), eval(eval_value) {}
};

void register_aberration_model(int id, AberrationFn fn);
bool add_aberration_model(const AberrationModelEntry& entry) noexcept;
bool find_aberration_model(int id, AberrationModelEntry* out) noexcept;
bool set_aberration_priority_order(const int* model_ids, size_t count) noexcept;
bool push_aberration_priority_model(int id) noexcept;
bool insert_aberration_priority_model(size_t index, int id) noexcept;
bool remove_aberration_priority_model(int id) noexcept;
bool select_aberration_model(int requested_id, AberrationModelEntry* out) noexcept;
bool eval_aberration(int id, const AberrationDispatchData* data, Vector3* out_position_au, Vector3* out_velocity_au_per_day, Vector3* out_acceleration_au_per_day2);
bool eval_selected_aberration(int requested_id, const AberrationDispatchData* data, Vector3* out_position_au, Vector3* out_velocity_au_per_day, Vector3* out_acceleration_au_per_day2);

// --- Delta-T / Ephemeris Compatibility ---

typedef double (*DeltaTEphemerisCorrectionFn)(
    const SplitJulianDate& jd_ut,
    int delta_t_model_id,
    int ephemeris_family_id,
    const void* data
);

struct DeltaTEphemerisCorrectionEntry {
    int correction_id;
    DeltaTEphemerisCorrectionFn eval;

    DeltaTEphemerisCorrectionEntry()
        : correction_id(0), eval(nullptr) {}

    DeltaTEphemerisCorrectionEntry(int correction_id_value, DeltaTEphemerisCorrectionFn eval_value)
        : correction_id(correction_id_value), eval(eval_value) {}
};

struct DeltaTEphemerisCorrectionKey {
    int delta_t_model_id;
    int ephemeris_family_id;

    DeltaTEphemerisCorrectionKey()
        : delta_t_model_id(0), ephemeris_family_id(0) {}

    DeltaTEphemerisCorrectionKey(int delta_t_model_id_value, int ephemeris_family_id_value)
        : delta_t_model_id(delta_t_model_id_value), ephemeris_family_id(ephemeris_family_id_value) {}

    bool operator==(const DeltaTEphemerisCorrectionKey& other) const {
        return delta_t_model_id == other.delta_t_model_id
            && ephemeris_family_id == other.ephemeris_family_id;
    }
};

void register_delta_t_ephemeris_correction(int correction_id, DeltaTEphemerisCorrectionFn fn);
bool add_delta_t_ephemeris_correction(const DeltaTEphemerisCorrectionEntry& entry) noexcept;
bool find_delta_t_ephemeris_correction(int correction_id, DeltaTEphemerisCorrectionEntry* out) noexcept;
bool bind_delta_t_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    int correction_id
) noexcept;
int find_delta_t_ephemeris_correction_binding(int delta_t_model_id, int ephemeris_family_id) noexcept;
double eval_delta_t_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    const SplitJulianDate& jd_ut,
    const void* data
) noexcept;

}  // namespace dispatch
}  // namespace taiyin

#endif  // TAIYIN_DISPATCH_H
