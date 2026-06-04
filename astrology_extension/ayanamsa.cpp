#include "ayanamsa.h"
#include "taiyin/stars.h"
#include "taiyin/angle.h"
#include "taiyin/dispatch.h"

#include <cmath>
#include <unordered_map>

namespace taiyin {

// --- Registry database for Ayanamsa ---

static std::unordered_map<int, AyanamsaFn>& ayanamsa_models() {
    static std::unordered_map<int, AyanamsaFn> models;
    return models;
}

void register_ayanamsa_model(int id, AyanamsaFn fn) {
    if (fn) {
        ayanamsa_models()[id] = fn;
    }
}

// Declarations of built-in models registration trigger
namespace wrappers {
void register_builtin_astrology_wrappers();
}

bool eval_ayanamsa(int id, double jd_tt, const void* data, double* out_ayanamsa_deg) {
    wrappers::register_builtin_astrology_wrappers();
    auto it = ayanamsa_models().find(id);
    if (it != ayanamsa_models().end()) {
        return it->second(jd_tt, data, out_ayanamsa_deg);
    }
    // Fallback to Lahiri if model is not registered
    auto fallback_it = ayanamsa_models().find(AYANAMSA_LAHIRI);
    if (fallback_it != ayanamsa_models().end()) {
        return fallback_it->second(jd_tt, data, out_ayanamsa_deg);
    }
    return false;
}

// --- Pure Calculation Functions ---

namespace {

enum PrecessionModelType {
    PRECESSION_MODEL_NEWCOMB_B1900,
    PRECESSION_MODEL_NEWCOMB_KSK,
    PRECESSION_MODEL_RAMAN
};

struct AyanamsaConstant {
    int id;
    const char* name;
    double t0;
    double ayan_t0;
    PrecessionModelType precession_model;
    double rate_arcsec_per_year;
    double year_length;
    double jd_zero;
};

static const AyanamsaConstant AYANAMSA_CONSTANTS[] = {
    // Fagan-Bradley
    {
        0,
        "Fagan-Bradley",
        2451545.0, // J2000
        24.0 + 44.0 / 60.0 + 11.72 / 3600.0, // 24.73658888888889
        PRECESSION_MODEL_NEWCOMB_B1900,
        0.0,
        0.0,
        0.0
    },
    // Lahiri
    {
        1,
        "Lahiri",
        2451545.0, // J2000
        85904.567578 / 3600.0, // 23.862379882777776
        PRECESSION_MODEL_NEWCOMB_B1900,
        0.0,
        0.0,
        0.0
    },
    // Raman
    {
        3,
        "Raman",
        2451545.0, // J2000
        50.3333333 * (2000.0 - 397.0) / 3600.0, // 22.412314799972222
        PRECESSION_MODEL_RAMAN,
        50.3333333,
        365.25,
        0.0
    },
    // Krishnamurti
    {
        5,
        "Krishnamurti",
        2451545.0, // J2000
        50.2388475 * (2451545.0 - 1827424.67291667) / 365.2422 / 3600.0, // 23.84649444334436
        PRECESSION_MODEL_NEWCOMB_KSK,
        50.2388475,
        365.2422,
        1827424.67291667
    }
};

static const AyanamsaConstant* get_ayanamsa_constant(int id) noexcept {
    for (size_t i = 0; i < sizeof(AYANAMSA_CONSTANTS) / sizeof(AYANAMSA_CONSTANTS[0]); ++i) {
        if (AYANAMSA_CONSTANTS[i].id == id) {
            return &AYANAMSA_CONSTANTS[i];
        }
    }
    return nullptr;
}

bool eval_constant_model(int id, double jd_tt, double* out_ayanamsa_deg) noexcept {
    const AyanamsaConstant* c = get_ayanamsa_constant(id);
    if (!c) return false;

    double psi_now = 0.0;
    double psi_t0 = 0.0;
    const double B1900 = 2415020.3135;
    const double J2000 = 2451545.0;

    auto psi_newcomb_b1900 = [B1900](double jd) -> double {
        double T = (jd - B1900) / 36525.0;
        double arcsec = 5036.464 * T + 1.073 * T * T;
        return arcsec / 3600.0;
    };

    switch (c->precession_model) {
        case PRECESSION_MODEL_NEWCOMB_B1900: {
            psi_now = psi_newcomb_b1900(jd_tt) - psi_newcomb_b1900(J2000);
            psi_t0 = psi_newcomb_b1900(c->t0) - psi_newcomb_b1900(J2000);
            break;
        }
        case PRECESSION_MODEL_NEWCOMB_KSK: {
            double rate = c->rate_arcsec_per_year;
            double year_len = c->year_length;
            psi_now = (rate / 3600.0) * (jd_tt - J2000) / year_len;
            psi_t0 = (rate / 3600.0) * (c->t0 - J2000) / year_len;
            break;
        }
        case PRECESSION_MODEL_RAMAN: {
            double rate = c->rate_arcsec_per_year;
            double year_len = c->year_length;
            psi_now = (rate / 3600.0) * (jd_tt - J2000) / year_len;
            psi_t0 = (rate / 3600.0) * (c->t0 - J2000) / year_len;
            break;
        }
    }

    double result = c->ayan_t0 + psi_now - psi_t0;
    if (result < 0.0) {
        result += 360.0;
    }
    *out_ayanamsa_deg = std::fmod(result, 360.0);
    return true;
}

} // namespace

// 1. Constant pure functions
bool fagan_bradley_ayanamsa(double jd_tt, double* out_ayanamsa_deg) noexcept {
    return eval_constant_model(AYANAMSA_FAGAN_BRADLEY, jd_tt, out_ayanamsa_deg);
}

bool lahiri_ayanamsa(double jd_tt, double* out_ayanamsa_deg) noexcept {
    return eval_constant_model(AYANAMSA_LAHIRI, jd_tt, out_ayanamsa_deg);
}

bool raman_ayanamsa(double jd_tt, double* out_ayanamsa_deg) noexcept {
    return eval_constant_model(AYANAMSA_RAMAN, jd_tt, out_ayanamsa_deg);
}

bool krishnamurti_ayanamsa(double jd_tt, double* out_ayanamsa_deg) noexcept {
    return eval_constant_model(AYANAMSA_KRISHNAMURTI, jd_tt, out_ayanamsa_deg);
}

// 2. Dynamic pure functions
bool true_chitrapaksha_ayanamsa(
    double jd_tt, 
    const Matrix3x3& precession_matrix, 
    double true_obliquity_rad, 
    const internal::CompiledEphemerisBlock& dynamic_block, 
    double* out_ayanamsa_deg
) noexcept {
    if (!out_ayanamsa_deg) return false;

    Vector3 v_j2000;
    if (!internal::eval_compiled_ephemeris_block_position(jd_tt, &dynamic_block, &v_j2000)) {
        return false;
    }
    Vector3 v_precessed = matrix3x3_multiply_vector(precession_matrix, v_j2000);
    Vector3 v_ecliptic = rotate_x(v_precessed, -true_obliquity_rad);

    double lon_rad = 0.0, lat_rad = 0.0, radius = 0.0;
    cartesian_to_spherical(v_ecliptic, &lon_rad, &lat_rad, &radius);
    double lon_deg = normalize_degrees(lon_rad * TAIYIN_RAD_TO_DEG);

    double ayan = lon_deg - 180.0;
    if (ayan < 0.0) {
        ayan += 360.0;
    }
    *out_ayanamsa_deg = std::fmod(ayan, 360.0);
    return true;
}

bool galactic_center_ayanamsa(
    double jd_tt, 
    const Matrix3x3& precession_matrix, 
    double true_obliquity_rad, 
    const internal::CompiledEphemerisBlock& dynamic_block, 
    double* out_ayanamsa_deg
) noexcept {
    if (!out_ayanamsa_deg) return false;

    Vector3 v_j2000;
    if (!internal::eval_compiled_ephemeris_block_position(jd_tt, &dynamic_block, &v_j2000)) {
        return false;
    }
    Vector3 v_precessed = matrix3x3_multiply_vector(precession_matrix, v_j2000);
    Vector3 v_ecliptic = rotate_x(v_precessed, -true_obliquity_rad);

    double lon_rad = 0.0, lat_rad = 0.0, radius = 0.0;
    cartesian_to_spherical(v_ecliptic, &lon_rad, &lat_rad, &radius);
    double lon_deg = normalize_degrees(lon_rad * TAIYIN_RAD_TO_DEG);

    double ayan = lon_deg - 240.0;
    if (ayan < 0.0) {
        ayan += 360.0;
    }
    *out_ayanamsa_deg = std::fmod(ayan, 360.0);
    return true;
}

// --- 3D Precession Pure Functions ---

bool newcomb_b1900_precession_matrix(double jd_tt, Matrix3x3* out, double* out_mean_obliquity_rad) noexcept {
    if (!out) return false;
    const double B1900 = 2415020.3135;
    double T = (jd_tt - B1900) / 36525.0;
    
    // Obliquity of B1900 epoch: 23d 27' 08.26"
    double eps0 = (23.0 + 27.0 / 60.0 + 8.26 / 3600.0) * TAIYIN_DEG_TO_RAD;
    // Obliquity of date
    double epsA = (23.0 + 27.0 / 60.0 + (8.26 - 46.844 * T - 0.00232 * T * T) / 3600.0) * TAIYIN_DEG_TO_RAD;
    
    // Accumulated precession angle in longitude psi
    double psi = (5036.464 * T + 1.073 * T * T) / 3600.0 * TAIYIN_DEG_TO_RAD;
    
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = epsA;
    }
    
    // P = Rx(-epsA) * Rz(psi) * Rx(eps0)
    Matrix3x3 rx_eps0 = rotation_x_matrix(eps0);
    Matrix3x3 rz_psi = rotation_z_matrix(psi);
    Matrix3x3 rx_neg_epsA = rotation_x_matrix(-epsA);
    
    *out = matrix3x3_multiply(rx_neg_epsA, matrix3x3_multiply(rz_psi, rx_eps0));
    return true;
}

struct LinearPrecessionParams {
    double rate_arcsec_per_year;
    double year_length;
};

bool linear_precession_matrix(double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) noexcept {
    if (!out) return false;
    const double J2000 = 2451545.0;
    
    // Obliquity of J2000 epoch: 23.4392911 deg
    double eps0 = 23.4392911 * TAIYIN_DEG_TO_RAD;
    double epsA = eps0; // Linear precession keeps obliquity constant
    
    // Fallback to Raman parameters if data context is not provided
    double rate = 50.3333333;
    double year_len = 365.25;
    if (data) {
        const auto* params = static_cast<const LinearPrecessionParams*>(data);
        rate = params->rate_arcsec_per_year;
        year_len = params->year_length;
    }
    
    double psi = (rate / 3600.0) * (jd_tt - J2000) / year_len * TAIYIN_DEG_TO_RAD;
    
    if (out_mean_obliquity_rad) {
        *out_mean_obliquity_rad = epsA;
    }
    
    Matrix3x3 rx_eps0 = rotation_x_matrix(eps0);
    Matrix3x3 rz_psi = rotation_z_matrix(psi);
    Matrix3x3 rx_neg_epsA = rotation_x_matrix(-epsA);
    
    *out = matrix3x3_multiply(rx_neg_epsA, matrix3x3_multiply(rz_psi, rx_eps0));
    return true;
}


// --- 3D Precession Wrapper Functions (for Core Dispatch Registration) ---

static bool newcomb_b1900(double jd_tt, const void* /*data*/, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return newcomb_b1900_precession_matrix(jd_tt, out, out_mean_obliquity_rad);
}

static bool linear(double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return linear_precession_matrix(jd_tt, data, out, out_mean_obliquity_rad);
}


// --- Ayanamsa Wrapper Functions ---

static bool fagan_bradley(double jd_tt, const void* /*data*/, double* out_ayanamsa_deg) noexcept {
    return fagan_bradley_ayanamsa(jd_tt, out_ayanamsa_deg);
}

static bool lahiri(double jd_tt, const void* /*data*/, double* out_ayanamsa_deg) noexcept {
    return lahiri_ayanamsa(jd_tt, out_ayanamsa_deg);
}

static bool raman(double jd_tt, const void* /*data*/, double* out_ayanamsa_deg) noexcept {
    return raman_ayanamsa(jd_tt, out_ayanamsa_deg);
}

static bool krishnamurti(double jd_tt, const void* /*data*/, double* out_ayanamsa_deg) noexcept {
    return krishnamurti_ayanamsa(jd_tt, out_ayanamsa_deg);
}

static bool true_chitrapaksha(double jd_tt, const void* data, double* out_ayanamsa_deg) noexcept {
    if (!data) return false;
    const auto* ctx = static_cast<const AyanamsaDispatchData*>(data);
    
    Matrix3x3 precession_matrix;
    double mean_obliquity_rad = 0.0;
    // Evaluate using core dispatch registry (which contains our dynamically injected precession models!)
    if (!dispatch::eval_precession(ctx->precession_model_id, jd_tt, nullptr, &precession_matrix, &mean_obliquity_rad)) {
        return false;
    }
    
    return true_chitrapaksha_ayanamsa(
        jd_tt, 
        precession_matrix, 
        ctx->true_obliquity_rad, 
        *(ctx->dynamic_block), 
        out_ayanamsa_deg
    );
}

static bool galactic_center(double jd_tt, const void* data, double* out_ayanamsa_deg) noexcept {
    if (!data) return false;
    const auto* ctx = static_cast<const AyanamsaDispatchData*>(data);
    
    Matrix3x3 precession_matrix;
    double mean_obliquity_rad = 0.0;
    // Evaluate using core dispatch registry (which contains our dynamically injected precession models!)
    if (!dispatch::eval_precession(ctx->precession_model_id, jd_tt, nullptr, &precession_matrix, &mean_obliquity_rad)) {
        return false;
    }
    
    return galactic_center_ayanamsa(
        jd_tt, 
        precession_matrix, 
        ctx->true_obliquity_rad, 
        *(ctx->dynamic_block), 
        out_ayanamsa_deg
    );
}

// --- Dynamic Registration Initialization ---

namespace wrappers {
void register_builtin_astrology_wrappers() {
    static bool registered = (
        // 1. Inject custom 3D Precession models into astronomical core dispatch registry (using system reserved 900+ IDs)
        dispatch::register_precession_model(PRECESSION_NEWCOMB_B1900, newcomb_b1900),
        dispatch::register_precession_model(PRECESSION_LINEAR, linear),
        // 2. Register astrology-specific Ayanamsa models
        register_ayanamsa_model(AYANAMSA_FAGAN_BRADLEY, fagan_bradley),
        register_ayanamsa_model(AYANAMSA_LAHIRI, lahiri),
        register_ayanamsa_model(AYANAMSA_RAMAN, raman),
        register_ayanamsa_model(AYANAMSA_KRISHNAMURTI, krishnamurti),
        register_ayanamsa_model(AYANAMSA_TRUE_CHITRAPAKSHA, true_chitrapaksha),
        register_ayanamsa_model(AYANAMSA_GALACTIC_CENTER, galactic_center),
        true
    );
    (void)registered;
}
} // namespace wrappers

} // namespace taiyin
