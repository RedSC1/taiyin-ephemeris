#include "taiyin/runtime/event_search.h"

#include "runtime/visibility/visibility_sampling_internal.h"
#include "runtime/visibility/visibility_search_internal.h"
#include "runtime/visibility/visibility_math_internal.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/math_solvers.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/star_position.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace taiyin {
namespace runtime {
namespace {

const int MAX_LONGITUDE_ITERATIONS = 48;
const double LONGITUDE_TOLERANCE_RAD = 1.0e-10;
const double LONGITUDE_NUMERIC_FLOOR_RAD = 1.0e-9;
const double MIN_LONGITUDE_RATE_RAD_PER_DAY = 1.0e-8;
const double STATION_SPEED_TOLERANCE_RAD_PER_DAY = 1.0e-12;
const double MIN_STATION_ACCELERATION_RAD_PER_DAY2 = 1.0e-12;
const double MAX_NEWTON_STEP_DAYS = 45.0;
const double MAX_LONGITUDE_SEARCH_DAYS = 400.0;
const int MAX_LONGITUDE_BRACKET_STEPS = 512;
const int MAX_BOUNDED_SEARCH_STEPS = 100000;
const double EVENT_TIME_DUPLICATE_TOLERANCE_DAYS = 1.0e-7;
const double DEFAULT_LUNAR_PHASE_STEP_DAYS = 1.0;
const double STATION_ASPECT_TOLERANCE_RAD = 1.0e-8;
const double STATION_ACCELERATION_STEP_DAYS = 1.0e-3;
const double ELONGATION_RATE_TOLERANCE_RAD_PER_DAY = 1.0e-12;
const double ELONGATION_TIME_TOLERANCE_DAYS = 1.0e-7;
const int MAX_ELONGATION_ITERATIONS = 64;
const double ANGULAR_SEPARATION_RATE_TOLERANCE_RAD_PER_DAY = 1.0e-12;
const double ANGULAR_SEPARATION_TIME_TOLERANCE_DAYS = 1.0e-7;
const int MAX_ANGULAR_SEPARATION_ITERATIONS = 64;
const double SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS = 1.0e-7;
const double SOLAR_TRANSIT_CONTACT_TIME_TOLERANCE_DAYS = 1.0e-7;
const int MAX_SOLAR_TRANSIT_CONTACT_ITERATIONS = 64;
const double SOLAR_TRANSIT_NODE_GATE_LATITUDE_RAD = 2.0 * TAIYIN_DEG_TO_RAD;
const double LOCAL_SOLAR_TRANSIT_MINIMUM_WINDOW_DAYS = 0.35;
const double LOCAL_SOLAR_TRANSIT_RISE_SET_PADDING_DAYS = 0.25;
const double LOCAL_SOLAR_TRANSIT_ALTITUDE_HORIZON_RAD = 0.0;
const int MAX_SOLAR_TRANSIT_CANDIDATE_CYCLES = 100000;

SplitJulianDate invalid_jd() noexcept {
    return SplitJulianDate(0, NAN);
}

struct AngleCrossingSample {
    double value_rad;
    double speed_rad_per_day;

    AngleCrossingSample() noexcept
        : value_rad(0.0), speed_rad_per_day(0.0) {}
};

typedef Status (*AngleCrossingEvalFn)(
    double x,
    void* user_data,
    AngleCrossingSample* out
);

struct AngleCrossingRootContext {
    AngleCrossingEvalFn eval;
    void* user_data;
    double start_value_rad;
    double target_progress_rad;
    AngleCrossingSample sample;
    Status last_status;
    int evaluation_count;

    AngleCrossingRootContext() noexcept
        : eval(0),
          user_data(0),
          start_value_rad(0.0),
          target_progress_rad(0.0),
          sample(),
          last_status(TAIYIN_STATUS_OK),
          evaluation_count(0) {}
};

struct SignedAngleRootContext {
    AngleCrossingEvalFn eval;
    void* user_data;
    double target_rad;
    double residual_sign;
    AngleCrossingSample sample;
    Status last_status;

    SignedAngleRootContext() noexcept
        : eval(0),
          user_data(0),
          target_rad(0.0),
          residual_sign(1.0),
          sample(),
          last_status(TAIYIN_STATUS_OK) {}
};

struct BodyLongitudeEvalContext {
    const NativeCalcContext* context;
    int body_id;
    uint32_t flags;
    bool use_ut;
    SplitJulianDate estimate_jd;
    double time_direction;
    EphemerisEvalDiagnostic* diagnostic;

    BodyLongitudeEvalContext() noexcept
        : context(0),
          body_id(0),
          flags(0),
          use_ut(true),
          estimate_jd(),
          time_direction(1.0),
          diagnostic(0) {}
};

struct BodyAspectEvalContext {
    const NativeCalcContext* context;
    int body_a_id;
    int body_b_id;
    uint32_t flags;
    bool use_ut;
    SplitJulianDate estimate_jd;
    EphemerisEvalDiagnostic* diagnostic;

    BodyAspectEvalContext() noexcept
        : context(0),
          body_a_id(0),
          body_b_id(0),
          flags(0),
          use_ut(true),
          estimate_jd(),
          diagnostic(0) {}
};

struct ElongationEvalSample {
    double elongation_rad;
    double elongation_rate_rad_per_day;
    double elongation_acceleration_rad_per_day2;
    double relative_longitude_rad;

    ElongationEvalSample() noexcept
        : elongation_rad(0.0),
          elongation_rate_rad_per_day(0.0),
          elongation_acceleration_rad_per_day2(0.0),
          relative_longitude_rad(0.0) {}
};

struct ElongationEvalContext {
    const NativeCalcContext* context;
    int body_id;
    uint32_t flags;
    EphemerisEvalDiagnostic* diagnostic;
    int evaluation_count;

    ElongationEvalContext() noexcept
        : context(0),
          body_id(0),
          flags(0),
          diagnostic(0),
          evaluation_count(0) {}
};

struct AngularSeparationEvalSample {
    double separation_rad;
    double separation_rate_rad_per_day;
    double separation_acceleration_rad_per_day2;

    AngularSeparationEvalSample() noexcept
        : separation_rad(0.0),
          separation_rate_rad_per_day(0.0),
          separation_acceleration_rad_per_day2(0.0) {}
};

struct AngularSeparationEvalContext {
    const NativeCalcContext* context;
    int body_a_id;
    int body_b_id;
    const char* star_key;
    uint32_t flags;
    bool use_ut;
    EphemerisEvalDiagnostic* diagnostic;
    int evaluation_count;

    AngularSeparationEvalContext() noexcept
        : context(0),
          body_a_id(0),
          body_b_id(0),
          star_key(0),
          flags(0),
          use_ut(true),
          diagnostic(0),
          evaluation_count(0) {}
};

struct SolarTransitContactContext {
    const NativeCalcContext* context;
    int body_id;
    uint32_t flags;
    double body_radius_sign;
    EphemerisEvalDiagnostic* diagnostic;
    int evaluation_count;

    SolarTransitContactContext() noexcept
        : context(0),
          body_id(0),
          flags(0),
          body_radius_sign(1.0),
          diagnostic(0),
          evaluation_count(0) {}
};

struct LocalSolarTransitEvalContext {
    const NativeCalcContext* context;
    NativeObserverLocation location;
    int body_id;
    uint32_t flags;
    EphemerisEvalDiagnostic* diagnostic;
    int evaluation_count;

    LocalSolarTransitEvalContext() noexcept
        : context(0),
          location(),
          body_id(0),
          flags(0),
          diagnostic(0),
          evaluation_count(0) {}
};

Status compute_geocentric_solar_transit_candidate(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate conjunction_jd_ut,
    uint32_t position_flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

struct StationRootContext {
    AngleCrossingEvalFn eval;
    void* user_data;
    double lower_x;
    double upper_x;
    AngleCrossingSample sample;
    Status last_status;

    StationRootContext() noexcept
        : eval(0),
          user_data(0),
          lower_x(0.0),
          upper_x(0.0),
          sample(),
          last_status(TAIYIN_STATUS_OK) {}
};

bool finite_position_speed(const double values[6]) noexcept {
    return std::isfinite(values[0])
        && std::isfinite(values[1])
        && std::isfinite(values[2])
        && std::isfinite(values[3]);
}

double angle_progress(double value_rad, double start_value_rad) noexcept {
    return normalize_radians(value_rad - start_value_rad);
}

bool angle_root_reached_numeric_floor(
    double residual,
    double last_evaluated_x,
    double next_x
) noexcept {
    // At multi-million-day epochs, adding a sub-microsecond local correction to
    // an absolute double JD can round back to the same representable instant.
    return next_x == last_evaluated_x
        && std::fabs(residual) <= LONGITUDE_NUMERIC_FLOOR_RAD;
}

bool is_coverage_status(Status status) noexcept {
    return status == TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_COVERAGE_GAP
        || status == TAIYIN_EPHEMERIS_ERROR_NO_ROUTE
        || status == TAIYIN_EPHEMERIS_ERROR_COMPOSITE_MISSING_COMPONENT;
}

void set_basic_diagnostic(
    EphemerisEvalDiagnostic* diagnostic,
    Status status,
    int target_id,
    SplitJulianDate jd_ut
) noexcept {
    if (!diagnostic) {
        return;
    }
    *diagnostic = EphemerisEvalDiagnostic();
    diagnostic->status = status;
    diagnostic->target_id = target_id;
    diagnostic->jd_tdb = jd_ut;
}

Status normalize_event_status(Status status) noexcept {
    return is_coverage_status(status) ? TAIYIN_EVENT_ERROR_NOT_FOUND : status;
}

bool eval_angle_crossing_root(double x, void* user_data, BracketedRootEval* out) noexcept {
    AngleCrossingRootContext* data = static_cast<AngleCrossingRootContext*>(user_data);
    if (!data || !out) {
        return false;
    }

    const Status status = data->eval(x, data->user_data, &data->sample);
    ++data->evaluation_count;
    data->last_status = status;
    if (status != TAIYIN_STATUS_OK) {
        return false;
    }
    if (!std::isfinite(data->sample.value_rad)
        || !std::isfinite(data->sample.speed_rad_per_day)
        || data->sample.speed_rad_per_day < MIN_LONGITUDE_RATE_RAD_PER_DAY) {
        data->last_status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        return false;
    }

    out->value = angle_progress(data->sample.value_rad, data->start_value_rad)
        - data->target_progress_rad;
    out->derivative = data->sample.speed_rad_per_day;
    return true;
}

bool eval_signed_angle_crossing_root(double x, void* user_data, BracketedRootEval* out) noexcept {
    SignedAngleRootContext* data = static_cast<SignedAngleRootContext*>(user_data);
    if (!data || !out) {
        return false;
    }

    const Status status = data->eval(x, data->user_data, &data->sample);
    data->last_status = status;
    if (status != TAIYIN_STATUS_OK) {
        return false;
    }
    if (!std::isfinite(data->sample.value_rad)
        || !std::isfinite(data->sample.speed_rad_per_day)) {
        data->last_status = TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        return false;
    }

    out->value = data->residual_sign
        * angular_difference_radians(data->sample.value_rad, data->target_rad);
    out->derivative = data->residual_sign * data->sample.speed_rad_per_day;
    return true;
}

Status solve_forward_angle_crossing_newton_bisect(
    AngleCrossingEvalFn eval,
    void* user_data,
    double target_rad,
    double max_search_days,
    double* out_x
) noexcept {
    if (!eval
        || !out_x
        || !std::isfinite(target_rad)
        || !(max_search_days > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_x = 0.0;

    const double target = normalize_radians(target_rad);
    AngleCrossingSample start_sample;
    Status status = eval(0.0, user_data, &start_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(start_sample.value_rad)
        || !std::isfinite(start_sample.speed_rad_per_day)
        || start_sample.speed_rad_per_day < MIN_LONGITUDE_RATE_RAD_PER_DAY) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double start_value = normalize_radians(start_sample.value_rad);
    const double start_rate = start_sample.speed_rad_per_day;
    const double start_error = angular_difference_radians(target, start_value);
    if (std::fabs(start_error) <= LONGITUDE_TOLERANCE_RAD) {
        return TAIYIN_STATUS_OK;
    }

    const double target_progress = normalize_radians(target - start_value);
    double lower_x = 0.0;
    double lower_progress = 0.0;
    double lower_residual = -target_progress;
    AngleCrossingSample lower_sample = start_sample;
    double upper_x = 0.0;
    AngleCrossingSample sample;
    double upper_residual = 0.0;
    bool bracketed = false;

    for (int bracket_step = 0; bracket_step < MAX_LONGITUDE_BRACKET_STEPS; ++bracket_step) {
        const double max_phase_step_days =
            (0.5 * TAIYIN_PI) / lower_sample.speed_rad_per_day;
        const double step_days = std::min(2.0, max_phase_step_days);
        upper_x = std::min(max_search_days, lower_x + step_days);
        if (!(upper_x > lower_x)) break;

        status = eval(upper_x, user_data, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!std::isfinite(sample.value_rad)
            || !std::isfinite(sample.speed_rad_per_day)
            || sample.speed_rad_per_day < MIN_LONGITUDE_RATE_RAD_PER_DAY) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }

        const double upper_progress = lower_progress
            + angle_progress(sample.value_rad, lower_sample.value_rad);
        upper_residual = upper_progress - target_progress;
        if (upper_residual >= 0.0) {
            bracketed = true;
            break;
        }
        if (upper_x >= max_search_days) {
            break;
        }
        lower_x = upper_x;
        lower_progress = upper_progress;
        lower_residual = upper_residual;
        lower_sample = sample;
    }

    if (!bracketed || !(lower_residual <= 0.0 && upper_residual >= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    double x = lower_x
        + (upper_x - lower_x) * (-lower_residual)
            / (upper_residual - lower_residual);
    if (!(x > lower_x && x < upper_x)) {
        x = 0.5 * (lower_x + upper_x);
    }

    AngleCrossingRootContext root_context;
    root_context.eval = eval;
    root_context.user_data = user_data;
    root_context.start_value_rad = lower_sample.value_rad;
    root_context.target_progress_rad = target_progress - lower_progress;

    double root_x = 0.0;
    double residual = 0.0;
    double residual_speed = 0.0;
    double last_evaluated_x = 0.0;
    double next_x = 0.0;
    int iteration_count = 0;
    int evaluation_count = 0;
    const bool solved = solve_bracketed_newton_bisection(
        eval_angle_crossing_root,
        &root_context,
        lower_x,
        lower_residual,
        upper_x,
        upper_residual,
        x,
        LONGITUDE_TOLERANCE_RAD,
        MIN_LONGITUDE_RATE_RAD_PER_DAY,
        MAX_NEWTON_STEP_DAYS,
        MAX_LONGITUDE_ITERATIONS,
        &root_x,
        &residual,
        &residual_speed,
        &last_evaluated_x,
        &next_x,
        &iteration_count,
        &evaluation_count);

    if (!solved
        && !(root_context.last_status == TAIYIN_STATUS_OK
            && angle_root_reached_numeric_floor(residual, last_evaluated_x, next_x))) {
        if (root_context.last_status != TAIYIN_STATUS_OK) {
            return normalize_event_status(root_context.last_status);
        }
        return iteration_count > 0 ? TAIYIN_EVENT_ERROR_NOT_FOUND : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    *out_x = last_evaluated_x;
    return TAIYIN_STATUS_OK;
}

bool append_event_time(
    SplitJulianDate jd,
    SplitJulianDate* out_jd,
    size_t max_event_count,
    size_t* event_count
) noexcept {
    if (!out_jd || !event_count || !split_julian_date_is_finite(jd)) {
        return false;
    }
    if (*event_count > 0
        && std::fabs(jd - out_jd[*event_count - 1]) <= EVENT_TIME_DUPLICATE_TOLERANCE_DAYS) {
        return true;
    }
    if (*event_count >= max_event_count) {
        return false;
    }
    out_jd[*event_count] = jd;
    ++(*event_count);
    return true;
}

bool append_station_event(
    SplitJulianDate jd,
    double longitude_rad,
    SplitJulianDate* out_jd,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* event_count
) noexcept {
    if (!out_jd || !event_count || !split_julian_date_is_finite(jd) || !std::isfinite(longitude_rad)) {
        return false;
    }
    if (*event_count > 0
        && std::fabs(jd - out_jd[*event_count - 1]) <= EVENT_TIME_DUPLICATE_TOLERANCE_DAYS) {
        return true;
    }
    if (*event_count >= max_event_count) {
        return false;
    }
    out_jd[*event_count] = jd;
    if (out_longitude_rad) {
        out_longitude_rad[*event_count] = normalize_radians(longitude_rad);
    }
    ++(*event_count);
    return true;
}

bool same_target(double a, double b) noexcept {
    return std::fabs(angular_difference_radians(a, b)) <= LONGITUDE_TOLERANCE_RAD;
}

bool append_exact_aspect_event(
    SplitJulianDate jd,
    double target_aspect_rad,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* event_count
) noexcept {
    if (!out_jd || !event_count || !split_julian_date_is_finite(jd) || !std::isfinite(target_aspect_rad)) {
        return false;
    }
    const double target = normalize_radians(target_aspect_rad);
    for (size_t i = 0; i < *event_count; ++i) {
        if (std::fabs(jd - out_jd[i]) <= EVENT_TIME_DUPLICATE_TOLERANCE_DAYS
            && (!out_target_aspect_rad || same_target(target, out_target_aspect_rad[i]))) {
            return true;
        }
    }
    if (*event_count >= max_event_count) {
        return false;
    }
    out_jd[*event_count] = jd;
    if (out_target_aspect_rad) {
        out_target_aspect_rad[*event_count] = target;
    }
    ++(*event_count);
    return true;
}

void sort_exact_aspect_events(
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t event_count
) noexcept {
    if (!out_jd) {
        return;
    }
    for (size_t i = 1; i < event_count; ++i) {
        const SplitJulianDate jd = out_jd[i];
        const double target = out_target_aspect_rad ? out_target_aspect_rad[i] : 0.0;
        size_t j = i;
        while (j > 0 && out_jd[j - 1] > jd) {
            out_jd[j] = out_jd[j - 1];
            if (out_target_aspect_rad) {
                out_target_aspect_rad[j] = out_target_aspect_rad[j - 1];
            }
            --j;
        }
        out_jd[j] = jd;
        if (out_target_aspect_rad) {
            out_target_aspect_rad[j] = target;
        }
    }
}

void compact_exact_aspect_events(
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t* event_count
) noexcept {
    if (!out_jd || !event_count || *event_count == 0) {
        return;
    }
    size_t write_index = 1;
    for (size_t read_index = 1; read_index < *event_count; ++read_index) {
        const bool duplicate =
            std::fabs(out_jd[read_index] - out_jd[write_index - 1]) <= EVENT_TIME_DUPLICATE_TOLERANCE_DAYS
            && (!out_target_aspect_rad
                || same_target(out_target_aspect_rad[read_index], out_target_aspect_rad[write_index - 1]));
        if (duplicate) {
            continue;
        }
        if (write_index != read_index) {
            out_jd[write_index] = out_jd[read_index];
            if (out_target_aspect_rad) {
                out_target_aspect_rad[write_index] = out_target_aspect_rad[read_index];
            }
        }
        ++write_index;
    }
    *event_count = write_index;
}

int aspect_targets(
    double aspect_separation_rad,
    double out_targets[2]
) noexcept {
    if (!out_targets || !std::isfinite(aspect_separation_rad)) {
        return 0;
    }
    double separation = normalize_radians(aspect_separation_rad);
    if (separation > TAIYIN_PI) {
        separation = TAIYIN_TWO_PI - separation;
    }
    if (std::fabs(separation) <= LONGITUDE_TOLERANCE_RAD) {
        out_targets[0] = 0.0;
        return 1;
    }
    if (std::fabs(separation - TAIYIN_PI) <= LONGITUDE_TOLERANCE_RAD) {
        out_targets[0] = TAIYIN_PI;
        return 1;
    }
    out_targets[0] = separation;
    out_targets[1] = TAIYIN_TWO_PI - separation;
    return 2;
}

Status build_unique_aspect_targets(
    const double* aspect_separations_rad,
    size_t aspect_count,
    std::vector<double>* out_targets
) noexcept {
    if (!aspect_separations_rad || aspect_count == 0 || !out_targets) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    try {
        out_targets->clear();
        out_targets->reserve(aspect_count * 2);
        for (size_t i = 0; i < aspect_count; ++i) {
            double targets[2] = {};
            const int target_count = aspect_targets(aspect_separations_rad[i], targets);
            if (target_count == 0) {
                out_targets->clear();
                return TAIYIN_ERROR_INVALID_ARGUMENT;
            }
            for (int target_index = 0; target_index < target_count; ++target_index) {
                bool exists = false;
                for (size_t j = 0; j < out_targets->size(); ++j) {
                    if (same_target(targets[target_index], (*out_targets)[j])) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    out_targets->push_back(normalize_radians(targets[target_index]));
                }
            }
        }
    } catch (...) {
        out_targets->clear();
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }

    return out_targets->empty() ? TAIYIN_ERROR_INVALID_ARGUMENT : TAIYIN_STATUS_OK;
}

Status refine_bounded_angle_crossing(
    AngleCrossingEvalFn eval,
    void* user_data,
    double target_rad,
    double lower_x,
    double lower_residual,
    double upper_x,
    double upper_residual,
    double* out_x
) noexcept {
    if (!eval
        || !out_x
        || !(lower_x < upper_x)
        || !std::isfinite(target_rad)
        || !std::isfinite(lower_residual)
        || !std::isfinite(upper_residual)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_x = 0.0;

    if (std::fabs(lower_residual) <= LONGITUDE_TOLERANCE_RAD) {
        *out_x = lower_x;
        return TAIYIN_STATUS_OK;
    }
    if (std::fabs(upper_residual) <= LONGITUDE_TOLERANCE_RAD) {
        *out_x = upper_x;
        return TAIYIN_STATUS_OK;
    }
    if (lower_residual * upper_residual > 0.0) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    double residual_sign = 1.0;
    double signed_lower = lower_residual;
    double signed_upper = upper_residual;
    if (signed_lower > signed_upper) {
        residual_sign = -1.0;
        signed_lower = -lower_residual;
        signed_upper = -upper_residual;
    }
    if (!(signed_lower <= 0.0 && signed_upper >= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    SignedAngleRootContext root_context;
    root_context.eval = eval;
    root_context.user_data = user_data;
    root_context.target_rad = normalize_radians(target_rad);
    root_context.residual_sign = residual_sign;

    const double initial_x = 0.5 * (lower_x + upper_x);
    double root_x = 0.0;
    double residual = 0.0;
    double residual_speed = 0.0;
    double last_evaluated_x = 0.0;
    double next_x = 0.0;
    int iteration_count = 0;
    int evaluation_count = 0;
    const bool solved = solve_bracketed_newton_bisection(
        eval_signed_angle_crossing_root,
        &root_context,
        lower_x,
        signed_lower,
        upper_x,
        signed_upper,
        initial_x,
        LONGITUDE_TOLERANCE_RAD,
        MIN_LONGITUDE_RATE_RAD_PER_DAY,
        MAX_NEWTON_STEP_DAYS,
        MAX_LONGITUDE_ITERATIONS,
        &root_x,
        &residual,
        &residual_speed,
        &last_evaluated_x,
        &next_x,
        &iteration_count,
        &evaluation_count);

    if (!solved
        && !(root_context.last_status == TAIYIN_STATUS_OK
            && angle_root_reached_numeric_floor(residual, last_evaluated_x, next_x))) {
        if (root_context.last_status != TAIYIN_STATUS_OK) {
            return normalize_event_status(root_context.last_status);
        }
        return iteration_count > 0 ? TAIYIN_EVENT_ERROR_NOT_FOUND : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    *out_x = last_evaluated_x;
    return TAIYIN_STATUS_OK;
}

bool eval_station_root(double x, void* user_data, BracketedRootEval* out) noexcept {
    StationRootContext* data = static_cast<StationRootContext*>(user_data);
    if (!data || !data->eval || !out) {
        return false;
    }

    Status status = data->eval(x, data->user_data, &data->sample);
    data->last_status = status;
    if (status != TAIYIN_STATUS_OK
        || !std::isfinite(data->sample.speed_rad_per_day)) {
        return false;
    }

    double before_x = x - STATION_ACCELERATION_STEP_DAYS;
    double after_x = x + STATION_ACCELERATION_STEP_DAYS;
    if (before_x < data->lower_x) {
        before_x = x;
    }
    if (after_x > data->upper_x) {
        after_x = x;
    }
    if (!(after_x > before_x)) {
        return false;
    }

    AngleCrossingSample before_sample;
    AngleCrossingSample after_sample;
    status = data->eval(before_x, data->user_data, &before_sample);
    data->last_status = status;
    if (status != TAIYIN_STATUS_OK
        || !std::isfinite(before_sample.speed_rad_per_day)) {
        return false;
    }
    status = data->eval(after_x, data->user_data, &after_sample);
    data->last_status = status;
    if (status != TAIYIN_STATUS_OK
        || !std::isfinite(after_sample.speed_rad_per_day)) {
        return false;
    }

    out->value = data->sample.speed_rad_per_day;
    out->derivative = (after_sample.speed_rad_per_day - before_sample.speed_rad_per_day)
        / (after_x - before_x);
    return std::isfinite(out->derivative);
}

Status refine_relative_station(
    AngleCrossingEvalFn eval,
    void* user_data,
    double lower_x,
    double lower_speed,
    double upper_x,
    double upper_speed,
    double* out_x
) noexcept {
    if (!eval
        || !out_x
        || !(lower_x < upper_x)
        || !std::isfinite(lower_speed)
        || !std::isfinite(upper_speed)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_x = 0.0;

    if (std::fabs(lower_speed) <= STATION_SPEED_TOLERANCE_RAD_PER_DAY) {
        *out_x = lower_x;
        return TAIYIN_STATUS_OK;
    }
    if (std::fabs(upper_speed) <= STATION_SPEED_TOLERANCE_RAD_PER_DAY) {
        *out_x = upper_x;
        return TAIYIN_STATUS_OK;
    }
    if (lower_speed * upper_speed > 0.0) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    double signed_lower = lower_speed;
    double signed_upper = upper_speed;
    double sign = 1.0;
    if (signed_lower > signed_upper) {
        signed_lower = -signed_lower;
        signed_upper = -signed_upper;
        sign = -1.0;
    }
    if (!(signed_lower <= 0.0 && signed_upper >= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    StationRootContext root_context;
    root_context.eval = eval;
    root_context.user_data = user_data;
    root_context.lower_x = lower_x;
    root_context.upper_x = upper_x;

    struct SignedStationContext {
        StationRootContext* inner;
        double sign;
    } signed_context = { &root_context, sign };

    const auto signed_eval = [](double x, void* raw, BracketedRootEval* out) noexcept -> bool {
        SignedStationContext* context = static_cast<SignedStationContext*>(raw);
        if (!context || !context->inner || !out) {
            return false;
        }
        if (!eval_station_root(x, context->inner, out)) {
            return false;
        }
        out->value *= context->sign;
        out->derivative *= context->sign;
        return true;
    };

    double root_x = 0.0;
    double value = 0.0;
    double derivative = 0.0;
    double last_evaluated_x = 0.0;
    double next_x = 0.0;
    int iteration_count = 0;
    int evaluation_count = 0;
    const bool solved = solve_bracketed_newton_bisection(
        signed_eval,
        &signed_context,
        lower_x,
        signed_lower,
        upper_x,
        signed_upper,
        0.5 * (lower_x + upper_x),
        STATION_SPEED_TOLERANCE_RAD_PER_DAY,
        MIN_STATION_ACCELERATION_RAD_PER_DAY2,
        MAX_NEWTON_STEP_DAYS,
        MAX_LONGITUDE_ITERATIONS,
        &root_x,
        &value,
        &derivative,
        &last_evaluated_x,
        &next_x,
        &iteration_count,
        &evaluation_count);

    if (!solved) {
        if (root_context.last_status != TAIYIN_STATUS_OK) {
            return normalize_event_status(root_context.last_status);
        }
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    *out_x = last_evaluated_x;
    return TAIYIN_STATUS_OK;
}

Status eval_body_longitude_sample(double x, void* user_data, AngleCrossingSample* out) noexcept {
    BodyLongitudeEvalContext* data = static_cast<BodyLongitudeEvalContext*>(user_data);
    if (!data || !out || !std::isfinite(x)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const SplitJulianDate jd = data->estimate_jd + data->time_direction * x;
    double position[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    Status status = TAIYIN_STATUS_OK;
    if (data->use_ut) {
        status = calc_position_ut(
            data->context,
            data->body_id,
            jd,
            data->flags,
            position,
            data->diagnostic);
    } else {
        const double tdb_minus_tt_seconds = dispatch::eval_tdb(
            data->context->model_context.tdb_model_id,
            jd,
            0);
        SplitJulianDate jd_tdb = jd;
        if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt_seconds, &jd_tdb)) {
            set_basic_diagnostic(data->diagnostic, TAIYIN_ERROR_UNSUPPORTED, data->body_id, jd);
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        status = calc_position_tdb(
            data->context,
            data->body_id,
            jd_tdb,
            jd,
            data->flags,
            position,
            data->diagnostic);
    }
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (!finite_position_speed(position)) {
        set_basic_diagnostic(data->diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, data->body_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double longitude = normalize_radians(position[0]);
    out->value_rad = data->time_direction > 0.0
        ? longitude
        : normalize_radians(-longitude);
    out->speed_rad_per_day = position[3];
    return TAIYIN_STATUS_OK;
}

Status calc_event_position(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd,
    bool use_ut,
    uint32_t flags,
    double position[6],
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (use_ut) {
        return calc_position_ut(
            context,
            body_id,
            jd,
            flags,
            position,
            diagnostic);
    }

    const double tdb_minus_tt_seconds = dispatch::eval_tdb(
        context->model_context.tdb_model_id,
        jd,
        0);
    SplitJulianDate jd_tdb = jd;
    if (!add_seconds_to_split_jd(jd_tdb, tdb_minus_tt_seconds, &jd_tdb)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    return calc_position_tdb(
        context,
        body_id,
        jd_tdb,
        jd,
        flags,
        position,
        diagnostic);
}

Status eval_body_aspect_sample(double x, void* user_data, AngleCrossingSample* out) noexcept {
    BodyAspectEvalContext* data = static_cast<BodyAspectEvalContext*>(user_data);
    if (!data || !out || !std::isfinite(x)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const SplitJulianDate jd = data->estimate_jd + x;
    double position_a[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    double position_b[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    Status status = calc_event_position(
        data->context,
        data->body_a_id,
        jd,
        data->use_ut,
        data->flags,
        position_a,
        data->diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    status = calc_event_position(
        data->context,
        data->body_b_id,
        jd,
        data->use_ut,
        data->flags,
        position_b,
        data->diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (!finite_position_speed(position_a) || !finite_position_speed(position_b)) {
        set_basic_diagnostic(data->diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, data->body_a_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    out->value_rad = normalize_radians(position_a[0] - position_b[0]);
    out->speed_rad_per_day = position_a[3] - position_b[3];
    return TAIYIN_STATUS_OK;
}

bool compute_elongation_from_states(
    const CartesianState& body_state,
    const CartesianState& sun_state,
    ElongationEvalSample* out
) noexcept {
    if (!out) {
        return false;
    }

    AngularSeparationKinematics kinematics;
    if (!cartesian_state_angular_separation_kinematics(body_state, sun_state, &kinematics)) {
        return false;
    }

    out->elongation_rad = kinematics.separation_rad;
    out->elongation_rate_rad_per_day = kinematics.separation_rate_rad_per_day;
    out->elongation_acceleration_rad_per_day2 = kinematics.separation_acceleration_rad_per_day2;
    out->relative_longitude_rad = normalize_signed_radians(
        std::atan2(body_state.position_au.y, body_state.position_au.x)
        - std::atan2(sun_state.position_au.y, sun_state.position_au.x));
    return std::isfinite(out->relative_longitude_rad);
}

bool compute_angular_separation_from_states(
    const CartesianState& state_a,
    const CartesianState& state_b,
    AngularSeparationEvalSample* out
) noexcept {
    if (!out) {
        return false;
    }

    AngularSeparationKinematics kinematics;
    if (!cartesian_state_angular_separation_kinematics(state_a, state_b, &kinematics)) {
        return false;
    }

    out->separation_rad = kinematics.separation_rad;
    out->separation_rate_rad_per_day = kinematics.separation_rate_rad_per_day;
    out->separation_acceleration_rad_per_day2 = kinematics.separation_acceleration_rad_per_day2;
    return true;
}

Status eval_elongation_sample(
    ElongationEvalContext* data,
    SplitJulianDate jd_ut,
    ElongationEvalSample* out
) noexcept {
    if (!data || !data->context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    NativeCalcContext frame_context = *data->context;
    // Inner-planet elongation is an observer geometry quantity:
    // angle(observer->body, observer->Sun). Use a Sun-centered evaluation
    // frame so Sun-centered routes such as the built-in semi-analytical model
    // can provide both target and observer states. calc_state_ut still returns
    // apparent vectors as seen from frame_context.observer_id.
    frame_context.center_id = TAIYIN_BODY_SUN;

    CartesianState body_state;
    CartesianState sun_state;
    EphemerisEvalDiagnostic body_diagnostic;
    EphemerisEvalDiagnostic sun_diagnostic;
    Status status = calc_state_ut(
        &frame_context,
        data->body_id,
        jd_ut,
        data->flags,
        &body_state,
        &body_diagnostic);
    ++data->evaluation_count;
    if (status != TAIYIN_STATUS_OK) {
        if (data->diagnostic) {
            *data->diagnostic = body_diagnostic;
        }
        return status;
    }
    status = calc_state_ut(
        &frame_context,
        TAIYIN_BODY_SUN,
        jd_ut,
        data->flags,
        &sun_state,
        &sun_diagnostic);
    ++data->evaluation_count;
    if (status != TAIYIN_STATUS_OK) {
        if (data->diagnostic) {
            *data->diagnostic = sun_diagnostic;
        }
        return status;
    }
    if (!compute_elongation_from_states(body_state, sun_state, out)) {
        set_basic_diagnostic(data->diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, data->body_id, jd_ut);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (data->diagnostic) {
        *data->diagnostic = body_diagnostic;
    }
    return TAIYIN_STATUS_OK;
}

Status eval_angular_separation_sample(
    AngularSeparationEvalContext* data,
    SplitJulianDate jd,
    AngularSeparationEvalSample* out
) noexcept {
    if (!data || !data->context || !out || !split_julian_date_is_finite(jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const NativeCalcContext* sample_context = data->context;
    NativeCalcContext refreshed_topocentric_context;
    if ((data->flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u
        && data->context->fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)) {
        SplitJulianDate jd_ut;
        SplitJulianDate jd_tt;
        if (data->use_ut) {
            jd_ut = jd;
            const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
                data->context->delta_t_model_id,
                data->context->ephemeris_family_id,
                jd_ut,
                0,
                0);
            if (!ut1_to_tt_split_jd(jd_ut, delta_t, &jd_tt)) {
                return TAIYIN_ERROR_UNSUPPORTED;
            }
        } else {
            jd_tt = jd;
            double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
                data->context->delta_t_model_id,
                data->context->ephemeris_family_id,
                jd_tt,
                0,
                0);
            if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
                return TAIYIN_ERROR_UNSUPPORTED;
            }
            delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
                data->context->delta_t_model_id,
                data->context->ephemeris_family_id,
                jd_ut,
                0,
                0);
            if (!tt_to_ut1_split_jd(jd_tt, delta_t, &jd_ut)) {
                return TAIYIN_ERROR_UNSUPPORTED;
            }
        }
        refreshed_topocentric_context = *data->context;
        const Status topocentric_status =
            native_context_refresh_topocentric_observer(
                &refreshed_topocentric_context, jd_ut, jd_tt);
        if (topocentric_status != TAIYIN_STATUS_OK) {
            if (data->diagnostic) {
                set_basic_diagnostic(
                    data->diagnostic,
                    topocentric_status,
                    data->body_a_id,
                    jd);
            }
            return topocentric_status;
        }
        sample_context = &refreshed_topocentric_context;
    }

    CartesianState state_a;
    CartesianState state_b;
    EphemerisEvalDiagnostic diagnostic_a;
    EphemerisEvalDiagnostic diagnostic_b;
    Status status = data->use_ut
        ? calc_state_ut(sample_context, data->body_a_id, jd, data->flags, &state_a, &diagnostic_a)
        : calc_state_tt(sample_context, data->body_a_id, jd, data->flags, &state_a, &diagnostic_a);
    ++data->evaluation_count;
    if (status != TAIYIN_STATUS_OK) {
        if (data->diagnostic) {
            *data->diagnostic = diagnostic_a;
        }
        return status;
    }
    if (data->star_key) {
        double star_position[6] = {};
        const uint64_t star_flags = static_cast<uint64_t>(
            (data->flags & ~TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX)
            | TAIYIN_NATIVE_POSITION_XYZ
            | TAIYIN_NATIVE_POSITION_SPEED);
        status = data->use_ut
            ? calc_star_position_ut(
                sample_context, data->star_key, jd, star_flags,
                star_position, &diagnostic_b)
            : calc_star_position_tt(
                sample_context, data->star_key, jd, star_flags,
                star_position, &diagnostic_b);
        state_b.position_au = Vector3{
            star_position[0], star_position[1], star_position[2]};
        state_b.velocity_au_per_day = Vector3{
            star_position[3], star_position[4], star_position[5]};
        // The public star-position API exposes apparent position and velocity.
        // A zero acceleration is sufficient for the guarded Newton proposal:
        // the exact first derivative brackets the root, and bisection remains
        // authoritative if the approximate curvature proposes a bad step.
        state_b.acceleration_au_per_day2 = Vector3{0.0, 0.0, 0.0};
    } else {
        status = data->use_ut
            ? calc_state_ut(sample_context, data->body_b_id, jd, data->flags, &state_b, &diagnostic_b)
            : calc_state_tt(sample_context, data->body_b_id, jd, data->flags, &state_b, &diagnostic_b);
    }
    ++data->evaluation_count;
    if (status != TAIYIN_STATUS_OK) {
        if (data->diagnostic) {
            *data->diagnostic = diagnostic_b;
        }
        return status;
    }
    if (!compute_angular_separation_from_states(state_a, state_b, out)) {
        set_basic_diagnostic(data->diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, data->body_a_id, jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (data->diagnostic) {
        *data->diagnostic = diagnostic_a;
    }
    return TAIYIN_STATUS_OK;
}

Status search_direct_body_longitude_impl(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate estimate_jd,
    bool use_ut,
    uint64_t flags,
    SplitJulianDate* out_jd,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out_jd) {
        *out_jd = SplitJulianDate(0, NAN);
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context || !out_jd || body_id == 0
        || !std::isfinite(target_longitude_rad)
        || !split_julian_date_is_finite(estimate_jd)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, estimate_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, estimate_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const uint64_t known_search_flags = TAIYIN_EVENT_SEARCH_REVERSE;
    if ((search_flags & ~known_search_flags) != 0u) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, estimate_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const bool reverse = (search_flags & TAIYIN_EVENT_SEARCH_REVERSE) != 0u;
    const double time_direction = reverse ? -1.0 : 1.0;
    const double directed_target = reverse
        ? normalize_radians(-target_longitude_rad)
        : normalize_radians(target_longitude_rad);

    BodyLongitudeEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_id = body_id;
    eval_context.flags = position_flags
        | TAIYIN_NATIVE_POSITION_SPEED
        | TAIYIN_NATIVE_POSITION_RADIANS;
    eval_context.use_ut = use_ut;
    eval_context.estimate_jd = estimate_jd;
    eval_context.time_direction = time_direction;
    eval_context.diagnostic = diagnostic;

    double x = 0.0;
    const Status status = solve_forward_angle_crossing_newton_bisect(
        eval_body_longitude_sample,
        &eval_context,
        directed_target,
        MAX_LONGITUDE_SEARCH_DAYS,
        &x);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic && diagnostic->status == TAIYIN_STATUS_OK) {
            diagnostic->status = status;
            diagnostic->target_id = body_id;
            diagnostic->jd_tdb = estimate_jd + time_direction * x;
        }
        return status;
    }

    *out_jd = estimate_jd + time_direction * x;
    return TAIYIN_STATUS_OK;
}

Status search_bounded_angle_crossings(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    double target_longitude_rad,
    SplitJulianDate start_jd,
    double span_days,
    int step_count,
    SplitJulianDate* out_jd,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!eval || !user_data || !out_event_count || (!out_jd && max_event_count > 0)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double target = normalize_radians(target_longitude_rad);
    const double actual_step_days = span_days / static_cast<double>(step_count);

    AngleCrossingSample previous_sample;
    Status status = eval(0.0, user_data, &previous_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(previous_sample.value_rad)
        || !std::isfinite(previous_sample.speed_rad_per_day)) {
        set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    double previous_x = 0.0;
    double previous_residual = angular_difference_radians(previous_sample.value_rad, target);
    if (std::fabs(previous_residual) <= LONGITUDE_TOLERANCE_RAD) {
        if (!append_event_time(start_jd, out_jd, max_event_count, out_event_count)) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        }
    }

    for (int i = 1; i <= step_count; ++i) {
        const double x = i == step_count
            ? span_days
            : actual_step_days * static_cast<double>(i);
        AngleCrossingSample sample;
        status = eval(x, user_data, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!std::isfinite(sample.value_rad)
            || !std::isfinite(sample.speed_rad_per_day)) {
            set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd + x);
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }

        const double residual = angular_difference_radians(sample.value_rad, target);
        if (std::fabs(residual) <= LONGITUDE_TOLERANCE_RAD) {
            if (!append_event_time(start_jd + x, out_jd, max_event_count, out_event_count)) {
                return TAIYIN_ERROR_OUT_OF_MEMORY;
            }
        } else if (previous_residual * residual < 0.0
            && std::fabs(previous_residual - residual) <= TAIYIN_PI) {
            double root_x = 0.0;
            status = refine_bounded_angle_crossing(
                eval,
                user_data,
                target,
                previous_x,
                previous_residual,
                x,
                residual,
                &root_x);
            if (status != TAIYIN_STATUS_OK) {
                return status;
            }
            if (!append_event_time(start_jd + root_x, out_jd, max_event_count, out_event_count)) {
                return TAIYIN_ERROR_OUT_OF_MEMORY;
            }
        }

        previous_x = x;
        previous_residual = residual;
    }

    return *out_event_count > 0 ? TAIYIN_STATUS_OK : TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status validate_bounded_search(
    const NativeCalcContext* context,
    int diagnostic_target_id,
    double target_rad,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic,
    uint32_t* out_position_flags,
    int* out_step_count
) noexcept {
    if (out_event_count) {
        *out_event_count = 0;
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out_event_count
        || !out_position_flags
        || !out_step_count
        || (!out_jd && max_event_count > 0)
        || !std::isfinite(target_rad)
        || !split_julian_date_is_finite(start_jd)
        || !split_julian_date_is_finite(end_jd)
        || !std::isfinite(max_step_days)
        || !(end_jd > start_jd)
        || !(max_step_days > 0.0)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u
        || search_flags != 0u) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const double span_days = end_jd - start_jd;
    const double raw_step_count = std::ceil(span_days / max_step_days);
    if (!std::isfinite(raw_step_count)
        || raw_step_count <= 0.0
        || raw_step_count > static_cast<double>(MAX_BOUNDED_SEARCH_STEPS)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_position_flags = position_flags;
    *out_step_count = static_cast<int>(raw_step_count);
    return TAIYIN_STATUS_OK;
}

Status search_body_longitude_crossings_impl(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    bool use_ut,
    uint64_t flags,
    SplitJulianDate* out_jd,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    uint32_t position_flags = 0;
    int step_count = 0;
    Status status = validate_bounded_search(
        context,
        body_id,
        target_longitude_rad,
        start_jd,
        end_jd,
        max_step_days,
        flags,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic,
        &position_flags,
        &step_count);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (body_id == 0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    BodyLongitudeEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_id = body_id;
    eval_context.flags = position_flags
        | TAIYIN_NATIVE_POSITION_SPEED
        | TAIYIN_NATIVE_POSITION_RADIANS;
    eval_context.use_ut = use_ut;
    eval_context.estimate_jd = start_jd;
    eval_context.time_direction = 1.0;
    eval_context.diagnostic = diagnostic;

    return search_bounded_angle_crossings(
        eval_body_longitude_sample,
        &eval_context,
        body_id,
        target_longitude_rad,
        start_jd,
        end_jd - start_jd,
        step_count,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_aspect_crossings_impl(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    bool use_ut,
    uint64_t flags,
    SplitJulianDate* out_jd,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    uint32_t position_flags = 0;
    int step_count = 0;
    Status status = validate_bounded_search(
        context,
        body_a_id,
        aspect_rad,
        start_jd,
        end_jd,
        max_step_days,
        flags,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic,
        &position_flags,
        &step_count);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (body_a_id == 0 || body_b_id == 0 || body_a_id == body_b_id) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_a_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    BodyAspectEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_a_id = body_a_id;
    eval_context.body_b_id = body_b_id;
    eval_context.flags = position_flags
        | TAIYIN_NATIVE_POSITION_SPEED
        | TAIYIN_NATIVE_POSITION_RADIANS;
    eval_context.use_ut = use_ut;
    eval_context.estimate_jd = start_jd;
    eval_context.diagnostic = diagnostic;

    return search_bounded_angle_crossings(
        eval_body_aspect_sample,
        &eval_context,
        body_a_id,
        aspect_rad,
        start_jd,
        end_jd - start_jd,
        step_count,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic);
}

bool station_speed_is_bracketed(
    double previous_speed,
    double speed
) noexcept {
    return std::fabs(previous_speed) <= MIN_LONGITUDE_RATE_RAD_PER_DAY
        || std::fabs(speed) <= MIN_LONGITUDE_RATE_RAD_PER_DAY
        || previous_speed * speed < 0.0;
}

Status append_longitude_station_hits(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    SplitJulianDate start_jd,
    double span_days,
    int step_count,
    SplitJulianDate* out_jd,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!eval || !user_data || (!out_jd && max_event_count > 0) || !out_event_count || step_count <= 0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double actual_step_days = span_days / static_cast<double>(step_count);

    AngleCrossingSample previous_sample;
    Status status = eval(0.0, user_data, &previous_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(previous_sample.value_rad)
        || !std::isfinite(previous_sample.speed_rad_per_day)) {
        set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    double previous_speed = previous_sample.speed_rad_per_day;
    double previous_x = 0.0;
    for (int i = 1; i <= step_count; ++i) {
        const double x = i == step_count
            ? span_days
            : actual_step_days * static_cast<double>(i);
        AngleCrossingSample sample;
        status = eval(x, user_data, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!std::isfinite(sample.value_rad)
            || !std::isfinite(sample.speed_rad_per_day)) {
            set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd + x);
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }

        const double speed = sample.speed_rad_per_day;
        if (station_speed_is_bracketed(previous_speed, speed)) {
            double station_x = 0.0;
            status = refine_relative_station(
                eval,
                user_data,
                previous_x,
                previous_speed,
                x,
                speed,
                &station_x);
            if (status == TAIYIN_STATUS_OK) {
                AngleCrossingSample station_sample;
                status = eval(station_x, user_data, &station_sample);
                if (status != TAIYIN_STATUS_OK) {
                    return normalize_event_status(status);
                }
                if (!std::isfinite(station_sample.value_rad)) {
                    set_basic_diagnostic(
                        diagnostic,
                        TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
                        diagnostic_target_id,
                        start_jd + station_x);
                    return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
                }
                if (!append_station_event(
                        start_jd + station_x,
                        station_sample.value_rad,
                        out_jd,
                        out_longitude_rad,
                        max_event_count,
                        out_event_count)) {
                    return TAIYIN_ERROR_OUT_OF_MEMORY;
                }
            } else if (status != TAIYIN_EVENT_ERROR_NOT_FOUND) {
                return status;
            }
        }

        previous_x = x;
        previous_speed = speed;
    }

    return TAIYIN_STATUS_OK;
}

Status search_body_longitude_stations_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    bool use_ut,
    uint64_t flags,
    SplitJulianDate* out_jd,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    uint32_t position_flags = 0;
    int step_count = 0;
    Status status = validate_bounded_search(
        context,
        body_id,
        0.0,
        start_jd,
        end_jd,
        max_step_days,
        flags,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic,
        &position_flags,
        &step_count);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (body_id == 0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    BodyLongitudeEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_id = body_id;
    eval_context.flags = position_flags
        | TAIYIN_NATIVE_POSITION_SPEED
        | TAIYIN_NATIVE_POSITION_RADIANS;
    eval_context.use_ut = use_ut;
    eval_context.estimate_jd = start_jd;
    eval_context.time_direction = 1.0;
    eval_context.diagnostic = diagnostic;

    status = append_longitude_station_hits(
        eval_body_longitude_sample,
        &eval_context,
        body_id,
        start_jd,
        end_jd - start_jd,
        step_count,
        out_jd,
        out_longitude_rad,
        max_event_count,
        out_event_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    return *out_event_count > 0 ? TAIYIN_STATUS_OK : TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status append_station_hit_if_exact(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    double target_aspect_rad,
    SplitJulianDate start_jd,
    double station_x,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    AngleCrossingSample station_sample;
    Status status = eval(station_x, user_data, &station_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(station_sample.value_rad)) {
        set_basic_diagnostic(
            diagnostic,
            TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED,
            diagnostic_target_id,
            start_jd + station_x);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    const double target = normalize_radians(target_aspect_rad);
    if (std::fabs(angular_difference_radians(station_sample.value_rad, target))
        > STATION_ASPECT_TOLERANCE_RAD) {
        return TAIYIN_STATUS_OK;
    }
    if (!append_exact_aspect_event(
            start_jd + station_x,
            target,
            out_jd,
            out_target_aspect_rad,
            max_event_count,
            out_event_count)) {
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    return TAIYIN_STATUS_OK;
}

Status append_station_hits_for_target(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    double target_aspect_rad,
    SplitJulianDate start_jd,
    double span_days,
    int step_count,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!eval || !user_data || !out_jd || !out_event_count || step_count <= 0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, diagnostic_target_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double target = normalize_radians(target_aspect_rad);
    const double actual_step_days = span_days / static_cast<double>(step_count);

    AngleCrossingSample previous_sample;
    Status status = eval(0.0, user_data, &previous_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(previous_sample.value_rad)
        || !std::isfinite(previous_sample.speed_rad_per_day)) {
        set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd);
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    double previous_speed = previous_sample.speed_rad_per_day;
    double previous_x = 0.0;
    for (int i = 1; i <= step_count; ++i) {
        const double x = i == step_count
            ? span_days
            : actual_step_days * static_cast<double>(i);
        AngleCrossingSample sample;
        status = eval(x, user_data, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!std::isfinite(sample.value_rad)
            || !std::isfinite(sample.speed_rad_per_day)) {
            set_basic_diagnostic(diagnostic, TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED, diagnostic_target_id, start_jd + x);
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }

        const double speed = sample.speed_rad_per_day;
        if (station_speed_is_bracketed(previous_speed, speed)) {
            double station_x = 0.0;
            status = refine_relative_station(
                eval,
                user_data,
                previous_x,
                previous_speed,
                x,
                speed,
                &station_x);
            if (status == TAIYIN_STATUS_OK) {
                status = append_station_hit_if_exact(
                    eval,
                    user_data,
                    diagnostic_target_id,
                    target,
                    start_jd,
                    station_x,
                    out_jd,
                    out_target_aspect_rad,
                    max_event_count,
                    out_event_count,
                    diagnostic);
                if (status != TAIYIN_STATUS_OK) {
                    return status;
                }
            } else if (status != TAIYIN_EVENT_ERROR_NOT_FOUND) {
                return status;
            }
        }

        previous_x = x;
        previous_speed = speed;
    }

    return TAIYIN_STATUS_OK;
}

Status append_crossing_hits_for_target(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    double target_aspect_rad,
    SplitJulianDate start_jd,
    double span_days,
    int step_count,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    const size_t start_count = *out_event_count;
    size_t crossing_count = 0;
    const Status status = search_bounded_angle_crossings(
        eval,
        user_data,
        diagnostic_target_id,
        target_aspect_rad,
        start_jd,
        span_days,
        step_count,
        out_jd + start_count,
        max_event_count - start_count,
        &crossing_count,
        diagnostic);
    if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) {
        return TAIYIN_STATUS_OK;
    }
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }

    size_t merged_count = start_count;
    const double target = normalize_radians(target_aspect_rad);
    for (size_t i = 0; i < crossing_count; ++i) {
        if (!append_exact_aspect_event(
                out_jd[start_count + i],
                target,
                out_jd,
                out_target_aspect_rad,
                max_event_count,
                &merged_count)) {
            return TAIYIN_ERROR_OUT_OF_MEMORY;
        }
    }
    *out_event_count = merged_count;
    return TAIYIN_STATUS_OK;
}

Status append_exact_angle_hits_for_target(
    AngleCrossingEvalFn eval,
    void* user_data,
    int diagnostic_target_id,
    double target_aspect_rad,
    SplitJulianDate start_jd,
    double span_days,
    int step_count,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    Status status = append_crossing_hits_for_target(
        eval,
        user_data,
        diagnostic_target_id,
        target_aspect_rad,
        start_jd,
        span_days,
        step_count,
        out_jd,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }

    return append_station_hits_for_target(
        eval,
        user_data,
        diagnostic_target_id,
        target_aspect_rad,
        start_jd,
        span_days,
        step_count,
        out_jd,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_exact_aspects_impl(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    bool use_ut,
    uint64_t flags,
    SplitJulianDate* out_jd,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    uint32_t position_flags = 0;
    int step_count = 0;
    Status status = validate_bounded_search(
        context,
        body_a_id,
        0.0,
        start_jd,
        end_jd,
        max_step_days,
        flags,
        out_jd,
        max_event_count,
        out_event_count,
        diagnostic,
        &position_flags,
        &step_count);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    if (body_a_id == 0
        || body_b_id == 0
        || body_a_id == body_b_id
        || !aspect_separations_rad
        || aspect_count == 0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_a_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    BodyAspectEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_a_id = body_a_id;
    eval_context.body_b_id = body_b_id;
    eval_context.flags = position_flags
        | TAIYIN_NATIVE_POSITION_SPEED
        | TAIYIN_NATIVE_POSITION_RADIANS;
    eval_context.use_ut = use_ut;
    eval_context.estimate_jd = start_jd;
    eval_context.diagnostic = diagnostic;

    const double span_days = end_jd - start_jd;
    std::vector<double> targets;
    status = build_unique_aspect_targets(aspect_separations_rad, aspect_count, &targets);
    if (status != TAIYIN_STATUS_OK) {
        set_basic_diagnostic(diagnostic, status, body_a_id, start_jd);
        return status;
    }

    for (size_t i = 0; i < targets.size(); ++i) {
        status = append_exact_angle_hits_for_target(
            eval_body_aspect_sample,
            &eval_context,
            body_a_id,
            targets[i],
            start_jd,
            span_days,
            step_count,
            out_jd,
            out_target_aspect_rad,
            max_event_count,
            out_event_count,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return status;
        }
    }

    if (*out_event_count == 0) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }
    sort_exact_aspect_events(out_jd, out_target_aspect_rad, *out_event_count);
    compact_exact_aspect_events(out_jd, out_target_aspect_rad, out_event_count);
    return TAIYIN_STATUS_OK;
}

bool is_inner_planet_elongation_body(int body_id) noexcept {
    return body_id == TAIYIN_BODY_MERCURY
        || body_id == TAIYIN_BODY_MERCURY_BARYCENTER
        || body_id == TAIYIN_BODY_VENUS
        || body_id == TAIYIN_BODY_VENUS_BARYCENTER;
}

int physical_elongation_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return TAIYIN_BODY_MERCURY;
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return TAIYIN_BODY_VENUS;
    default:
        return body_id;
    }
}

int ephemeris_elongation_body(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
        return TAIYIN_BODY_MERCURY_BARYCENTER;
    case TAIYIN_BODY_VENUS:
        return TAIYIN_BODY_VENUS_BARYCENTER;
    default:
        return body_id;
    }
}

double recommended_greatest_elongation_step_days(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return 2.0;
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return 5.0;
    default:
        return 2.0;
    }
}

double recommended_solar_transit_conjunction_step_days(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return 5.0;
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return 20.0;
    default:
        return 5.0;
    }
}

// Conservative half-window around an inferior-conjunction candidate.  It is
// used both to recover candidates clipped by the caller interval and to keep
// greatest-transit/contact solving away from artificial search boundaries.
double solar_transit_candidate_half_window_days(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
        return 3.0;
    default:
        return 1.5;
    }
}

bool solar_transit_synodic_seed(
    int body_id,
    SplitJulianDate* out_base_inferior_conjunction_jd_ut,
    double* out_synodic_period_days
) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        if (out_base_inferior_conjunction_jd_ut) {
            *out_base_inferior_conjunction_jd_ut = SplitJulianDate(2458799, 0.138751322404);
        }
        if (out_synodic_period_days) {
            *out_synodic_period_days = 115.8774777586;
        }
        return true;
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
        if (out_base_inferior_conjunction_jd_ut) {
            *out_base_inferior_conjunction_jd_ut = SplitJulianDate(2453164, 0.847039);
        }
        if (out_synodic_period_days) {
            *out_synodic_period_days = 583.921361;
        }
        return true;
    default:
        return false;
    }
}

bool is_inner_planet_transit_body(int body_id) noexcept {
    return is_inner_planet_elongation_body(body_id);
}

bool native_context_has_topocentric_observer(const NativeCalcContext& context) noexcept {
    return (context.apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u
        || context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET);
}

NativeCalcContext solar_transit_candidate_context(
    const NativeCalcContext& context,
    uint32_t position_flags
) noexcept {
    NativeCalcContext candidate_context = context;
    candidate_context.apparent_options.output_frame_id =
        (position_flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u
            ? TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE
            : TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return candidate_context;
}

bool valid_geodetic_degrees(double longitude_deg, double latitude_deg, double height_m) noexcept {
    return std::isfinite(longitude_deg)
        && std::isfinite(latitude_deg)
        && std::isfinite(height_m)
        && latitude_deg >= -90.0
        && latitude_deg <= 90.0;
}

NativeCalcContext local_transit_context_from_observer(
    const NativeCalcContext& context,
    const NativeObserverLocation& location
) noexcept {
    NativeCalcContext local = context;
    native_context_set_geocentric_observer(
        &local,
        TAIYIN_BODY_EARTH,
        TAIYIN_BODY_EARTH);
    native_context_set_observer_location(&local, location);
    return local;
}

Status local_transit_context_at_ut(
    const NativeCalcContext* context,
    const NativeObserverLocation& location,
    SplitJulianDate jd_ut,
    NativeCalcContext* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const double delta_t = dispatch::eval_delta_t_with_ephemeris_correction(
        context->delta_t_model_id,
        context->ephemeris_family_id,
        jd_ut,
        0,
        0);
    SplitJulianDate jd_tt;
    if (!ut1_to_tt_split_jd(jd_ut, delta_t, &jd_tt)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, TAIYIN_BODY_EARTH, jd_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    *out = local_transit_context_from_observer(*context, location);
    const Status status = native_context_set_simple_topocentric_observer(
        out,
        location,
        jd_ut,
        jd_tt);
    if (status != TAIYIN_STATUS_OK) {
        set_basic_diagnostic(diagnostic, status, TAIYIN_BODY_EARTH, jd_ut);
    }
    return status;
}

bool resolve_local_event_refraction_flags(
    uint64_t search_flags,
    uint64_t* out_observed_flags
) noexcept {
    if (!out_observed_flags) return false;
    if ((search_flags & TAIYIN_EVENT_SEARCH_REFRACTION) != 0u
        && (search_flags & TAIYIN_EVENT_SEARCH_NO_REFRACTION) != 0u) {
        return false;
    }
    *out_observed_flags = (search_flags & TAIYIN_EVENT_SEARCH_NO_REFRACTION) == 0u
        ? TAIYIN_OBSERVED_REFRACTION
        : 0u;
    return true;
}

Status eval_solar_transit_radii(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    double* out_sun_radius_rad,
    double* out_body_radius_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context
        || !out_sun_radius_rad
        || !out_body_radius_rad
        || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_sun_radius_rad = NAN;
    *out_body_radius_rad = NAN;

    BodyPhenomena sun;
    BodyPhenomena body;
    EphemerisEvalDiagnostic sun_diagnostic;
    EphemerisEvalDiagnostic body_diagnostic;
    Status status = calc_body_phenomena_ut(
        context,
        TAIYIN_BODY_SUN,
        jd_ut,
        flags,
        &sun,
        &sun_diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = sun_diagnostic;
        return status;
    }
    status = calc_body_phenomena_ut(
        context,
        physical_elongation_body(body_id),
        jd_ut,
        flags,
        &body,
        &body_diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = body_diagnostic;
        return status;
    }
    if (!std::isfinite(sun.apparent_diameter_rad)
        || !std::isfinite(body.apparent_diameter_rad)
        || !(sun.apparent_diameter_rad > 0.0)
        || !(body.apparent_diameter_rad > 0.0)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }

    *out_sun_radius_rad = 0.5 * sun.apparent_diameter_rad;
    *out_body_radius_rad = 0.5 * body.apparent_diameter_rad;
    if (diagnostic) *diagnostic = body_diagnostic;
    return TAIYIN_STATUS_OK;
}

Status eval_solar_transit_separation(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint32_t flags,
    double* out_separation_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_separation_rad || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_separation_rad = NAN;
    AngularSeparationEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_a_id = ephemeris_elongation_body(body_id);
    eval_context.body_b_id = TAIYIN_BODY_SUN;
    eval_context.flags = flags;
    eval_context.use_ut = true;
    eval_context.diagnostic = diagnostic;
    AngularSeparationEvalSample sample;
    const Status status = eval_angular_separation_sample(&eval_context, jd_ut, &sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    if (!std::isfinite(sample.separation_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_separation_rad = sample.separation_rad;
    return TAIYIN_STATUS_OK;
}

Status eval_solar_transit_contact_residual(
    SolarTransitContactContext* contact,
    SplitJulianDate jd_ut,
    double* out_residual
) noexcept {
    if (!contact || !out_residual || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_residual = NAN;
    double separation = NAN;
    Status status = eval_solar_transit_separation(
        contact->context,
        contact->body_id,
        jd_ut,
        contact->flags,
        &separation,
        contact->diagnostic);
    ++contact->evaluation_count;
    if (status != TAIYIN_STATUS_OK) return status;

    double sun_radius = NAN;
    double body_radius = NAN;
    status = eval_solar_transit_radii(
        contact->context,
        contact->body_id,
        jd_ut,
        contact->flags,
        &sun_radius,
        &body_radius,
        contact->diagnostic);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    const double target_radius = sun_radius + contact->body_radius_sign * body_radius;
    if (!(target_radius > 0.0) || !std::isfinite(target_radius)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_residual = separation - target_radius;
    return std::isfinite(*out_residual) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status solve_solar_transit_contact(
    SolarTransitContactContext* contact,
    SplitJulianDate greatest_jd_ut,
    double direction,
    double max_offset_days,
    SplitJulianDate* out_jd_ut,
    int* out_iteration_count
) noexcept {
    if (!contact
        || !out_jd_ut
        || !out_iteration_count
        || !split_julian_date_is_finite(greatest_jd_ut)
        || !(max_offset_days > 0.0)
        || !(direction == -1.0 || direction == 1.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();
    *out_iteration_count = 0;

    double inside_residual = NAN;
    Status status = eval_solar_transit_contact_residual(contact, greatest_jd_ut, &inside_residual);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!(inside_residual <= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    const double probe_step = 0.02;
    SplitJulianDate inside_jd = greatest_jd_ut;
    SplitJulianDate outside_jd = invalid_jd();
    double outside_residual = NAN;
    for (double offset = probe_step; offset <= max_offset_days + 1.0e-12; offset += probe_step) {
        const SplitJulianDate jd = greatest_jd_ut + direction * offset;
        status = eval_solar_transit_contact_residual(contact, jd, &outside_residual);
        if (status != TAIYIN_STATUS_OK) return status;
        if (outside_residual >= 0.0) {
            outside_jd = jd;
            break;
        }
        inside_jd = jd;
        inside_residual = outside_residual;
    }
    if (!split_julian_date_is_finite(outside_jd)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    SplitJulianDate lo = std::min(inside_jd, outside_jd);
    SplitJulianDate hi = std::max(inside_jd, outside_jd);
    for (int iter = 0; iter < MAX_SOLAR_TRANSIT_CONTACT_ITERATIONS; ++iter) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        double residual = NAN;
        status = eval_solar_transit_contact_residual(contact, mid, &residual);
        if (status != TAIYIN_STATUS_OK) return status;
        if ((hi - lo) <= SOLAR_TRANSIT_CONTACT_TIME_TOLERANCE_DAYS || std::fabs(residual) <= 1.0e-12) {
            *out_jd_ut = mid;
            *out_iteration_count = iter + 1;
            return TAIYIN_STATUS_OK;
        }
        const bool mid_inside = residual <= 0.0;
        if (direction < 0.0) {
            if (mid_inside) {
                hi = mid;
            } else {
                lo = mid;
            }
        } else {
            if (mid_inside) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
    }

    *out_jd_ut = lo + 0.5 * (hi - lo);
    *out_iteration_count = MAX_SOLAR_TRANSIT_CONTACT_ITERATIONS;
    return TAIYIN_STATUS_OK;
}

Status eval_local_solar_transit_separation(
    LocalSolarTransitEvalContext* data,
    SplitJulianDate jd_ut,
    double* out_separation_rad
) noexcept {
    if (!data || !data->context || !out_separation_rad || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext local;
    Status status = local_transit_context_at_ut(
        data->context,
        data->location,
        jd_ut,
        &local,
        data->diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    status = eval_solar_transit_separation(
        &local,
        data->body_id,
        jd_ut,
        data->flags | TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
        out_separation_rad,
        data->diagnostic);
    ++data->evaluation_count;
    return status;
}

Status eval_local_solar_transit_radii(
    LocalSolarTransitEvalContext* data,
    SplitJulianDate jd_ut,
    double* out_sun_radius_rad,
    double* out_body_radius_rad
) noexcept {
    if (!data || !data->context || !out_sun_radius_rad || !out_body_radius_rad || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    NativeCalcContext local;
    Status status = local_transit_context_at_ut(
        data->context,
        data->location,
        jd_ut,
        &local,
        data->diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    return eval_solar_transit_radii(
        &local,
        data->body_id,
        jd_ut,
        data->flags | TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
        out_sun_radius_rad,
        out_body_radius_rad,
        data->diagnostic);
}

Status eval_local_solar_transit_contact_residual(
    LocalSolarTransitEvalContext* data,
    double body_radius_sign,
    SplitJulianDate jd_ut,
    double* out_residual
) noexcept {
    if (!data || !out_residual || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_residual = NAN;
    double separation = NAN;
    Status status = eval_local_solar_transit_separation(data, jd_ut, &separation);
    if (status != TAIYIN_STATUS_OK) return status;

    double sun_radius = NAN;
    double body_radius = NAN;
    status = eval_local_solar_transit_radii(data, jd_ut, &sun_radius, &body_radius);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    const double target_radius = sun_radius + body_radius_sign * body_radius;
    if (!(target_radius > 0.0) || !std::isfinite(target_radius)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_residual = separation - target_radius;
    return std::isfinite(*out_residual) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status minimize_local_solar_transit_separation(
    LocalSolarTransitEvalContext* data,
    SplitJulianDate lower_jd_ut,
    SplitJulianDate upper_jd_ut,
    SplitJulianDate* out_jd_ut,
    double* out_separation_rad,
    int* out_iteration_count
) noexcept {
    if (!data
        || !out_jd_ut
        || !out_separation_rad
        || !out_iteration_count
        || !(lower_jd_ut < upper_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();
    *out_separation_rad = NAN;
    *out_iteration_count = 0;

    const double inv_phi = 0.6180339887498948482;
    double a = 0.0;
    double b = upper_jd_ut - lower_jd_ut;
    double c = b - inv_phi * (b - a);
    double d = a + inv_phi * (b - a);
    double c_value = NAN;
    double d_value = NAN;
    Status status = eval_local_solar_transit_separation(data, lower_jd_ut + c, &c_value);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    status = eval_local_solar_transit_separation(data, lower_jd_ut + d, &d_value);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    for (int iter = 0;
         iter < MAX_ANGULAR_SEPARATION_ITERATIONS && (b - a) > ANGULAR_SEPARATION_TIME_TOLERANCE_DAYS;
         ++iter) {
        if (c_value > d_value) {
            a = c;
            c = d;
            c_value = d_value;
            d = a + inv_phi * (b - a);
            status = eval_local_solar_transit_separation(data, lower_jd_ut + d, &d_value);
            if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        } else {
            b = d;
            d = c;
            d_value = c_value;
            c = b - inv_phi * (b - a);
            status = eval_local_solar_transit_separation(data, lower_jd_ut + c, &c_value);
            if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        }
        *out_iteration_count = iter + 1;
    }

    const SplitJulianDate jd = lower_jd_ut + 0.5 * (a + b);
    double value = NAN;
    status = eval_local_solar_transit_separation(data, jd, &value);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    *out_jd_ut = jd;
    *out_separation_rad = value;
    return std::isfinite(value) ? TAIYIN_STATUS_OK : TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
}

Status solve_local_solar_transit_contact(
    LocalSolarTransitEvalContext* data,
    double body_radius_sign,
    SplitJulianDate greatest_jd_ut,
    double direction,
    double max_offset_days,
    SplitJulianDate* out_jd_ut,
    int* out_iteration_count
) noexcept {
    if (!data
        || !out_jd_ut
        || !out_iteration_count
        || !split_julian_date_is_finite(greatest_jd_ut)
        || !(max_offset_days > 0.0)
        || !(direction == -1.0 || direction == 1.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();
    *out_iteration_count = 0;

    double inside_residual = NAN;
    Status status = eval_local_solar_transit_contact_residual(
        data,
        body_radius_sign,
        greatest_jd_ut,
        &inside_residual);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!(inside_residual <= 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    const double probe_step = 0.02;
    SplitJulianDate inside_jd = greatest_jd_ut;
    SplitJulianDate outside_jd = invalid_jd();
    double outside_residual = NAN;
    for (double offset = probe_step; offset <= max_offset_days + 1.0e-12; offset += probe_step) {
        const SplitJulianDate jd = greatest_jd_ut + direction * offset;
        status = eval_local_solar_transit_contact_residual(
            data,
            body_radius_sign,
            jd,
            &outside_residual);
        if (status != TAIYIN_STATUS_OK) return status;
        if (outside_residual >= 0.0) {
            outside_jd = jd;
            break;
        }
        inside_jd = jd;
        inside_residual = outside_residual;
    }
    if (!split_julian_date_is_finite(outside_jd)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    SplitJulianDate lo = std::min(inside_jd, outside_jd);
    SplitJulianDate hi = std::max(inside_jd, outside_jd);
    for (int iter = 0; iter < MAX_SOLAR_TRANSIT_CONTACT_ITERATIONS; ++iter) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        double residual = NAN;
        status = eval_local_solar_transit_contact_residual(
            data,
            body_radius_sign,
            mid,
            &residual);
        if (status != TAIYIN_STATUS_OK) return status;
        if ((hi - lo) <= SOLAR_TRANSIT_CONTACT_TIME_TOLERANCE_DAYS || std::fabs(residual) <= 1.0e-12) {
            *out_jd_ut = mid;
            *out_iteration_count = iter + 1;
            return TAIYIN_STATUS_OK;
        }
        const bool mid_inside = residual <= 0.0;
        if (direction < 0.0) {
            if (mid_inside) {
                hi = mid;
            } else {
                lo = mid;
            }
        } else {
            if (mid_inside) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
    }

    *out_jd_ut = lo + 0.5 * (hi - lo);
    *out_iteration_count = MAX_SOLAR_TRANSIT_CONTACT_ITERATIONS;
    return TAIYIN_STATUS_OK;
}

Status body_is_inferior_conjunction_candidate(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint64_t flags,
    bool* out_inferior,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_inferior || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_inferior = false;

    const uint32_t xyz_flags = static_cast<uint32_t>(flags) | TAIYIN_NATIVE_POSITION_XYZ;
    double body_xyz[6] = {};
    Status status = calc_position_ut(
        context,
        ephemeris_elongation_body(body_id),
        jd_ut,
        xyz_flags,
        body_xyz,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }

    double sun_xyz[6] = {};
    status = calc_position_ut(
        context,
        TAIYIN_BODY_SUN,
        jd_ut,
        xyz_flags,
        sun_xyz,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }

    const double body_distance = std::sqrt(
        body_xyz[0] * body_xyz[0] + body_xyz[1] * body_xyz[1] + body_xyz[2] * body_xyz[2]);
    const double sun_distance = std::sqrt(
        sun_xyz[0] * sun_xyz[0] + sun_xyz[1] * sun_xyz[1] + sun_xyz[2] * sun_xyz[2]);
    if (!std::isfinite(body_distance) || !std::isfinite(sun_distance)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_inferior = body_distance < sun_distance;
    return TAIYIN_STATUS_OK;
}

Status solar_transit_node_gate_passes(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_ut,
    uint32_t position_flags,
    bool* out_passes,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_passes || !split_julian_date_is_finite(jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_passes = false;
    const NativeCalcContext candidate_context =
        solar_transit_candidate_context(*context, position_flags);
    double position[6] = {};
    const Status status = calc_position_ut(
        &candidate_context,
        ephemeris_elongation_body(body_id),
        jd_ut,
        position_flags | TAIYIN_NATIVE_POSITION_RADIANS,
        position,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    const double latitude = position[1];
    if (!std::isfinite(latitude)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_passes = std::fabs(latitude) <= SOLAR_TRANSIT_NODE_GATE_LATITUDE_RAD;
    return TAIYIN_STATUS_OK;
}

// Solar-transit k follows the same role as eclipse lunation k: it is a compact
// candidate index, not a proof that a transit exists.  The seed currently uses
// a mean inferior-conjunction period; the search window widens with |k| so
// ancient/future drift is absorbed before the real geometry is solved.
int64_t solar_transit_k_for_jd(
    int body_id,
    SplitJulianDate jd_start_ut,
    bool reverse
) noexcept {
    SplitJulianDate base;
    double period = 0.0;
    if (!solar_transit_synodic_seed(body_id, &base, &period) || !(period > 0.0)) {
        return 0;
    }
    const double raw = (jd_start_ut - base) / period;
    const double k = reverse ? std::ceil(raw) + 1.0 : std::floor(raw) - 1.0;
    if (!std::isfinite(k)) {
        return 0;
    }
    return static_cast<int64_t>(k);
}

SplitJulianDate solar_transit_seed_jd_ut(
    int body_id,
    int64_t k
) noexcept {
    SplitJulianDate base;
    double period = 0.0;
    if (!solar_transit_synodic_seed(body_id, &base, &period)) {
        return invalid_jd();
    }
    return base + static_cast<double>(k) * period;
}

double solar_transit_empirical_seed_correction_days(
    int body_id,
    int64_t k
) noexcept {
    struct PoissonTerm {
        double frequency_cycles_per_k;
        int k_power;
        double sin_coeff;
        double cos_coeff;
    };
    const double* poly = nullptr;
    const PoissonTerm* terms = nullptr;
    size_t term_count = 0;

    static const double mercury_poly[] = {
        -2.65805822841193162e+00,
        +3.09964177813690372e-02,
        -2.44183172519672961e-02,
        -2.01749954676924241e-02,
    };
    static const PoissonTerm mercury_terms[] = {
        {0.017247859678, 0, -6.20638946776787037e+00, +3.26711984637353270e+00},
        {0.017247859678, 1, -8.54038736447258251e-01, -8.18940812350077985e-01},
        {0.015504280643, 0, -5.56425077575121052e-01, -7.33997267195320990e-01},
        {0.015504280643, 1, -3.09314315764283643e-01, +1.39710409246646905e-01},
        {0.001743579035, 0, -3.93980171670783030e-02, +1.66951167833826769e-01},
        {0.001743579035, 1, -1.05431449956147671e-01, -1.55626554371668373e-02},
    };
    static const double venus_poly[] = {
        -3.11120064292121801e-01,
        +6.34635798888830038e-01,
        -6.83170651565805009e+00,
        -4.43787328901102396e-01,
    };
    static const PoissonTerm venus_terms[] = {
        {0.001368061037, 0, +2.02521593051717907e+00, +5.07866972848727505e-01},
        {0.001368061037, 1, -2.30283733010666403e+00, -3.68834758209282487e+00},
        {0.001368061037, 2, -2.28714726775616484e+00, +6.08633826811089307e+00},
    };

    switch (body_id) {
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        poly = mercury_poly;
        terms = mercury_terms;
        term_count = sizeof(mercury_terms) / sizeof(mercury_terms[0]);
        break;
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
        poly = venus_poly;
        terms = venus_terms;
        term_count = sizeof(venus_terms) / sizeof(venus_terms[0]);
        break;
    default:
        return NAN;
    }

    const double x = static_cast<double>(k) / 50000.0;
    double correction = ((poly[3] * x + poly[2]) * x + poly[1]) * x + poly[0];
    for (size_t i = 0; i < term_count; ++i) {
        double scale = 1.0;
        for (int power = 0; power < terms[i].k_power; ++power) {
            scale *= x;
        }
        const double angle = 2.0 * TAIYIN_PI * terms[i].frequency_cycles_per_k * static_cast<double>(k);
        correction += scale * (terms[i].sin_coeff * std::sin(angle)
            + terms[i].cos_coeff * std::cos(angle));
    }
    return correction;
}

SplitJulianDate solar_transit_empirical_seed_jd_ut(
    int body_id,
    int64_t k
) noexcept {
    const SplitJulianDate mean_seed = solar_transit_seed_jd_ut(body_id, k);
    const double correction = solar_transit_empirical_seed_correction_days(body_id, k);
    if (!split_julian_date_is_finite(mean_seed) || !std::isfinite(correction)) {
        return invalid_jd();
    }
    return mean_seed + correction;
}

double solar_transit_empirical_seed_half_window_days(int body_id) noexcept {
    return body_id == TAIYIN_BODY_VENUS || body_id == TAIYIN_BODY_VENUS_BARYCENTER
        ? 1.0
        : 3.0;
}

double solar_transit_seed_search_half_window_days(
    int body_id,
    int64_t k
) noexcept {
    double period = 0.0;
    if (!solar_transit_synodic_seed(body_id, nullptr, &period) || !(period > 0.0)) {
        return NAN;
    }
    const double k_distance_from_seed = std::fabs(static_cast<double>(k));
    const double drift_allowance = 2.0 + k_distance_from_seed * 0.0012;
    const double max_half_window = 0.52 * period;
    const double min_half_window = body_id == TAIYIN_BODY_VENUS || body_id == TAIYIN_BODY_VENUS_BARYCENTER
        ? 20.0
        : 16.0;
    return std::min(max_half_window, std::max(min_half_window, drift_allowance));
}

Status find_solar_transit_inferior_conjunction_near_seed(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate predicted,
    double half_window,
    uint32_t position_flags,
    SplitJulianDate* out_conjunction_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_conjunction_jd_ut || !is_inner_planet_transit_body(body_id)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_conjunction_jd_ut = invalid_jd();

    if (!split_julian_date_is_finite(predicted) || !std::isfinite(half_window) || !(half_window > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const NativeCalcContext candidate_context =
        solar_transit_candidate_context(*context, position_flags);
    SplitJulianDate conjunctions[6] = {};
    size_t conjunction_count = 0;
    Status status = search_body_aspect_crossings_ut(
        &candidate_context,
        ephemeris_elongation_body(body_id),
        TAIYIN_BODY_SUN,
        0.0,
        predicted - half_window,
        predicted + half_window,
        recommended_solar_transit_conjunction_step_days(body_id),
        position_flags,
        conjunctions,
        sizeof(conjunctions) / sizeof(conjunctions[0]),
        &conjunction_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }

    SplitJulianDate best = invalid_jd();
    double best_delta = std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < conjunction_count; ++i) {
        const SplitJulianDate jd = conjunctions[i];
        bool inferior = false;
        status = body_is_inferior_conjunction_candidate(
            context,
            body_id,
            jd,
            position_flags,
            &inferior,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!inferior) continue;
        const double delta = std::fabs(jd - predicted);
        if (delta < best_delta) {
            best = jd;
            best_delta = delta;
        }
    }

    if (!split_julian_date_is_finite(best)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }
    *out_conjunction_jd_ut = best;
    return TAIYIN_STATUS_OK;
}

Status solve_solar_transit_inferior_conjunction_for_k(
    const NativeCalcContext* context,
    int body_id,
    int64_t k,
    uint32_t position_flags,
    SplitJulianDate* out_conjunction_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out_conjunction_jd_ut || !is_inner_planet_transit_body(body_id)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_conjunction_jd_ut = invalid_jd();

    const SplitJulianDate empirical_seed = solar_transit_empirical_seed_jd_ut(body_id, k);
    const double empirical_half_window = solar_transit_empirical_seed_half_window_days(body_id);
    if (split_julian_date_is_finite(empirical_seed)) {
        const Status empirical_status = find_solar_transit_inferior_conjunction_near_seed(
            context,
            body_id,
            empirical_seed,
            empirical_half_window,
            position_flags,
            out_conjunction_jd_ut,
            diagnostic);
        if (empirical_status == TAIYIN_STATUS_OK) {
            return TAIYIN_STATUS_OK;
        }
        if (empirical_status != TAIYIN_EVENT_ERROR_NOT_FOUND) {
            return empirical_status;
        }
    }

    double period = 0.0;
    if (!solar_transit_synodic_seed(body_id, nullptr, &period)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const SplitJulianDate mean_seed = solar_transit_seed_jd_ut(body_id, k);
    const double wide_half_window = solar_transit_seed_search_half_window_days(body_id, k);
    if (!split_julian_date_is_finite(mean_seed) || !std::isfinite(wide_half_window)
        || !(wide_half_window > 0.0) || !(wide_half_window < 0.6 * period)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    return find_solar_transit_inferior_conjunction_near_seed(
        context,
        body_id,
        mean_seed,
        wide_half_window,
        position_flags,
        out_conjunction_jd_ut,
        diagnostic);
}

Status refine_elongation_rate_root(
    ElongationEvalContext* eval_context,
    SplitJulianDate lower_jd_ut,
    double lower_rate,
    SplitJulianDate upper_jd_ut,
    double upper_rate,
    SplitJulianDate* out_jd_ut,
    int* out_iteration_count
) noexcept {
    if (!eval_context
        || !out_jd_ut
        || !out_iteration_count
        || !(lower_jd_ut < upper_jd_ut)
        || !std::isfinite(lower_rate)
        || !std::isfinite(upper_rate)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd_ut = invalid_jd();
    *out_iteration_count = 0;

    if (std::fabs(lower_rate) <= ELONGATION_RATE_TOLERANCE_RAD_PER_DAY) {
        *out_jd_ut = lower_jd_ut;
        return TAIYIN_STATUS_OK;
    }
    if (std::fabs(upper_rate) <= ELONGATION_RATE_TOLERANCE_RAD_PER_DAY) {
        *out_jd_ut = upper_jd_ut;
        return TAIYIN_STATUS_OK;
    }
    if (!(lower_rate > 0.0 && upper_rate < 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    SplitJulianDate lo = lower_jd_ut;
    SplitJulianDate hi = upper_jd_ut;
    for (int iter = 0; iter < MAX_ELONGATION_ITERATIONS; ++iter) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        ElongationEvalSample sample;
        Status status = eval_elongation_sample(eval_context, mid, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        SplitJulianDate candidate = mid;
        if (std::isfinite(sample.elongation_acceleration_rad_per_day2)
            && std::fabs(sample.elongation_acceleration_rad_per_day2) > 1.0e-16) {
            const SplitJulianDate newton = mid
                - sample.elongation_rate_rad_per_day / sample.elongation_acceleration_rad_per_day2;
            if (split_julian_date_is_finite(newton) && newton > lo && newton < hi) {
                ElongationEvalSample newton_sample;
                status = eval_elongation_sample(eval_context, newton, &newton_sample);
                if (status != TAIYIN_STATUS_OK) {
                    return normalize_event_status(status);
                }
                if (std::isfinite(newton_sample.elongation_rate_rad_per_day)) {
                    candidate = newton;
                    sample = newton_sample;
                }
            }
        }
        if (std::fabs(sample.elongation_rate_rad_per_day) <= ELONGATION_RATE_TOLERANCE_RAD_PER_DAY
            || (hi - lo) <= ELONGATION_TIME_TOLERANCE_DAYS) {
            *out_jd_ut = candidate;
            *out_iteration_count = iter + 1;
            return TAIYIN_STATUS_OK;
        }
        if (sample.elongation_rate_rad_per_day > 0.0) {
            lo = candidate;
        } else {
            hi = candidate;
        }
    }

    *out_jd_ut = lo + 0.5 * (hi - lo);
    *out_iteration_count = MAX_ELONGATION_ITERATIONS;
    return TAIYIN_STATUS_OK;
}

Status search_greatest_elongation_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    GreatestElongationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = GreatestElongationSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out
        || !is_inner_planet_elongation_body(body_id)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u
        || search_flags != 0u) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, start_jd_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const double span_days = end_jd_ut - start_jd_ut;
    const double max_step_days = recommended_greatest_elongation_step_days(body_id);
    const double raw_step_count = std::ceil(span_days / max_step_days);
    if (!std::isfinite(raw_step_count)
        || raw_step_count <= 0.0
        || raw_step_count > static_cast<double>(MAX_BOUNDED_SEARCH_STEPS)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    ElongationEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_id = ephemeris_elongation_body(body_id);
    eval_context.flags = position_flags;
    eval_context.diagnostic = diagnostic;

    const int step_count = static_cast<int>(raw_step_count);
    const double step_days = span_days / static_cast<double>(step_count);

    SplitJulianDate best_jd = invalid_jd();
    double best_elongation = -1.0;
    int best_iterations = 0;
    bool found = false;

    ElongationEvalSample previous_sample;
    Status status = eval_elongation_sample(&eval_context, start_jd_ut, &previous_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    SplitJulianDate previous_jd = start_jd_ut;

    for (int i = 1; i <= step_count; ++i) {
        const SplitJulianDate jd = i == step_count ? end_jd_ut : start_jd_ut + step_days * static_cast<double>(i);
        ElongationEvalSample sample;
        status = eval_elongation_sample(&eval_context, jd, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }

        if (previous_sample.elongation_rate_rad_per_day > 0.0
            && sample.elongation_rate_rad_per_day < 0.0) {
            SplitJulianDate root_jd = invalid_jd();
            int iterations = 0;
            status = refine_elongation_rate_root(
                &eval_context,
                previous_jd,
                previous_sample.elongation_rate_rad_per_day,
                jd,
                sample.elongation_rate_rad_per_day,
                &root_jd,
                &iterations);
            if (status != TAIYIN_STATUS_OK) {
                return status;
            }
            ElongationEvalSample root_sample;
            status = eval_elongation_sample(&eval_context, root_jd, &root_sample);
            if (status != TAIYIN_STATUS_OK) {
                return normalize_event_status(status);
            }
            if (!found || root_sample.elongation_rad > best_elongation) {
                found = true;
                best_jd = root_jd;
                best_elongation = root_sample.elongation_rad;
                best_iterations = iterations;
            }
        }

        previous_sample = sample;
        previous_jd = jd;
    }

    if (!found) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    ElongationEvalSample final_sample;
    status = eval_elongation_sample(&eval_context, best_jd, &final_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }

    out->jd_ut = best_jd;
    out->elongation_rad = final_sample.elongation_rad;
    out->relative_longitude_rad = final_sample.relative_longitude_rad;
    out->kind = final_sample.relative_longitude_rad >= 0.0
        ? TAIYIN_GREATEST_ELONGATION_EASTERN
        : TAIYIN_GREATEST_ELONGATION_WESTERN;
    out->body_id = body_id;
    out->iteration_count = best_iterations;
    out->evaluation_count = eval_context.evaluation_count;

    EphemerisEvalDiagnostic phenomena_diagnostic;
    status = calc_body_phenomena_ut(
        context,
        physical_elongation_body(body_id),
        best_jd,
        flags,
        &out->phenomena,
        &phenomena_diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) {
            *diagnostic = phenomena_diagnostic;
        }
        return status;
    }
    out->phenomena.solar_elongation_rad = final_sample.elongation_rad;
    return TAIYIN_STATUS_OK;
}

Status refine_angular_separation_rate_root(
    AngularSeparationEvalContext* eval_context,
    SplitJulianDate lower_jd,
    double lower_rate,
    SplitJulianDate upper_jd,
    double upper_rate,
    SplitJulianDate* out_jd,
    int* out_iteration_count
) noexcept {
    if (!eval_context
        || !out_jd
        || !out_iteration_count
        || !(lower_jd < upper_jd)
        || !std::isfinite(lower_rate)
        || !std::isfinite(upper_rate)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd = invalid_jd();
    *out_iteration_count = 0;

    if (std::fabs(lower_rate) <= ANGULAR_SEPARATION_RATE_TOLERANCE_RAD_PER_DAY) {
        *out_jd = lower_jd;
        return TAIYIN_STATUS_OK;
    }
    if (std::fabs(upper_rate) <= ANGULAR_SEPARATION_RATE_TOLERANCE_RAD_PER_DAY) {
        *out_jd = upper_jd;
        return TAIYIN_STATUS_OK;
    }
    if (!(lower_rate < 0.0 && upper_rate > 0.0)) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    SplitJulianDate lo = lower_jd;
    SplitJulianDate hi = upper_jd;
    for (int iter = 0; iter < MAX_ANGULAR_SEPARATION_ITERATIONS; ++iter) {
        const SplitJulianDate mid = lo + 0.5 * (hi - lo);
        AngularSeparationEvalSample sample;
        Status status = eval_angular_separation_sample(eval_context, mid, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        SplitJulianDate candidate = mid;
        if (std::isfinite(sample.separation_acceleration_rad_per_day2)
            && std::fabs(sample.separation_acceleration_rad_per_day2) > 1.0e-16) {
            const SplitJulianDate newton = mid
                - sample.separation_rate_rad_per_day / sample.separation_acceleration_rad_per_day2;
            if (split_julian_date_is_finite(newton) && newton > lo && newton < hi) {
                AngularSeparationEvalSample newton_sample;
                status = eval_angular_separation_sample(eval_context, newton, &newton_sample);
                if (status != TAIYIN_STATUS_OK) {
                    return normalize_event_status(status);
                }
                if (std::isfinite(newton_sample.separation_rate_rad_per_day)) {
                    candidate = newton;
                    sample = newton_sample;
                }
            }
        }
        if (std::fabs(sample.separation_rate_rad_per_day) <= ANGULAR_SEPARATION_RATE_TOLERANCE_RAD_PER_DAY
            || (hi - lo) <= ANGULAR_SEPARATION_TIME_TOLERANCE_DAYS) {
            *out_jd = candidate;
            *out_iteration_count = iter + 1;
            return TAIYIN_STATUS_OK;
        }
        if (sample.separation_rate_rad_per_day < 0.0) {
            lo = candidate;
        } else {
            hi = candidate;
        }
    }

    *out_jd = lo + 0.5 * (hi - lo);
    *out_iteration_count = MAX_ANGULAR_SEPARATION_ITERATIONS;
    return TAIYIN_STATUS_OK;
}

Status minimize_angular_separation_value(
    AngularSeparationEvalContext* eval_context,
    SplitJulianDate lower_jd,
    SplitJulianDate upper_jd,
    SplitJulianDate* out_jd,
    int* out_iteration_count
) noexcept {
    if (!eval_context
        || !out_jd
        || !out_iteration_count
        || !(lower_jd < upper_jd)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_jd = invalid_jd();
    *out_iteration_count = 0;

    const double inv_phi = 0.6180339887498948482;
    double a = 0.0;
    double b = upper_jd - lower_jd;
    double c = b - inv_phi * (b - a);
    double d = a + inv_phi * (b - a);
    AngularSeparationEvalSample c_sample;
    AngularSeparationEvalSample d_sample;
    Status status = eval_angular_separation_sample(eval_context, lower_jd + c, &c_sample);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    status = eval_angular_separation_sample(eval_context, lower_jd + d, &d_sample);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    for (int iter = 0;
         iter < MAX_ANGULAR_SEPARATION_ITERATIONS && (b - a) > ANGULAR_SEPARATION_TIME_TOLERANCE_DAYS;
         ++iter) {
        if (c_sample.separation_rad > d_sample.separation_rad) {
            a = c;
            c = d;
            c_sample = d_sample;
            d = a + inv_phi * (b - a);
            status = eval_angular_separation_sample(eval_context, lower_jd + d, &d_sample);
            if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        } else {
            b = d;
            d = c;
            d_sample = c_sample;
            c = b - inv_phi * (b - a);
            status = eval_angular_separation_sample(eval_context, lower_jd + c, &c_sample);
            if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        }
        *out_iteration_count = iter + 1;
    }

    *out_jd = lower_jd + 0.5 * (a + b);
    return TAIYIN_STATUS_OK;
}

Status search_minimum_angular_separation_impl(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const char* star_key,
    SplitJulianDate start_jd,
    SplitJulianDate end_jd,
    double max_step_days,
    bool use_ut,
    uint64_t flags,
    AngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = AngularSeparationSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    const bool use_star = star_key && star_key[0] != '\0';
    if (!context
        || !out
        || body_a_id == 0
        || (use_star ? body_b_id != 0 : body_b_id == 0)
        || (!use_star && body_a_id == body_b_id)
        || !split_julian_date_is_finite(start_jd)
        || !split_julian_date_is_finite(end_jd)
        || !std::isfinite(max_step_days)
        || !(end_jd > start_jd)
        || !(max_step_days > 0.0)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_a_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u
        || search_flags != 0u) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_a_id, start_jd);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const double span_days = end_jd - start_jd;
    const double raw_step_count = std::ceil(span_days / max_step_days);
    if (!std::isfinite(raw_step_count)
        || raw_step_count <= 0.0
        || raw_step_count > static_cast<double>(MAX_BOUNDED_SEARCH_STEPS)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_a_id, start_jd);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    AngularSeparationEvalContext eval_context;
    eval_context.context = context;
    eval_context.body_a_id = body_a_id;
    eval_context.body_b_id = body_b_id;
    eval_context.star_key = use_star ? star_key : 0;
    eval_context.flags = position_flags;
    eval_context.use_ut = use_ut;
    eval_context.diagnostic = diagnostic;

    const int step_count = static_cast<int>(raw_step_count);
    const double step_days = span_days / static_cast<double>(step_count);

    SplitJulianDate best_jd = invalid_jd();
    double best_separation = std::numeric_limits<double>::infinity();
    int best_iterations = 0;
    bool found = false;

    AngularSeparationEvalSample previous_sample;
    Status status = eval_angular_separation_sample(&eval_context, start_jd, &previous_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }
    SplitJulianDate previous_jd = start_jd;

    SplitJulianDate best_sampled_jd = start_jd;
    double best_sampled_separation = previous_sample.separation_rad;

    for (int i = 1; i <= step_count; ++i) {
        const SplitJulianDate jd = i == step_count ? end_jd : start_jd + step_days * static_cast<double>(i);
        AngularSeparationEvalSample sample;
        status = eval_angular_separation_sample(&eval_context, jd, &sample);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (sample.separation_rad < best_sampled_separation) {
            best_sampled_separation = sample.separation_rad;
            best_sampled_jd = jd;
        }

        if (previous_sample.separation_rate_rad_per_day < 0.0
            && sample.separation_rate_rad_per_day > 0.0) {
            SplitJulianDate root_jd = invalid_jd();
            int iterations = 0;
            status = refine_angular_separation_rate_root(
                &eval_context,
                previous_jd,
                previous_sample.separation_rate_rad_per_day,
                jd,
                sample.separation_rate_rad_per_day,
                &root_jd,
                &iterations);
            if (status != TAIYIN_STATUS_OK) {
                return status;
            }
            AngularSeparationEvalSample root_sample;
            status = eval_angular_separation_sample(&eval_context, root_jd, &root_sample);
            if (status != TAIYIN_STATUS_OK) {
                return normalize_event_status(status);
            }
            if (!found || root_sample.separation_rad < best_separation) {
                found = true;
                best_jd = root_jd;
                best_separation = root_sample.separation_rad;
                best_iterations = iterations;
            }
        }

        previous_sample = sample;
        previous_jd = jd;
    }

    if (!found) {
        if (best_sampled_jd <= start_jd || best_sampled_jd >= end_jd) {
            best_jd = best_sampled_jd;
            best_iterations = 0;
        } else {
            const SplitJulianDate fallback_lower = std::max(start_jd, best_sampled_jd - step_days);
            const SplitJulianDate fallback_upper = std::min(end_jd, best_sampled_jd + step_days);
            if (!(fallback_upper > fallback_lower)) {
                return TAIYIN_EVENT_ERROR_NOT_FOUND;
            }
            status = minimize_angular_separation_value(
                &eval_context,
                fallback_lower,
                fallback_upper,
                &best_jd,
                &best_iterations);
            if (status != TAIYIN_STATUS_OK) {
                return status;
            }
        }
    }

    AngularSeparationEvalSample final_sample;
    status = eval_angular_separation_sample(&eval_context, best_jd, &final_sample);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }

    out->jd = best_jd;
    out->separation_rad = final_sample.separation_rad;
    out->separation_rate_rad_per_day = final_sample.separation_rate_rad_per_day;
    out->body_a_id = body_a_id;
    out->body_b_id = body_b_id;
    out->iteration_count = best_iterations;
    out->evaluation_count = eval_context.evaluation_count;
    return TAIYIN_STATUS_OK;
}

Status search_solar_transit_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = SolarTransitSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out
        || !is_inner_planet_transit_body(body_id)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    const uint64_t known_search_flags = 0u;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ | TAIYIN_NATIVE_POSITION_EQUATORIAL)) != 0u
        || (position_flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u
        || (search_flags & ~known_search_flags) != 0u
        || native_context_has_topocentric_observer(*context)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, start_jd_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    const double conjunction_step_days = recommended_solar_transit_conjunction_step_days(body_id);
    const double candidate_half_window_days = solar_transit_candidate_half_window_days(body_id);
    const SplitJulianDate candidate_search_start = start_jd_ut - candidate_half_window_days;
    const SplitJulianDate candidate_search_end = end_jd_ut + candidate_half_window_days;
    const double candidate_search_span_days = candidate_search_end - candidate_search_start;
    const double rough_capacity = std::ceil(candidate_search_span_days / 40.0) + 16.0;
    if (!std::isfinite(rough_capacity)
        || rough_capacity <= 0.0
        || rough_capacity > 20000.0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::vector<SplitJulianDate> conjunctions;
    try {
        conjunctions.assign(static_cast<size_t>(rough_capacity), invalid_jd());
    } catch (...) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_OUT_OF_MEMORY, body_id, start_jd_ut);
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    size_t conjunction_count = 0;
    Status status = search_body_aspect_crossings_ut(
        context,
        ephemeris_elongation_body(body_id),
        TAIYIN_BODY_SUN,
        0.0,
        candidate_search_start,
        candidate_search_end,
        conjunction_step_days,
        position_flags,
        conjunctions.data(),
        conjunctions.size(),
        &conjunction_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }

    for (size_t i = 0; i < conjunction_count; ++i) {
        const SplitJulianDate conjunction_jd = conjunctions[i];
        bool inferior_conjunction = false;
        status = body_is_inferior_conjunction_candidate(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &inferior_conjunction,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!inferior_conjunction) {
            continue;
        }
        bool node_gate_passes = false;
        status = solar_transit_node_gate_passes(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &node_gate_passes,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!node_gate_passes) {
            continue;
        }

        const SplitJulianDate window_start = conjunction_jd - candidate_half_window_days;
        const SplitJulianDate window_end = conjunction_jd + candidate_half_window_days;
        if (!(window_end > window_start)) {
            continue;
        }

        AngularSeparationSearchResult minimum;
        status = search_minimum_angular_separation_ut(
            context,
            ephemeris_elongation_body(body_id),
            TAIYIN_BODY_SUN,
            window_start,
            window_end,
            0.03,
            position_flags,
            &minimum,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) {
                continue;
            }
            return normalize_event_status(status);
        }
        if (minimum.jd <= window_start + SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS
            || minimum.jd >= window_end - SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
            continue;
        }

        double sun_radius = NAN;
        double body_radius = NAN;
        status = eval_solar_transit_radii(
            context,
            body_id,
            minimum.jd,
            position_flags,
            &sun_radius,
            &body_radius,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        const double exterior_radius = sun_radius + body_radius;
        const double interior_radius = sun_radius - body_radius;
        if (!std::isfinite(exterior_radius)
            || !std::isfinite(interior_radius)
            || !(exterior_radius > 0.0)
            || !(interior_radius > 0.0)
            || minimum.separation_rad > exterior_radius) {
            continue;
        }

        SolarTransitSearchResult candidate;
        SolarTransitContactContext exterior_contact;
        exterior_contact.context = context;
        exterior_contact.body_id = body_id;
        exterior_contact.flags = position_flags;
        exterior_contact.body_radius_sign = 1.0;
        exterior_contact.diagnostic = diagnostic;

        int t1_iterations = 0;
        int t4_iterations = 0;
        status = solve_solar_transit_contact(
            &exterior_contact,
            minimum.jd,
            -1.0,
            candidate_half_window_days,
            &candidate.t1_jd_ut,
            &t1_iterations);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        status = solve_solar_transit_contact(
            &exterior_contact,
            minimum.jd,
            1.0,
            candidate_half_window_days,
            &candidate.t4_jd_ut,
            &t4_iterations);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }

        const bool in_requested_interval = minimum.jd >= start_jd_ut && minimum.jd <= end_jd_ut;
        if (!in_requested_interval) {
            continue;
        }

        candidate.kind = TAIYIN_SOLAR_TRANSIT_PARTIAL;
        int interior_iterations = 0;
        int interior_evaluations = 0;
        if (minimum.separation_rad <= interior_radius) {
            SolarTransitContactContext interior_contact;
            interior_contact.context = context;
            interior_contact.body_id = body_id;
            interior_contact.flags = position_flags;
            interior_contact.body_radius_sign = -1.0;
            interior_contact.diagnostic = diagnostic;
            int t2_iterations = 0;
            int t3_iterations = 0;
            status = solve_solar_transit_contact(
                &interior_contact,
                minimum.jd,
                -1.0,
                candidate_half_window_days,
                &candidate.t2_jd_ut,
                &t2_iterations);
            if (status == TAIYIN_STATUS_OK) {
                status = solve_solar_transit_contact(
                    &interior_contact,
                    minimum.jd,
                    1.0,
                    candidate_half_window_days,
                    &candidate.t3_jd_ut,
                    &t3_iterations);
            }
            if (status != TAIYIN_STATUS_OK) {
                return normalize_event_status(status);
            }
            candidate.kind |= TAIYIN_SOLAR_TRANSIT_FULL_DISK;
            interior_iterations = t2_iterations + t3_iterations;
            interior_evaluations = interior_contact.evaluation_count;
        }

        candidate.body_id = body_id;
        candidate.greatest_jd_ut = minimum.jd;
        candidate.minimum_separation_rad = minimum.separation_rad;
        candidate.sun_radius_rad = sun_radius;
        candidate.body_radius_rad = body_radius;
        candidate.iteration_count = minimum.iteration_count + t1_iterations + t4_iterations + interior_iterations;
        candidate.evaluation_count =
            minimum.evaluation_count + exterior_contact.evaluation_count + interior_evaluations;
        *out = candidate;
        return TAIYIN_STATUS_OK;
    }

    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_next_solar_transit_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = SolarTransitSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out
        || !is_inner_planet_transit_body(body_id)
        || !split_julian_date_is_finite(jd_start_ut)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, jd_start_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    const uint64_t known_search_flags = TAIYIN_EVENT_SEARCH_REVERSE;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ
                           | TAIYIN_NATIVE_POSITION_EQUATORIAL
                           | TAIYIN_NATIVE_POSITION_TOPOCENTRIC)) != 0u
        || (search_flags & ~known_search_flags) != 0u
        || native_context_has_topocentric_observer(*context)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, jd_start_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const bool reverse = (search_flags & TAIYIN_EVENT_SEARCH_REVERSE) != 0u;
    const int64_t direction = reverse ? -1 : 1;
    int64_t k = solar_transit_k_for_jd(body_id, jd_start_ut, reverse);
    for (int i = 0; i < MAX_SOLAR_TRANSIT_CANDIDATE_CYCLES; ++i, k += direction) {
        SplitJulianDate conjunction_jd = invalid_jd();
        Status status = solve_solar_transit_inferior_conjunction_for_k(
            context,
            body_id,
            k,
            position_flags,
            &conjunction_jd,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            return status;
        }
        bool node_gate_passes = false;
        status = solar_transit_node_gate_passes(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &node_gate_passes,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            return normalize_event_status(status);
        }
        if (!node_gate_passes) {
            continue;
        }

        SolarTransitSearchResult candidate;
        status = compute_geocentric_solar_transit_candidate(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &candidate,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            return normalize_event_status(status);
        }
        if (candidate.kind == 0u) continue;
        if (reverse) {
            if (candidate.greatest_jd_ut >= jd_start_ut - SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
                continue;
            }
        } else if (candidate.greatest_jd_ut <= jd_start_ut + SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
            continue;
        }
        *out = candidate;
        return TAIYIN_STATUS_OK;
    }

    set_basic_diagnostic(diagnostic, TAIYIN_EVENT_ERROR_NOT_FOUND, body_id, jd_start_ut);
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

uint32_t local_solar_transit_contact_visibility_flag(size_t index) noexcept {
    switch (index) {
    case TAIYIN_SOLAR_TRANSIT_CONTACT_T1:
        return TAIYIN_SOLAR_TRANSIT_T1_VISIBLE;
    case TAIYIN_SOLAR_TRANSIT_CONTACT_T2:
        return TAIYIN_SOLAR_TRANSIT_T2_VISIBLE;
    case TAIYIN_SOLAR_TRANSIT_CONTACT_GREATEST:
        return TAIYIN_SOLAR_TRANSIT_GREATEST_VISIBLE;
    case TAIYIN_SOLAR_TRANSIT_CONTACT_T3:
        return TAIYIN_SOLAR_TRANSIT_T3_VISIBLE;
    case TAIYIN_SOLAR_TRANSIT_CONTACT_T4:
        return TAIYIN_SOLAR_TRANSIT_T4_VISIBLE;
    default:
        return 0u;
    }
}

void local_solar_transit_contact_times(
    const SolarTransitSearchResult& transit,
    SplitJulianDate out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT]
) noexcept {
    out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_T1] = transit.t1_jd_ut;
    out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_T2] = transit.t2_jd_ut;
    out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_GREATEST] = transit.greatest_jd_ut;
    out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_T3] = transit.t3_jd_ut;
    out_jd_ut[TAIYIN_SOLAR_TRANSIT_CONTACT_T4] = transit.t4_jd_ut;
}

bool finite_solar_transit_interval(
    const SolarTransitSearchResult& transit,
    SplitJulianDate* out_start_jd_ut,
    SplitJulianDate* out_end_jd_ut
) noexcept {
    if (!out_start_jd_ut || !out_end_jd_ut) return false;
    SplitJulianDate contact_jd[TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT] = {};
    local_solar_transit_contact_times(transit, contact_jd);
    SplitJulianDate start = invalid_jd();
    SplitJulianDate end = invalid_jd();
    for (size_t i = 0; i < TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT; ++i) {
        const SplitJulianDate jd = contact_jd[i];
        if (!split_julian_date_is_finite(jd)) continue;
        if (!split_julian_date_is_finite(start) || jd < start) start = jd;
        if (!split_julian_date_is_finite(end) || jd > end) end = jd;
    }
    if (!split_julian_date_is_finite(start) || !split_julian_date_is_finite(end) || !(end > start)) {
        return false;
    }
    *out_start_jd_ut = start;
    *out_end_jd_ut = end;
    return true;
}

Status compute_topocentric_solar_transit(
    const NativeCalcContext* context,
    const SolarTransitSearchResult& global,
    const NativeObserverLocation& location,
    uint32_t position_flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !is_inner_planet_transit_body(global.body_id)
        || !split_julian_date_is_finite(global.greatest_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = SolarTransitSearchResult();

    LocalSolarTransitEvalContext eval;
    eval.context = context;
    eval.location = location;
    eval.body_id = global.body_id;
    eval.flags = position_flags;
    eval.diagnostic = diagnostic;

    const SplitJulianDate min_lower = global.greatest_jd_ut - LOCAL_SOLAR_TRANSIT_MINIMUM_WINDOW_DAYS;
    const SplitJulianDate min_upper = global.greatest_jd_ut + LOCAL_SOLAR_TRANSIT_MINIMUM_WINDOW_DAYS;
    SplitJulianDate greatest = invalid_jd();
    double minimum_separation = NAN;
    int minimum_iterations = 0;
    Status status = minimize_local_solar_transit_separation(
        &eval,
        min_lower,
        min_upper,
        &greatest,
        &minimum_separation,
        &minimum_iterations);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    if (greatest <= min_lower + SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS
        || greatest >= min_upper - SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    double sun_radius = NAN;
    double body_radius = NAN;
    status = eval_local_solar_transit_radii(
        &eval,
        greatest,
        &sun_radius,
        &body_radius);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    const double exterior_radius = sun_radius + body_radius;
    const double interior_radius = sun_radius - body_radius;
    if (!std::isfinite(exterior_radius)
        || !std::isfinite(interior_radius)
        || !(exterior_radius > 0.0)
        || !(interior_radius > 0.0)
        || minimum_separation > exterior_radius) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    int t1_iterations = 0;
    int t4_iterations = 0;
    const double max_offset = solar_transit_candidate_half_window_days(global.body_id);
    status = solve_local_solar_transit_contact(
        &eval,
        1.0,
        greatest,
        -1.0,
        max_offset,
        &out->t1_jd_ut,
        &t1_iterations);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    status = solve_local_solar_transit_contact(
        &eval,
        1.0,
        greatest,
        1.0,
        max_offset,
        &out->t4_jd_ut,
        &t4_iterations);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    uint32_t kind = TAIYIN_SOLAR_TRANSIT_PARTIAL;
    int interior_iterations = 0;
    if (minimum_separation <= interior_radius) {
        int t2_iterations = 0;
        int t3_iterations = 0;
        status = solve_local_solar_transit_contact(
            &eval,
            -1.0,
            greatest,
            -1.0,
            max_offset,
            &out->t2_jd_ut,
            &t2_iterations);
        if (status == TAIYIN_STATUS_OK) {
            status = solve_local_solar_transit_contact(
                &eval,
                -1.0,
                greatest,
                1.0,
                max_offset,
                &out->t3_jd_ut,
                &t3_iterations);
        }
        if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        kind |= TAIYIN_SOLAR_TRANSIT_FULL_DISK;
        interior_iterations = t2_iterations + t3_iterations;
    }

    out->body_id = global.body_id;
    out->kind = kind;
    out->greatest_jd_ut = greatest;
    out->minimum_separation_rad = minimum_separation;
    out->sun_radius_rad = sun_radius;
    out->body_radius_rad = body_radius;
    out->iteration_count = minimum_iterations + t1_iterations + t4_iterations + interior_iterations;
    out->evaluation_count = eval.evaluation_count;
    return TAIYIN_STATUS_OK;
}

Status compute_geocentric_solar_transit_candidate(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate conjunction_jd_ut,
    uint32_t position_flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out || !is_inner_planet_transit_body(body_id) || !split_julian_date_is_finite(conjunction_jd_ut)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out = SolarTransitSearchResult();
    const double candidate_half_window_days = solar_transit_candidate_half_window_days(body_id);
    const SplitJulianDate window_start = conjunction_jd_ut - candidate_half_window_days;
    const SplitJulianDate window_end = conjunction_jd_ut + candidate_half_window_days;

    AngularSeparationSearchResult minimum;
    Status status = search_minimum_angular_separation_ut(
        context,
        ephemeris_elongation_body(body_id),
        TAIYIN_BODY_SUN,
        window_start,
        window_end,
        0.03,
        position_flags,
        &minimum,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    if (minimum.jd <= window_start + SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS
        || minimum.jd >= window_end - SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
        return TAIYIN_EVENT_ERROR_NOT_FOUND;
    }

    double sun_radius = NAN;
    double body_radius = NAN;
    status = eval_solar_transit_radii(
        context,
        body_id,
        minimum.jd,
        position_flags,
        &sun_radius,
        &body_radius,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    out->body_id = body_id;
    out->greatest_jd_ut = minimum.jd;
    out->minimum_separation_rad = minimum.separation_rad;
    out->sun_radius_rad = sun_radius;
    out->body_radius_rad = body_radius;
    out->iteration_count = minimum.iteration_count;
    out->evaluation_count = minimum.evaluation_count;

    const double exterior_radius = sun_radius + body_radius;
    const double interior_radius = sun_radius - body_radius;
    if (!std::isfinite(exterior_radius)
        || !std::isfinite(interior_radius)
        || !(exterior_radius > 0.0)
        || !(interior_radius > 0.0)
        || minimum.separation_rad > exterior_radius) {
        return TAIYIN_STATUS_OK;
    }

    out->kind = TAIYIN_SOLAR_TRANSIT_PARTIAL;
    SolarTransitContactContext exterior_contact;
    exterior_contact.context = context;
    exterior_contact.body_id = body_id;
    exterior_contact.flags = position_flags;
    exterior_contact.body_radius_sign = 1.0;
    exterior_contact.diagnostic = diagnostic;
    int t1_iterations = 0;
    int t4_iterations = 0;
    status = solve_solar_transit_contact(
        &exterior_contact,
        minimum.jd,
        -1.0,
        candidate_half_window_days,
        &out->t1_jd_ut,
        &t1_iterations);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
    status = solve_solar_transit_contact(
        &exterior_contact,
        minimum.jd,
        1.0,
        candidate_half_window_days,
        &out->t4_jd_ut,
        &t4_iterations);
    if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);

    if (minimum.separation_rad <= interior_radius) {
        SolarTransitContactContext interior_contact;
        interior_contact.context = context;
        interior_contact.body_id = body_id;
        interior_contact.flags = position_flags;
        interior_contact.body_radius_sign = -1.0;
        interior_contact.diagnostic = diagnostic;
        int t2_iterations = 0;
        int t3_iterations = 0;
        status = solve_solar_transit_contact(
            &interior_contact,
            minimum.jd,
            -1.0,
            candidate_half_window_days,
            &out->t2_jd_ut,
            &t2_iterations);
        if (status == TAIYIN_STATUS_OK) {
            status = solve_solar_transit_contact(
                &interior_contact,
                minimum.jd,
                1.0,
                candidate_half_window_days,
                &out->t3_jd_ut,
                &t3_iterations);
        }
        if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        out->kind |= TAIYIN_SOLAR_TRANSIT_FULL_DISK;
        out->iteration_count += t2_iterations + t3_iterations;
        out->evaluation_count += interior_contact.evaluation_count;
    }

    out->iteration_count += t1_iterations + t4_iterations;
    out->evaluation_count += exterior_contact.evaluation_count;
    return TAIYIN_STATUS_OK;
}

Status sample_local_solar_transit_visibility(
    const NativeCalcContext* context,
    const SolarTransitSearchResult& topocentric,
    uint64_t observed_flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!context || !out) return TAIYIN_ERROR_INVALID_ARGUMENT;

    SplitJulianDate contact_jd[TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT] = {};
    local_solar_transit_contact_times(topocentric, contact_jd);
    for (size_t i = 0; i < TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT; ++i) {
        const SplitJulianDate jd = contact_jd[i];
        if (!split_julian_date_is_finite(jd)) continue;
        double altitude = NAN;
        double azimuth = NAN;
        double hour_angle = NAN;
        double distance = NAN;
        const Status status = visibility_sample_body_center_horizontal_ut(
            context,
            TAIYIN_BODY_SUN,
            jd,
            observed_flags,
            &altitude,
            &azimuth,
            &hour_angle,
            &distance,
            nullptr,
            nullptr,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        out->contact_sun_altitude_deg[i] = altitude * TAIYIN_RAD_TO_DEG;
        out->contact_sun_azimuth_deg[i] = azimuth * TAIYIN_RAD_TO_DEG;
        if (altitude >= 0.0) {
            out->visibility_flags |= local_solar_transit_contact_visibility_flag(i);
        }
        (void)hour_angle;
        (void)distance;
    }

    if ((out->visibility_flags
            & (TAIYIN_SOLAR_TRANSIT_T1_VISIBLE
               | TAIYIN_SOLAR_TRANSIT_T2_VISIBLE
               | TAIYIN_SOLAR_TRANSIT_GREATEST_VISIBLE
               | TAIYIN_SOLAR_TRANSIT_T3_VISIBLE
               | TAIYIN_SOLAR_TRANSIT_T4_VISIBLE)) != 0u) {
        out->visibility_flags |= TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER;
    }

    SplitJulianDate start = invalid_jd();
    SplitJulianDate end = invalid_jd();
    if (!finite_solar_transit_interval(topocentric, &start, &end)) {
        return TAIYIN_STATUS_OK;
    }

    VisibilityAltitudeSearchSpec spec;
    spec.body_id = TAIYIN_BODY_SUN;
    spec.start_jd_ut = start - LOCAL_SOLAR_TRANSIT_RISE_SET_PADDING_DAYS;
    spec.end_jd_ut = end + LOCAL_SOLAR_TRANSIT_RISE_SET_PADDING_DAYS;
    spec.target_altitude_rad = LOCAL_SOLAR_TRANSIT_ALTITUDE_HORIZON_RAD;
    spec.observed_flags = observed_flags;
    spec.coarse_step_days = 1.0 / 24.0;
    spec.root_tolerance_days = 1.0e-10;
    spec.residual_tolerance_rad = 1.0e-10;

    VisibilityAltitudeSearchResult rise;
    spec.crossing_direction = TAIYIN_VISIBILITY_CROSSING_RISING;
    Status status = visibility_search_altitude_interval_ut(context, spec, &rise, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (rise.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES
        && split_julian_date_is_finite(rise.jd_ut)
        && rise.jd_ut >= start
        && rise.jd_ut <= end) {
        out->sunrise_jd_ut = rise.jd_ut;
        out->visibility_flags |= TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER;
    } else if (rise.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
        out->visibility_flags |= TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER;
    }

    VisibilityAltitudeSearchResult set;
    spec.crossing_direction = TAIYIN_VISIBILITY_CROSSING_SETTING;
    status = visibility_search_altitude_interval_ut(context, spec, &set, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (set.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_CROSSES
        && split_julian_date_is_finite(set.jd_ut)
        && set.jd_ut >= start
        && set.jd_ut <= end) {
        out->sunset_jd_ut = set.jd_ut;
        out->visibility_flags |= TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER;
    } else if (set.altitude_state == TAIYIN_VISIBILITY_ALTITUDE_STATE_ALWAYS_ABOVE) {
        out->visibility_flags |= TAIYIN_SOLAR_TRANSIT_VISIBLE_AT_OBSERVER;
    }

    return TAIYIN_STATUS_OK;
}

}  // namespace

GreatestElongationSearchResult::GreatestElongationSearchResult() noexcept
    : jd_ut(),
      elongation_rad(0.0),
      relative_longitude_rad(0.0),
      kind(0),
      body_id(0),
      iteration_count(0),
      evaluation_count(0),
      phenomena() {}

AngularSeparationSearchResult::AngularSeparationSearchResult() noexcept
    : jd(),
      separation_rad(0.0),
      separation_rate_rad_per_day(0.0),
      body_a_id(0),
      body_b_id(0),
      iteration_count(0),
      evaluation_count(0) {}

BodyStarAngularSeparationSearchResult::BodyStarAngularSeparationSearchResult() noexcept
    : jd(),
      separation_rad(0.0),
      separation_rate_rad_per_day(0.0),
      body_id(0),
      iteration_count(0),
      evaluation_count(0) {}

SolarTransitSearchResult::SolarTransitSearchResult() noexcept
    : body_id(0),
      kind(0),
      greatest_jd_ut(invalid_jd()),
      minimum_separation_rad(NAN),
      sun_radius_rad(NAN),
      body_radius_rad(NAN),
      t1_jd_ut(invalid_jd()),
      t2_jd_ut(invalid_jd()),
      t3_jd_ut(invalid_jd()),
      t4_jd_ut(invalid_jd()),
      iteration_count(0),
      evaluation_count(0) {}

LocalSolarTransitSearchResult::LocalSolarTransitSearchResult() noexcept
    : global(),
      topocentric(),
      visibility_flags(0u),
      sunrise_jd_ut(invalid_jd()),
      sunset_jd_ut(invalid_jd()) {
    for (size_t i = 0; i < TAIYIN_SOLAR_TRANSIT_CONTACT_COUNT; ++i) {
        contact_sun_altitude_deg[i] = NAN;
        contact_sun_azimuth_deg[i] = NAN;
    }
}

double recommended_longitude_search_step_days(int body_id) noexcept {
    switch (body_id) {
    case TAIYIN_BODY_MOON:
        return 0.25;
    case TAIYIN_BODY_MERCURY:
    case TAIYIN_BODY_MERCURY_BARYCENTER:
        return 0.5;
    case TAIYIN_BODY_VENUS:
    case TAIYIN_BODY_VENUS_BARYCENTER:
    case TAIYIN_BODY_MARS:
    case TAIYIN_BODY_MARS_BARYCENTER:
        return 1.0;
    case TAIYIN_BODY_SUN:
    case TAIYIN_BODY_JUPITER_BARYCENTER:
    case TAIYIN_BODY_SATURN_BARYCENTER:
        return 2.0;
    case TAIYIN_BODY_URANUS_BARYCENTER:
    case TAIYIN_BODY_NEPTUNE_BARYCENTER:
    case TAIYIN_BODY_PLUTO_BARYCENTER:
        return 3.0;
    default:
        return 0.5;
    }
}

double recommended_aspect_search_step_days(int body_a_id, int body_b_id) noexcept {
    const double a = recommended_longitude_search_step_days(body_a_id);
    const double b = recommended_longitude_search_step_days(body_b_id);
    return a < b ? a : b;
}

Status search_solar_longitude_ut(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_direct_body_longitude_impl(
        context,
        TAIYIN_BODY_SUN,
        target_longitude_rad,
        estimate_jd_ut,
        true,
        flags,
        out_jd_ut,
        diagnostic);
}

Status search_solar_longitude_tt(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_direct_body_longitude_impl(
        context,
        TAIYIN_BODY_SUN,
        target_longitude_rad,
        estimate_jd_tt,
        false,
        flags,
        out_jd_tt,
        diagnostic);
}

Status search_moon_longitude_ut(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_direct_body_longitude_impl(
        context,
        TAIYIN_BODY_MOON,
        target_longitude_rad,
        estimate_jd_ut,
        true,
        flags,
        out_jd_ut,
        diagnostic);
}

Status search_moon_longitude_tt(
    const NativeCalcContext* context,
    double target_longitude_rad,
    SplitJulianDate estimate_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_direct_body_longitude_impl(
        context,
        TAIYIN_BODY_MOON,
        target_longitude_rad,
        estimate_jd_tt,
        false,
        flags,
        out_jd_tt,
        diagnostic);
}

Status search_body_longitude_crossings_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_crossings_impl(
        context,
        body_id,
        target_longitude_rad,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_crossings_auto_step_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_crossings_impl(
        context,
        body_id,
        target_longitude_rad,
        start_jd_ut,
        end_jd_ut,
        recommended_longitude_search_step_days(body_id),
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_crossings_tt(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_crossings_impl(
        context,
        body_id,
        target_longitude_rad,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_crossings_auto_step_tt(
    const NativeCalcContext* context,
    int body_id,
    double target_longitude_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_crossings_impl(
        context,
        body_id,
        target_longitude_rad,
        start_jd_tt,
        end_jd_tt,
        recommended_longitude_search_step_days(body_id),
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_stations_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_stations_impl(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out_jd_ut,
        out_longitude_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_stations_auto_step_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_stations_impl(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        recommended_longitude_search_step_days(body_id),
        true,
        flags,
        out_jd_ut,
        out_longitude_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_stations_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_stations_impl(
        context,
        body_id,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out_jd_tt,
        out_longitude_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_longitude_stations_auto_step_tt(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_longitude_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_longitude_stations_impl(
        context,
        body_id,
        start_jd_tt,
        end_jd_tt,
        recommended_longitude_search_step_days(body_id),
        false,
        flags,
        out_jd_tt,
        out_longitude_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_aspect_crossings_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_rad,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_aspect_crossings_auto_step_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_rad,
        start_jd_ut,
        end_jd_ut,
        recommended_aspect_search_step_days(body_a_id, body_b_id),
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_aspect_crossings_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_rad,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_aspect_crossings_auto_step_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    double aspect_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_rad,
        start_jd_tt,
        end_jd_tt,
        recommended_aspect_search_step_days(body_a_id, body_b_id),
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_exact_aspects_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_exact_aspects_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_separations_rad,
        aspect_count,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out_jd_ut,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_exact_aspects_auto_step_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_exact_aspects_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_separations_rad,
        aspect_count,
        start_jd_ut,
        end_jd_ut,
        recommended_aspect_search_step_days(body_a_id, body_b_id),
        true,
        flags,
        out_jd_ut,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_exact_aspects_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_exact_aspects_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_separations_rad,
        aspect_count,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out_jd_tt,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_body_exact_aspects_auto_step_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    const double* aspect_separations_rad,
    size_t aspect_count,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    double* out_target_aspect_rad,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_exact_aspects_impl(
        context,
        body_a_id,
        body_b_id,
        aspect_separations_rad,
        aspect_count,
        start_jd_tt,
        end_jd_tt,
        recommended_aspect_search_step_days(body_a_id, body_b_id),
        false,
        flags,
        out_jd_tt,
        out_target_aspect_rad,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_greatest_elongation_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    GreatestElongationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_greatest_elongation_impl(
        context,
        body_id,
        start_jd_ut,
        end_jd_ut,
        flags,
        out,
        diagnostic);
}

Status search_minimum_angular_separation_ut(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    AngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_minimum_angular_separation_impl(
        context,
        body_a_id,
        body_b_id,
        0,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out,
        diagnostic);
}

Status search_minimum_angular_separation_tt(
    const NativeCalcContext* context,
    int body_a_id,
    int body_b_id,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    AngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_minimum_angular_separation_impl(
        context,
        body_a_id,
        body_b_id,
        0,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out,
        diagnostic);
}

Status search_minimum_body_star_angular_separation_ut(
    const NativeCalcContext* context,
    int body_id,
    const char* star_key,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    BodyStarAngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = BodyStarAngularSeparationSearchResult();
    }
    if (!out || !star_key || star_key[0] == '\0') {
        if (diagnostic) {
            *diagnostic = EphemerisEvalDiagnostic();
            set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    AngularSeparationSearchResult result;
    const Status status = search_minimum_angular_separation_impl(
        context, body_id, 0, star_key, start_jd_ut, end_jd_ut,
        max_step_days, true, flags, &result, diagnostic);
    if (status == TAIYIN_STATUS_OK) {
        out->jd = result.jd;
        out->separation_rad = result.separation_rad;
        out->separation_rate_rad_per_day = result.separation_rate_rad_per_day;
        out->body_id = body_id;
        out->iteration_count = result.iteration_count;
        out->evaluation_count = result.evaluation_count;
    }
    return status;
}

Status search_minimum_body_star_angular_separation_tt(
    const NativeCalcContext* context,
    int body_id,
    const char* star_key,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    BodyStarAngularSeparationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = BodyStarAngularSeparationSearchResult();
    }
    if (!out || !star_key || star_key[0] == '\0') {
        if (diagnostic) {
            *diagnostic = EphemerisEvalDiagnostic();
            set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_tt);
        }
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    AngularSeparationSearchResult result;
    const Status status = search_minimum_angular_separation_impl(
        context, body_id, 0, star_key, start_jd_tt, end_jd_tt,
        max_step_days, false, flags, &result, diagnostic);
    if (status == TAIYIN_STATUS_OK) {
        out->jd = result.jd;
        out->separation_rad = result.separation_rad;
        out->separation_rate_rad_per_day = result.separation_rate_rad_per_day;
        out->body_id = body_id;
        out->iteration_count = result.iteration_count;
        out->evaluation_count = result.evaluation_count;
    }
    return status;
}

Status search_next_solar_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    uint64_t flags,
    SolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_next_solar_transit_impl(
        context,
        body_id,
        jd_start_ut,
        flags,
        out,
        diagnostic);
}

Status compute_local_solar_transit_ut(
    const NativeCalcContext* context,
    const SolarTransitSearchResult* global_transit,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = LocalSolarTransitSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !global_transit
        || !out
        || !is_inner_planet_transit_body(global_transit->body_id)
        || !valid_geodetic_degrees(longitude_deg, latitude_deg, height_m)
        || !split_julian_date_is_finite(global_transit->greatest_jd_ut)) {
        set_basic_diagnostic(
            diagnostic,
            TAIYIN_ERROR_INVALID_ARGUMENT,
            global_transit ? global_transit->body_id : 0,
            global_transit ? global_transit->greatest_jd_ut : invalid_jd());
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    const uint64_t known_search_flags =
        TAIYIN_EVENT_SEARCH_REFRACTION
        | TAIYIN_EVENT_SEARCH_NO_REFRACTION;
    uint64_t observed_flags = 0u;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ
                           | TAIYIN_NATIVE_POSITION_EQUATORIAL
                           | TAIYIN_NATIVE_POSITION_TOPOCENTRIC)) != 0u
        || (search_flags & ~known_search_flags) != 0u
        || !resolve_local_event_refraction_flags(search_flags, &observed_flags)
        || native_context_has_topocentric_observer(*context)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, global_transit->body_id, global_transit->greatest_jd_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    const NativeObserverLocation location =
        native_observer_location_degrees(longitude_deg, latitude_deg, height_m);
    NativeCalcContext local_visibility_context =
        local_transit_context_from_observer(*context, location);

    out->global = *global_transit;
    Status status = compute_topocentric_solar_transit(
        context,
        *global_transit,
        location,
        position_flags,
        &out->topocentric,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    status = sample_local_solar_transit_visibility(
        &local_visibility_context,
        out->topocentric,
        observed_flags,
        out,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    return TAIYIN_STATUS_OK;
}

Status search_local_solar_transit_interval_impl(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = LocalSolarTransitSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out
        || !valid_geodetic_degrees(longitude_deg, latitude_deg, height_m)
        || !split_julian_date_is_finite(start_jd_ut)
        || !split_julian_date_is_finite(end_jd_ut)
        || !(end_jd_ut > start_jd_ut)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    const uint64_t known_search_flags =
        TAIYIN_EVENT_SEARCH_REFRACTION
        | TAIYIN_EVENT_SEARCH_NO_REFRACTION;
    uint64_t observed_flags = 0u;
    if ((search_flags & ~known_search_flags) != 0u
        || (position_flags & (TAIYIN_NATIVE_POSITION_XYZ
                              | TAIYIN_NATIVE_POSITION_EQUATORIAL
                              | TAIYIN_NATIVE_POSITION_TOPOCENTRIC)) != 0u
        || !resolve_local_event_refraction_flags(search_flags, &observed_flags)
        || native_context_has_topocentric_observer(*context)
        || !is_inner_planet_transit_body(body_id)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, start_jd_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    (void)observed_flags;

    const double conjunction_step_days = recommended_solar_transit_conjunction_step_days(body_id);
    const double candidate_half_window_days = solar_transit_candidate_half_window_days(body_id);
    const SplitJulianDate candidate_search_start = start_jd_ut - candidate_half_window_days;
    const SplitJulianDate candidate_search_end = end_jd_ut + candidate_half_window_days;
    const double candidate_search_span_days = candidate_search_end - candidate_search_start;
    const double rough_capacity = std::ceil(candidate_search_span_days / 40.0) + 16.0;
    if (!std::isfinite(rough_capacity)
        || rough_capacity <= 0.0
        || rough_capacity > 20000.0) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, start_jd_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    std::vector<SplitJulianDate> conjunctions;
    try {
        conjunctions.assign(static_cast<size_t>(rough_capacity), invalid_jd());
    } catch (...) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_OUT_OF_MEMORY, body_id, start_jd_ut);
        return TAIYIN_ERROR_OUT_OF_MEMORY;
    }
    size_t conjunction_count = 0;
    Status status = search_body_aspect_crossings_ut(
        context,
        ephemeris_elongation_body(body_id),
        TAIYIN_BODY_SUN,
        0.0,
        candidate_search_start,
        candidate_search_end,
        conjunction_step_days,
        position_flags,
        conjunctions.data(),
        conjunctions.size(),
        &conjunction_count,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) {
        return normalize_event_status(status);
    }

    const NativeObserverLocation location =
        native_observer_location_degrees(longitude_deg, latitude_deg, height_m);
    for (size_t i = 0; i < conjunction_count; ++i) {
        const SplitJulianDate conjunction_jd = conjunctions[i];
        bool inferior_conjunction = false;
        status = body_is_inferior_conjunction_candidate(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &inferior_conjunction,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        if (!inferior_conjunction) continue;
        bool node_gate_passes = false;
        status = solar_transit_node_gate_passes(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &node_gate_passes,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return normalize_event_status(status);
        if (!node_gate_passes) continue;

        SolarTransitSearchResult geocentric_candidate;
        status = compute_geocentric_solar_transit_candidate(
            context,
            body_id,
            conjunction_jd,
            position_flags,
            &geocentric_candidate,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            return normalize_event_status(status);
        }

        SolarTransitSearchResult topocentric;
        status = compute_topocentric_solar_transit(
            context,
            geocentric_candidate,
            location,
            position_flags,
            &topocentric,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            return normalize_event_status(status);
        }

        const bool in_requested_interval =
            topocentric.greatest_jd_ut >= start_jd_ut && topocentric.greatest_jd_ut <= end_jd_ut;
        if (!in_requested_interval) continue;

        LocalSolarTransitSearchResult candidate;
        candidate.global = geocentric_candidate;
        candidate.topocentric = topocentric;
        NativeCalcContext local_visibility_context =
            local_transit_context_from_observer(*context, location);
        status = sample_local_solar_transit_visibility(
            &local_visibility_context,
            candidate.topocentric,
            observed_flags,
            &candidate,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) return status;
        *out = candidate;
        return TAIYIN_STATUS_OK;
    }

    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_next_local_solar_transit_ut(
    const NativeCalcContext* context,
    int body_id,
    SplitJulianDate jd_start_ut,
    double longitude_deg,
    double latitude_deg,
    double height_m,
    uint64_t flags,
    LocalSolarTransitSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (out) {
        *out = LocalSolarTransitSearchResult();
    }
    if (diagnostic) {
        *diagnostic = EphemerisEvalDiagnostic();
    }
    if (!context
        || !out
        || !is_inner_planet_transit_body(body_id)
        || !valid_geodetic_degrees(longitude_deg, latitude_deg, height_m)
        || !split_julian_date_is_finite(jd_start_ut)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_INVALID_ARGUMENT, body_id, jd_start_ut);
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t position_flags = static_cast<uint32_t>(flags & TAIYIN_EVENT_SEARCH_POSITION_FLAGS_MASK);
    const uint64_t search_flags = flags & TAIYIN_EVENT_SEARCH_OPTION_FLAGS_MASK;
    const uint64_t known_search_flags =
        TAIYIN_EVENT_SEARCH_REVERSE
        | TAIYIN_EVENT_SEARCH_REFRACTION
        | TAIYIN_EVENT_SEARCH_NO_REFRACTION;
    uint64_t observed_flags = 0u;
    if ((position_flags & (TAIYIN_NATIVE_POSITION_XYZ
                           | TAIYIN_NATIVE_POSITION_EQUATORIAL
                           | TAIYIN_NATIVE_POSITION_TOPOCENTRIC)) != 0u
        || (search_flags & ~known_search_flags) != 0u
        || !resolve_local_event_refraction_flags(search_flags, &observed_flags)
        || native_context_has_topocentric_observer(*context)) {
        set_basic_diagnostic(diagnostic, TAIYIN_ERROR_UNSUPPORTED, body_id, jd_start_ut);
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    (void)observed_flags;

    const bool reverse = (search_flags & TAIYIN_EVENT_SEARCH_REVERSE) != 0u;
    const int64_t direction = reverse ? -1 : 1;
    const uint64_t interval_flags =
        static_cast<uint64_t>(position_flags)
        | (search_flags & (TAIYIN_EVENT_SEARCH_REFRACTION | TAIYIN_EVENT_SEARCH_NO_REFRACTION));
    int64_t k = solar_transit_k_for_jd(body_id, jd_start_ut, reverse);
    for (int i = 0; i < MAX_SOLAR_TRANSIT_CANDIDATE_CYCLES; ++i, k += direction) {
        SplitJulianDate conjunction_jd = invalid_jd();
        Status status = solve_solar_transit_inferior_conjunction_for_k(
            context,
            body_id,
            k,
            position_flags,
            &conjunction_jd,
            diagnostic);
        if (status != TAIYIN_STATUS_OK) {
            if (status == TAIYIN_EVENT_ERROR_NOT_FOUND) continue;
            return status;
        }
        const double half_window = solar_transit_candidate_half_window_days(body_id);
        const SplitJulianDate start = conjunction_jd - half_window;
        const SplitJulianDate end = conjunction_jd + half_window;
        LocalSolarTransitSearchResult candidate;
        status = search_local_solar_transit_interval_impl(
            context,
            body_id,
            start,
            end,
            longitude_deg,
            latitude_deg,
            height_m,
            interval_flags,
            &candidate,
            diagnostic);
        if (status == TAIYIN_STATUS_OK) {
            if (reverse) {
                if (candidate.topocentric.greatest_jd_ut >= jd_start_ut - SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
                    continue;
                }
            } else if (candidate.topocentric.greatest_jd_ut <= jd_start_ut + SOLAR_TRANSIT_BOUNDARY_MINIMUM_TOLERANCE_DAYS) {
                continue;
            }
            *out = candidate;
            return TAIYIN_STATUS_OK;
        }
        if (status != TAIYIN_EVENT_ERROR_NOT_FOUND) {
            return status;
        }
    }

    set_basic_diagnostic(diagnostic, TAIYIN_EVENT_ERROR_NOT_FOUND, body_id, jd_start_ut);
    return TAIYIN_EVENT_ERROR_NOT_FOUND;
}

Status search_lunar_phase_crossings_ut(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        phase_rad,
        start_jd_ut,
        end_jd_ut,
        max_step_days,
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_lunar_phase_crossings_default_step_ut(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_ut,
    SplitJulianDate end_jd_ut,
    uint64_t flags,
    SplitJulianDate* out_jd_ut,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        phase_rad,
        start_jd_ut,
        end_jd_ut,
        DEFAULT_LUNAR_PHASE_STEP_DAYS,
        true,
        flags,
        out_jd_ut,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_lunar_phase_crossings_tt(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    double max_step_days,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        phase_rad,
        start_jd_tt,
        end_jd_tt,
        max_step_days,
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

Status search_lunar_phase_crossings_default_step_tt(
    const NativeCalcContext* context,
    double phase_rad,
    SplitJulianDate start_jd_tt,
    SplitJulianDate end_jd_tt,
    uint64_t flags,
    SplitJulianDate* out_jd_tt,
    size_t max_event_count,
    size_t* out_event_count,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    return search_body_aspect_crossings_impl(
        context,
        TAIYIN_BODY_MOON,
        TAIYIN_BODY_SUN,
        phase_rad,
        start_jd_tt,
        end_jd_tt,
        DEFAULT_LUNAR_PHASE_STEP_DAYS,
        false,
        flags,
        out_jd_tt,
        max_event_count,
        out_event_count,
        diagnostic);
}

}  // namespace runtime
}  // namespace taiyin
