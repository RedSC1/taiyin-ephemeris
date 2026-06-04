#ifndef TAIYIN_DISPATCH_H
#define TAIYIN_DISPATCH_H

#include "coordinates.h"

#include <cstddef>

namespace taiyin {
namespace dispatch {

// --- Model ID constants ---

enum ModelSelectionId {
    MODEL_SELECTION_DEFAULT = -1,
};

enum RefractionModelId {
    REFRACTION_BENNETT = 0,
    REFRACTION_SKYFIELD = 1,
    REFRACTION_HYBRID = 2,
    REFRACTION_AUER_STANDISH = 3,
    REFRACTION_SOFA = 4,
    REFRACTION_CUSTOM_START = 1000,
};

enum PrecessionModelId {
    PRECESSION_VONDRAK2011 = 0,
    PRECESSION_IAU2006 = 1,
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
double eval_refraction(int id, const void* data);

// --- Precession ---

typedef bool (*PrecessionFn)(double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad);

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
bool eval_precession(int id, double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr);
bool eval_selected_precession(int requested_id, double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad = nullptr);

// --- Nutation ---

typedef bool (*NutationFn)(double jd_tt, const void* data, NutationAngles* out);

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
bool eval_nutation(int id, double jd_tt, const void* data, NutationAngles* out);
bool eval_selected_nutation(int requested_id, double jd_tt, const void* data, NutationAngles* out);

// --- TDB ---

typedef double (*TdbFn)(double jd_tt, const void* data);

void register_tdb_model(int id, TdbFn fn);
double eval_tdb(int id, double jd_tt, const void* data);

struct TdbInverseDispatchData {
    int max_iterations;
    double tolerance_days;
};

typedef double (*TdbInverseFn)(double jd_tdb, const void* data);

void register_tdb_inverse_model(int id, TdbInverseFn fn);
double eval_tdb_inverse(int id, double jd_tdb, const void* data);

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

typedef bool (*FrameRouteFn)(double jd_ut1, double jd_tt, const void* data, Matrix3x3* out);

void register_frame_route(int id, FrameRouteFn fn);
bool eval_frame_route(int id, double jd_ut1, double jd_tt, const void* data, Matrix3x3* out);

}  // namespace dispatch
}  // namespace taiyin

#endif  // TAIYIN_DISPATCH_H
