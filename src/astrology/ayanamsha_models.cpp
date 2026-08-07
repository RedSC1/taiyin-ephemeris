#include "taiyin/astrology/sidereal.h"

#include "sidereal_internal.h"

#include "runtime/apparent/builtin_star_position.h"

#include "taiyin/angle.h"
#include "taiyin/coordinates.h"
#include "taiyin/dispatch.h"
#include "taiyin/time.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace taiyin {
namespace astrology {
namespace {

struct ReferenceEpochAyanamshaData {
    double reference_jd_tt;
    double offset_rad;
    int reference_precession_model_id;
};

struct StarAnchoredAyanamshaData {
    const runtime::BuiltinStarAstrometry* astrometry;
    double assigned_longitude_rad;
};

bool model_epoch(double jd, SplitJulianDate* out) noexcept {
    return split_julian_date_from_double(jd, out);
}

constexpr double kJ1900 = 2415020.0;
// Fagan/Bradley's Besselian B1950.0 reference epoch.
constexpr double kFaganBradleyReferenceJd = 2433282.42346;

const ReferenceEpochAyanamshaData kFaganBradleyData = {
    kFaganBradleyReferenceJd,
    deg_to_rad(24.0 + 2.0 / 60.0 + 31.36 / 3600.0),
    dispatch::PRECESSION_NEWCOMB1895
};
const ReferenceEpochAyanamshaData kLahiriData = {
    2435553.5,
    // Standard mean-Lahiri anchor used by modern sidereal ephemerides.
    // The separately published 23d15'00.658" value is a true-equinoctial
    // convention and will be exposed later as a distinct ICRC mode.
    deg_to_rad(23.245524742777778),
    dispatch::PRECESSION_IAU1976
};
const ReferenceEpochAyanamshaData kRamanData = {
    kJ1900,
    deg_to_rad(360.0 - 338.98556),
    dispatch::PRECESSION_NEWCOMB1895
};
const ReferenceEpochAyanamshaData kKrishnamurtiData = {
    kJ1900,
    deg_to_rad(360.0 - 337.636111),
    dispatch::PRECESSION_NEWCOMB1895
};

// Values are the ICRF/J2000 records distributed with a fixed-star catalog.
// Keeping them here makes these ayanamshas independent of caller-installed
// TSC1/TSF1 catalogs.
const runtime::BuiltinStarAstrometry kSpicaAstrometry = {
    deg_to_rad(201.298247375), deg_to_rad(-11.161319472222222),
    -42.35, -30.67, 13.06, -10.0, JD_J2000
};
const runtime::BuiltinStarAstrometry kSgrAAstrometry = {
    deg_to_rad(266.416816625), deg_to_rad(-29.007824972222222),
    -2.755718425, -5.547, 0.125, 0.0, JD_J2000
};
const StarAnchoredAyanamshaData kSgrAData = {
    &kSgrAAstrometry, deg_to_rad(240.0)
};
const StarAnchoredAyanamshaData kTrueChitraData = {
    &kSpicaAstrometry, TAIYIN_PI
};

bool valid_sidereal_flags(uint64_t flags) noexcept {
    const uint64_t policy_flags = flags & TAIYIN_SIDEREAL_PRECESSION_POLICY_FLAGS;
    return (flags & ~TAIYIN_SIDEREAL_KNOWN_FLAGS) == 0u
        && !(policy_flags != 0u && (policy_flags & (policy_flags - 1u)) != 0u);
}

constexpr uint32_t kAyanamshaCorrectionFlags =
    runtime::TAIYIN_NATIVE_POSITION_TRUEPOS
    | runtime::TAIYIN_NATIVE_POSITION_NO_ABERR
    | runtime::TAIYIN_NATIVE_POSITION_NO_GDEFL
    | runtime::TAIYIN_NATIVE_POSITION_ASTROMETRIC;

bool eval_ecliptic_matrix(int precession_model_id, SplitJulianDate jd_tt, Matrix3x3* out) noexcept {
    if (!out) return false;
    Matrix3x3 precession;
    double obliquity = 0.0;
    if (!dispatch::eval_precession(
            precession_model_id, jd_tt, nullptr, &precession, &obliquity)) {
        return false;
    }
    *out = matrix3x3_multiply(rotation_x_matrix(obliquity), precession);
    return true;
}

bool ayanamsha_raw(
    const ReferenceEpochAyanamshaData& definition,
    int precession_model_id,
    SplitJulianDate jd_tt,
    double* out
) noexcept {
    Matrix3x3 date_ecliptic;
    Matrix3x3 reference_ecliptic;
    SplitJulianDate reference_jd_tt;
    if (!model_epoch(definition.reference_jd_tt, &reference_jd_tt)) {
        return false;
    }
    if (!eval_ecliptic_matrix(precession_model_id, jd_tt, &date_ecliptic)
        || !eval_ecliptic_matrix(
            precession_model_id, reference_jd_tt, &reference_ecliptic)) {
        return false;
    }
    const Vector3 date_equinox = { 1.0, 0.0, 0.0 };
    const Vector3 in_j2000 = matrix3x3_multiply_vector(
        matrix3x3_transpose(date_ecliptic), date_equinox);
    const Vector3 at_reference = matrix3x3_multiply_vector(reference_ecliptic, in_j2000);
    *out = normalize_radians(definition.offset_rad - std::atan2(at_reference.y, at_reference.x));
    return std::isfinite(*out);
}

bool ayanamsha_compensation(
    const ReferenceEpochAyanamshaData& definition,
    int current_precession_model_id,
    double* out
) noexcept {
    Matrix3x3 current;
    Matrix3x3 reference;
    double reference_obliquity = 0.0;
    SplitJulianDate reference_jd_tt;
    if (!model_epoch(definition.reference_jd_tt, &reference_jd_tt)) {
        return false;
    }
    if (!dispatch::eval_precession(
            current_precession_model_id, reference_jd_tt, nullptr,
            &current, nullptr)
        || !dispatch::eval_precession(
            definition.reference_precession_model_id, reference_jd_tt,
            nullptr, &reference, &reference_obliquity)) {
        return false;
    }
    const Vector3 current_equinox = { 1.0, 0.0, 0.0 };
    const Vector3 in_j2000 = matrix3x3_multiply_vector(
        matrix3x3_transpose(current), current_equinox);
    const Vector3 in_reference_equator = matrix3x3_multiply_vector(reference, in_j2000);
    const Vector3 in_reference_ecliptic = matrix3x3_multiply_vector(
        rotation_x_matrix(reference_obliquity), in_reference_equator);
    *out = -std::atan2(in_reference_ecliptic.y, in_reference_ecliptic.x);
    return std::isfinite(*out);
}

Status eval_reference_epoch_ayanamsha(
    const AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    const ReferenceEpochAyanamshaData* definition =
        data ? static_cast<const ReferenceEpochAyanamshaData*>(data->model_data) : nullptr;
    if (!data || !data->native_context
        || !definition || !out_ayanamsha_rad
        || !split_julian_date_is_finite(data->jd_tt)
        || !valid_sidereal_flags(data->sidereal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int current_model =
        data->native_context->model_context.precession_model_id;
    const int model =
        ((data->sidereal_flags & TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION) != 0u
            && definition->reference_precession_model_id >= 0)
        ? definition->reference_precession_model_id : current_model;
    double result = 0.0;
    if (!ayanamsha_raw(*definition, model, data->jd_tt, &result)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if ((data->sidereal_flags & TAIYIN_SIDEREAL_PRECESSION_POLICY_FLAGS) == 0u) {
        double correction = 0.0;
        if (!ayanamsha_compensation(*definition, current_model, &correction)) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        result = normalize_radians(result + correction);
    }
    *out_ayanamsha_rad = result;
    return TAIYIN_STATUS_OK;
}

Status eval_star_anchored_ayanamsha(
    const AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) {
    const StarAnchoredAyanamshaData* definition =
        data ? static_cast<const StarAnchoredAyanamshaData*>(data->model_data) : nullptr;
    if (!data || !data->native_context || !definition || !definition->astrometry
        || !out_ayanamsha_rad || !split_julian_date_is_finite(data->jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    runtime::NativeCalcContext native;
    Status status = internal::effective_native_context(
        data->native_context, data->ayanamsha_id, data->sidereal_flags, &native);
    if (status != TAIYIN_STATUS_OK) return status;
    const uint64_t star_flags = runtime::TAIYIN_NATIVE_POSITION_RADIANS
        | runtime::TAIYIN_NATIVE_POSITION_NONUT
        | (data->native_position_flags & (
            runtime::TAIYIN_NATIVE_POSITION_TRUEPOS
            | runtime::TAIYIN_NATIVE_POSITION_ASTROMETRIC
            | runtime::TAIYIN_NATIVE_POSITION_NO_ABERR
            | runtime::TAIYIN_NATIVE_POSITION_NO_GDEFL));
    double position[6] = {};
    status = runtime::calc_builtin_star_position_tt(
        &native, *definition->astrometry, data->jd_tt, star_flags, position, nullptr);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_ayanamsha_rad = normalize_radians(position[0] - definition->assigned_longitude_rad);
    return std::isfinite(*out_ayanamsha_rad)
        ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

std::mutex& ayanamsha_model_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::unordered_map<int, AyanamshaModelEntry>& ayanamsha_models() {
    static std::unordered_map<int, AyanamshaModelEntry> models = [] {
        std::unordered_map<int, AyanamshaModelEntry> value;
        value[TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY] = AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY,
            &eval_reference_epoch_ayanamsha,
            kFaganBradleyData.reference_precession_model_id,
            &kFaganBradleyData);
        value[TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI] = AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_LAHIRI,
            &eval_reference_epoch_ayanamsha,
            kLahiriData.reference_precession_model_id,
            &kLahiriData);
        value[TAIYIN_SIDEREAL_AYANAMSHA_RAMAN] = AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_RAMAN,
            &eval_reference_epoch_ayanamsha,
            kRamanData.reference_precession_model_id,
            &kRamanData);
        value[TAIYIN_SIDEREAL_AYANAMSHA_KRISHNAMURTI] = AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_KRISHNAMURTI,
            &eval_reference_epoch_ayanamsha,
            kKrishnamurtiData.reference_precession_model_id,
            &kKrishnamurtiData);
        value[TAIYIN_SIDEREAL_AYANAMSHA_GALACTIC_CENTER_0_SAGITTARIUS] =
            AyanamshaModelEntry(
                TAIYIN_SIDEREAL_AYANAMSHA_GALACTIC_CENTER_0_SAGITTARIUS,
                &eval_star_anchored_ayanamsha,
                -1,
                &kSgrAData);
        value[TAIYIN_SIDEREAL_AYANAMSHA_TRUE_CHITRA] = AyanamshaModelEntry(
            TAIYIN_SIDEREAL_AYANAMSHA_TRUE_CHITRA,
            &eval_star_anchored_ayanamsha,
            -1,
            &kTrueChitraData);
        return value;
    }();
    return models;
}

}  // namespace

AyanamshaDispatchData::AyanamshaDispatchData() noexcept
    : native_context(nullptr),
      ayanamsha_id(TAIYIN_SIDEREAL_AYANAMSHA_FAGAN_BRADLEY),
      jd_tt(0, NAN),
      native_position_flags(0),
      sidereal_flags(0),
      model_data(nullptr) {}

AyanamshaModelEntry::AyanamshaModelEntry() noexcept
    : model_id(-1),
      eval(nullptr),
      reference_precession_model_id(-1),
      model_data(nullptr) {}

AyanamshaModelEntry::AyanamshaModelEntry(
    int model_id_value,
    AyanamshaFn eval_value,
    int reference_precession_model_id_value,
    const void* model_data_value
) noexcept
    : model_id(model_id_value),
      eval(eval_value),
      reference_precession_model_id(reference_precession_model_id_value),
      model_data(model_data_value) {}

bool add_ayanamsha_model(const AyanamshaModelEntry& entry) noexcept {
    if (entry.model_id < TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START || !entry.eval) {
        return false;
    }
    if (entry.reference_precession_model_id >= 0) {
        dispatch::PrecessionModelEntry precession;
        if (!dispatch::find_precession_model(
                entry.reference_precession_model_id, &precession)) {
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(ayanamsha_model_mutex());
    std::unordered_map<int, AyanamshaModelEntry>& models = ayanamsha_models();
    if (models.find(entry.model_id) != models.end()) return false;
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    return true;
}

bool remove_ayanamsha_model(int model_id) noexcept {
    return remove_ayanamsha_model_if_matches(model_id, nullptr, nullptr);
}

bool remove_ayanamsha_model_if_matches(
    int model_id,
    AyanamshaFn expected_eval,
    const void* expected_model_data
) noexcept {
    if (model_id < TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START) return false;
    try {
        std::lock_guard<std::mutex> lock(ayanamsha_model_mutex());
        std::unordered_map<int, AyanamshaModelEntry>& models = ayanamsha_models();
        const std::unordered_map<int, AyanamshaModelEntry>::iterator it =
            models.find(model_id);
        if (it == models.end()
            || (expected_eval
                && (it->second.eval != expected_eval
                    || it->second.model_data != expected_model_data))) {
            return false;
        }
        models.erase(it);
        return true;
    } catch (...) {
        return false;
    }
}

bool find_ayanamsha_model(int model_id, AyanamshaModelEntry* out) noexcept {
    if (out) *out = AyanamshaModelEntry();
    if (!out) return false;
    std::lock_guard<std::mutex> lock(ayanamsha_model_mutex());
    const std::unordered_map<int, AyanamshaModelEntry>& models = ayanamsha_models();
    const std::unordered_map<int, AyanamshaModelEntry>::const_iterator it =
        models.find(model_id);
    if (it == models.end() || !it->second.eval) return false;
    *out = it->second;
    return true;
}

bool has_ayanamsha_model(int model_id) noexcept {
    AyanamshaModelEntry entry;
    return find_ayanamsha_model(model_id, &entry);
}

Status eval_ayanamsha_model(
    int model_id,
    const AyanamshaDispatchData* data,
    double* out_ayanamsha_rad
) noexcept {
    if (out_ayanamsha_rad) *out_ayanamsha_rad = NAN;
    AyanamshaModelEntry entry;
    if (!data || !out_ayanamsha_rad || !find_ayanamsha_model(model_id, &entry)
        || !entry.eval) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    AyanamshaDispatchData dispatch_data = *data;
    dispatch_data.model_data = entry.model_data;
    try {
        const Status status = entry.eval(&dispatch_data, out_ayanamsha_rad);
        if (status != TAIYIN_STATUS_OK) {
            *out_ayanamsha_rad = NAN;
            return status;
        }
    } catch (...) {
        *out_ayanamsha_rad = NAN;
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (!std::isfinite(*out_ayanamsha_rad)) {
        *out_ayanamsha_rad = NAN;
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_ayanamsha_rad = normalize_radians(*out_ayanamsha_rad);
    return TAIYIN_STATUS_OK;
}

namespace internal {

uint32_t ayanamsha_evaluation_flags(uint32_t native_position_flags) noexcept {
    return native_position_flags & kAyanamshaCorrectionFlags;
}

uint64_t ayanamsha_context_flags(uint64_t sidereal_flags) noexcept {
    return (sidereal_flags & ~TAIYIN_SIDEREAL_POSITION_FLAGS_MASK)
        | ayanamsha_evaluation_flags(static_cast<uint32_t>(sidereal_flags));
}

Status effective_native_context(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    uint64_t sidereal_flags,
    runtime::NativeCalcContext* out
) noexcept {
    if (!native_context || !out || !valid_sidereal_flags(sidereal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    AyanamshaModelEntry entry;
    if (!find_ayanamsha_model(ayanamsha_id, &entry)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = *native_context;
    if ((sidereal_flags & TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION) != 0u
        && entry.reference_precession_model_id >= 0) {
        out->model_context.precession_model_id = entry.reference_precession_model_id;
    }
    out->apparent_options.model_context = &out->model_context;
    out->apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
    out->apparent_options.custom_output_frame_evaluator = 0;
    out->apparent_options.custom_output_frame_data = 0;
    return TAIYIN_STATUS_OK;
}

Status calc_ayanamsha_tt_with_position_flags(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    SplitJulianDate jd_tt,
    uint64_t native_position_flags,
    uint64_t sidereal_flags,
    double* out_ayanamsha_rad
) noexcept {
    if (!native_context || !out_ayanamsha_rad
        || !split_julian_date_is_finite(jd_tt)
        || !valid_sidereal_flags(sidereal_flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    runtime::NativeCalcContext effective;
    Status status = effective_native_context(
        native_context, ayanamsha_id, sidereal_flags, &effective);
    if (status != TAIYIN_STATUS_OK) return status;
    AyanamshaDispatchData data;
    data.native_context = &effective;
    data.ayanamsha_id = ayanamsha_id;
    data.jd_tt = jd_tt;
    data.native_position_flags = ayanamsha_evaluation_flags(
        static_cast<uint32_t>(native_position_flags));
    data.sidereal_flags = ayanamsha_context_flags(sidereal_flags);
    return eval_ayanamsha_model(ayanamsha_id, &data, out_ayanamsha_rad);
}

Status calc_longitude_nutation_tt(
    const runtime::NativeCalcContext* native_context,
    SplitJulianDate jd_tt,
    uint64_t native_position_flags,
    double* out_dpsi_rad
) noexcept {
    if (!native_context || !out_dpsi_rad
        || !split_julian_date_is_finite(jd_tt)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_dpsi_rad = 0.0;
    if ((native_position_flags & runtime::TAIYIN_NATIVE_POSITION_NONUT) != 0u) {
        return TAIYIN_STATUS_OK;
    }
    NutationAngles nutation;
    if (!dispatch::eval_nutation(
            native_context->model_context.nutation_model_id,
            jd_tt,
            nullptr,
            &nutation)
        || !std::isfinite(nutation.dpsi_rad)) {
        *out_dpsi_rad = NAN;
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out_dpsi_rad = nutation.dpsi_rad;
    return TAIYIN_STATUS_OK;
}

}  // namespace internal

Status calc_ayanamsha_tt(
    const runtime::NativeCalcContext* native_context,
    int ayanamsha_id,
    SplitJulianDate jd_tt,
    uint64_t flags,
    double* out_ayanamsha_rad
) noexcept {
    if ((flags & (TAIYIN_SIDEREAL_REFERENCE_PLANE_FLAGS
            | TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1)) != 0u) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    Status status = internal::calc_ayanamsha_tt_with_position_flags(
        native_context, ayanamsha_id, jd_tt,
        flags & TAIYIN_SIDEREAL_POSITION_FLAGS_MASK, flags, out_ayanamsha_rad);
    if (status != TAIYIN_STATUS_OK) return status;
    double dpsi_rad = 0.0;
    status = internal::calc_longitude_nutation_tt(
        native_context, jd_tt, flags, &dpsi_rad);
    if (status != TAIYIN_STATUS_OK) {
        *out_ayanamsha_rad = NAN;
        return status;
    }
    *out_ayanamsha_rad = normalize_radians(
        *out_ayanamsha_rad + dpsi_rad);
    return TAIYIN_STATUS_OK;
}

}  // namespace astrology
}  // namespace taiyin
