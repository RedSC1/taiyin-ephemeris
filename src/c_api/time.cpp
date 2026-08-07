#include "taiyin/c/time.h"

#include "c_api_internal.h"
#include "taiyin/runtime/runtime.h"

#include <cmath>
#include <cstring>

namespace {

bool valid_tdb_model(int32_t model_id) noexcept {
    return model_id == TAIYIN_TDB_MODEL_FAST_PERIODIC
        || model_id == TAIYIN_TDB_MODEL_SOFA_FULL;
}

taiyin::TdbModel cpp_tdb_model(int32_t model_id) noexcept {
    return model_id == TAIYIN_TDB_MODEL_SOFA_FULL
        ? taiyin::TdbModel::SofaFull
        : taiyin::TdbModel::FastPeriodic;
}

taiyin::SplitJulianDate to_cpp_split(
    const taiyin_split_julian_date& value
) noexcept {
    return taiyin_c_internal::to_cpp_split_jd(value);
}

void from_cpp_split(
    const taiyin::SplitJulianDate& value,
    taiyin_split_julian_date* out
) noexcept {
    out->day_number = value.day_number;
    out->day_fraction = value.day_fraction;
}

bool build_split_precise_time_scales(
    const taiyin::CalendarDateTime& datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    taiyin::TdbModel tdb_model,
    taiyin_split_precise_time_scales* out
) noexcept {
    taiyin::SplitJulianDate utc;
    taiyin::SplitJulianDate tai;
    taiyin::SplitJulianDate tt;
    taiyin::SplitJulianDate ut1;
    taiyin::SplitJulianDate tdb;
    if (!taiyin::julian_day_split(datetime_utc, &utc)
        || !taiyin::utc_to_tai_split_jd(
            utc, tai_minus_utc_seconds, &tai)
        || !taiyin::tai_to_tt_split_jd(tai, &tt)
        || !taiyin::utc_to_ut1_split_jd(utc, dut1_seconds, &ut1)
        || !taiyin::tt_to_tdb_split_jd(tt, tdb_model, &tdb)) {
        return false;
    }
    taiyin_split_precise_time_scales result;
    std::memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    from_cpp_split(utc, &result.utc);
    from_cpp_split(tai, &result.tai);
    from_cpp_split(tt, &result.tt);
    from_cpp_split(ut1, &result.ut1);
    from_cpp_split(tdb, &result.tdb);
    result.tai_minus_utc_seconds = tai_minus_utc_seconds;
    result.dut1_seconds = dut1_seconds;
    result.delta_t_seconds =
        taiyin::delta_t_from_tai_minus_utc_and_dut1(
            tai_minus_utc_seconds, dut1_seconds);
    *out = result;
    return true;
}

bool build_split_estimated_time_scales(
    const taiyin::CalendarDateTime& datetime_ut,
    double delta_t_seconds,
    taiyin::TdbModel tdb_model,
    taiyin_split_estimated_time_scales* out
) noexcept {
    taiyin::SplitJulianDate ut1;
    taiyin::SplitJulianDate tt;
    taiyin::SplitJulianDate tdb;
    if (!taiyin::julian_day_split(datetime_ut, &ut1)
        || !taiyin::ut1_to_tt_split_jd(ut1, delta_t_seconds, &tt)
        || !taiyin::tt_to_tdb_split_jd(tt, tdb_model, &tdb)) {
        return false;
    }
    taiyin_split_estimated_time_scales result;
    std::memset(&result, 0, sizeof(result));
    result.struct_size = sizeof(result);
    from_cpp_split(ut1, &result.ut1);
    from_cpp_split(tt, &result.tt);
    from_cpp_split(tdb, &result.tdb);
    result.delta_t_seconds = delta_t_seconds;
    *out = result;
    return true;
}

void copy_precise_time_scales(
    const taiyin::PreciseTimeScales& value,
    taiyin_precise_time_scales* out
) noexcept {
    out->jd_utc = taiyin::split_julian_date_to_double(value.jd_utc);
    out->jd_tai = taiyin::split_julian_date_to_double(value.jd_tai);
    out->jd_tt = taiyin::split_julian_date_to_double(value.jd_tt);
    out->jd_ut1 = taiyin::split_julian_date_to_double(value.jd_ut1);
    out->jd_tdb = taiyin::split_julian_date_to_double(value.jd_tdb);
    out->tai_minus_utc_seconds = value.tai_minus_utc_seconds;
    out->dut1_seconds = value.dut1_seconds;
    out->delta_t_seconds = value.delta_t_seconds;
}

void copy_estimated_time_scales(
    const taiyin::EstimatedTimeScales& value,
    taiyin_estimated_time_scales* out
) noexcept {
    out->jd_ut1 = taiyin::split_julian_date_to_double(value.jd_ut1);
    out->jd_tt = taiyin::split_julian_date_to_double(value.jd_tt);
    out->jd_tdb = taiyin::split_julian_date_to_double(value.jd_tdb);
    out->delta_t_seconds = value.delta_t_seconds;
}

void copy_time_diagnostic(
    const taiyin::TimeScaleDiagnostic& value,
    taiyin_time_scale_diagnostic* out
) noexcept {
    if (!out) return;
    out->route = static_cast<int32_t>(value.route);
    out->fallback_reason = static_cast<int32_t>(value.fallback_reason);
    out->flags = (value.used_leap_seconds ? TAIYIN_TIME_USED_LEAP_SECONDS : 0u)
        | (value.used_eop ? TAIYIN_TIME_USED_EOP : 0u)
        | (value.used_delta_t_model ? TAIYIN_TIME_USED_DELTA_T_MODEL : 0u);
    out->tdb_model_id = value.tdb_model_id;
    out->delta_t_model_id = value.delta_t_model_id;
    out->ephemeris_family_id = value.ephemeris_family_id;
    out->tai_minus_utc_seconds = value.tai_minus_utc_seconds;
    out->dut1_seconds = value.dut1_seconds;
    out->delta_t_seconds = value.delta_t_seconds;
}

taiyin_status time_failure_status(
    const taiyin::TimeScaleDiagnostic& diagnostic
) noexcept {
    if (diagnostic.fallback_reason
        == taiyin::TimeScaleFallbackLeapSecondUnavailable) {
        return taiyin::TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE;
    }
    if (diagnostic.fallback_reason == taiyin::TimeScaleFallbackNullEopTable
        || diagnostic.fallback_reason
            == taiyin::TimeScaleFallbackEopOutOfRange) {
        return taiyin::TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE;
    }
    return taiyin::TAIYIN_ERROR_INTERNAL;
}

}  // namespace

extern "C" {

void TAIYIN_C_CALL taiyin_precise_time_scales_init(
    taiyin_precise_time_scales* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_split_precise_time_scales_init(
    taiyin_split_precise_time_scales* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_time_scale_diagnostic_init(
    taiyin_time_scale_diagnostic* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_estimated_time_scales_init(
    taiyin_estimated_time_scales* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

void TAIYIN_C_CALL taiyin_split_estimated_time_scales_init(
    taiyin_split_estimated_time_scales* value
) {
    if (!value) return;
    std::memset(value, 0, sizeof(*value));
    value->struct_size = sizeof(*value);
}

taiyin_status TAIYIN_C_CALL taiyin_split_julian_date_from_parts(
    int64_t day_number,
    double day_fraction,
    taiyin_split_julian_date* out
) {
    taiyin::SplitJulianDate value;
    if (!out
        || !taiyin::normalize_split_julian_date(
            day_number, day_fraction, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_split_julian_date_from_double(
    double jd,
    taiyin_split_julian_date* out
) {
    taiyin::SplitJulianDate value;
    if (!out || !taiyin::split_julian_date_from_double(jd, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_split_julian_date_to_double(
    const taiyin_split_julian_date* jd,
    double* out_jd
) {
    if (!taiyin_c_internal::valid_split_jd(jd) || !out_jd) {
        return taiyin_c_internal::invalid_argument();
    }
    const double value =
        taiyin::split_julian_date_to_double(to_cpp_split(*jd));
    if (!std::isfinite(value)) {
        return taiyin_c_internal::invalid_argument();
    }
    *out_jd = value;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_julian_day(
    const taiyin_calendar_datetime* datetime,
    double* out_jd
) {
    if (!taiyin_c_internal::valid_struct(datetime) || !out_jd) {
        return taiyin_c_internal::invalid_argument();
    }
    const double jd = taiyin::julian_day(taiyin_c_internal::to_cpp_datetime(*datetime));
    if (!std::isfinite(jd)) return taiyin_c_internal::invalid_argument();
    *out_jd = jd;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_julian_day_split(
    const taiyin_calendar_datetime* datetime,
    taiyin_split_julian_date* out_jd
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_struct(datetime)
        || !out_jd
        || !taiyin::julian_day_split(
            taiyin_c_internal::to_cpp_datetime(*datetime), &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_reverse_julian_day(
    double jd,
    taiyin_calendar_datetime* out_datetime
) {
    if (!std::isfinite(jd) || !taiyin_c_internal::valid_struct(out_datetime)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin_c_internal::from_cpp_datetime(
        taiyin::reverse_julian_day(jd), out_datetime);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_reverse_julian_day_split(
    const taiyin_split_julian_date* jd,
    taiyin_calendar_datetime* out_datetime
) {
    taiyin::CalendarDateTime value;
    if (!taiyin_c_internal::valid_split_jd(jd)
        || !taiyin_c_internal::valid_struct(out_datetime)
        || !taiyin::reverse_julian_day_split(to_cpp_split(*jd), &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin_c_internal::from_cpp_datetime(value, out_datetime);
    return taiyin::TAIYIN_STATUS_OK;
}

double TAIYIN_C_CALL taiyin_decimal_year_from_jd(double jd) {
    return taiyin::decimal_year_from_jd(jd);
}

double TAIYIN_C_CALL taiyin_julian_centuries_from_j2000(double jd) {
    return taiyin::julian_centuries_from_j2000(jd);
}

double TAIYIN_C_CALL taiyin_julian_millennia_from_j2000(double jd) {
    return taiyin::julian_millennia_from_j2000(jd);
}

double TAIYIN_C_CALL taiyin_add_seconds_to_jd(double jd, double seconds) {
    return taiyin::add_seconds_to_jd(jd, seconds);
}

double TAIYIN_C_CALL taiyin_seconds_between_jd(double jd_a, double jd_b) {
    return taiyin::seconds_between_jd(jd_a, jd_b);
}

taiyin_status TAIYIN_C_CALL taiyin_add_seconds_to_split_jd(
    const taiyin_split_julian_date* jd,
    double seconds,
    taiyin_split_julian_date* out
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd) || !out
        || !taiyin::add_seconds_to_split_jd(
            to_cpp_split(*jd), seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_seconds_between_split_jd(
    const taiyin_split_julian_date* jd_a,
    const taiyin_split_julian_date* jd_b,
    double* out_seconds
) {
    if (!taiyin_c_internal::valid_split_jd(jd_a)
        || !taiyin_c_internal::valid_split_jd(jd_b) || !out_seconds) {
        return taiyin_c_internal::invalid_argument();
    }
    const double value = taiyin::seconds_between_split_jd(
        to_cpp_split(*jd_a), to_cpp_split(*jd_b));
    if (!std::isfinite(value)) {
        return taiyin_c_internal::invalid_argument();
    }
    *out_seconds = value;
    return taiyin::TAIYIN_STATUS_OK;
}

double TAIYIN_C_CALL taiyin_estimated_delta_t_seconds_for_decimal_year(
    double decimal_year
) {
    return taiyin::estimated_delta_t_seconds_for_decimal_year(decimal_year);
}

double TAIYIN_C_CALL taiyin_estimated_delta_t_seconds_from_ut1(double jd_ut1) {
    return taiyin::estimated_delta_t_seconds_from_ut1_jd(jd_ut1);
}

double TAIYIN_C_CALL taiyin_estimated_delta_t_seconds_from_tt(double jd_tt) {
    return taiyin::estimated_delta_t_seconds_from_tt_jd(jd_tt);
}

double TAIYIN_C_CALL taiyin_tt_to_tdb(double jd_tt, int32_t tdb_model_id) {
    if (!valid_tdb_model(tdb_model_id)) return NAN;
    return taiyin::tt_to_tdb_jd(
        jd_tt, static_cast<taiyin::TdbModel>(tdb_model_id));
}

double TAIYIN_C_CALL taiyin_tdb_to_tt(double jd_tdb, int32_t tdb_model_id) {
    if (!valid_tdb_model(tdb_model_id)) return NAN;
    return taiyin::tdb_to_tt_jd(
        jd_tdb, static_cast<taiyin::TdbModel>(tdb_model_id));
}

taiyin_status TAIYIN_C_CALL taiyin_tai_minus_utc_seconds(
    const taiyin_calendar_datetime* datetime_utc,
    double* out_seconds
) {
    if (!taiyin_c_internal::valid_struct(datetime_utc) || !out_seconds) {
        return taiyin_c_internal::invalid_argument();
    }
    double value = 0.0;
    if (!taiyin::tai_minus_utc_seconds_from_utc(
            taiyin_c_internal::to_cpp_datetime(*datetime_utc), &value)) {
        return taiyin::TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE;
    }
    *out_seconds = value;
    return taiyin::TAIYIN_STATUS_OK;
}

double TAIYIN_C_CALL taiyin_utc_to_tai(
    double jd_utc,
    double tai_minus_utc_seconds
) {
    return taiyin::utc_to_tai_jd(jd_utc, tai_minus_utc_seconds);
}

double TAIYIN_C_CALL taiyin_tai_to_tt(double jd_tai) {
    return taiyin::tai_to_tt_jd(jd_tai);
}

double TAIYIN_C_CALL taiyin_utc_to_tt(
    double jd_utc,
    double tai_minus_utc_seconds
) {
    return taiyin::utc_to_tt_jd(jd_utc, tai_minus_utc_seconds);
}

double TAIYIN_C_CALL taiyin_utc_to_ut1(
    double jd_utc,
    double dut1_seconds
) {
    return taiyin::utc_to_ut1_jd(jd_utc, dut1_seconds);
}

double TAIYIN_C_CALL taiyin_delta_t_from_tai_minus_utc_and_dut1(
    double tai_minus_utc_seconds,
    double dut1_seconds
) {
    return taiyin::delta_t_from_tai_minus_utc_and_dut1(
        tai_minus_utc_seconds, dut1_seconds);
}

double TAIYIN_C_CALL taiyin_tt_to_ut1(
    double jd_tt,
    double delta_t_seconds
) {
    return taiyin::tt_to_ut1_jd(jd_tt, delta_t_seconds);
}

double TAIYIN_C_CALL taiyin_ut1_to_tt(
    double jd_ut1,
    double delta_t_seconds
) {
    return taiyin::ut1_to_tt_jd(jd_ut1, delta_t_seconds);
}

taiyin_status TAIYIN_C_CALL taiyin_utc_to_tai_split(
    const taiyin_split_julian_date* jd_utc,
    double tai_minus_utc_seconds,
    taiyin_split_julian_date* out_jd_tai
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_utc) || !out_jd_tai
        || !taiyin::utc_to_tai_split_jd(
            to_cpp_split(*jd_utc), tai_minus_utc_seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tai);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_tai_to_tt_split(
    const taiyin_split_julian_date* jd_tai,
    taiyin_split_julian_date* out_jd_tt
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_tai) || !out_jd_tt
        || !taiyin::tai_to_tt_split_jd(
            to_cpp_split(*jd_tai), &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tt);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_utc_to_tt_split(
    const taiyin_split_julian_date* jd_utc,
    double tai_minus_utc_seconds,
    taiyin_split_julian_date* out_jd_tt
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_utc) || !out_jd_tt
        || !taiyin::utc_to_tt_split_jd(
            to_cpp_split(*jd_utc), tai_minus_utc_seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tt);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_utc_to_ut1_split(
    const taiyin_split_julian_date* jd_utc,
    double dut1_seconds,
    taiyin_split_julian_date* out_jd_ut1
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_utc) || !out_jd_ut1
        || !taiyin::utc_to_ut1_split_jd(
            to_cpp_split(*jd_utc), dut1_seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_ut1);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_tt_to_ut1_split(
    const taiyin_split_julian_date* jd_tt,
    double delta_t_seconds,
    taiyin_split_julian_date* out_jd_ut1
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_tt) || !out_jd_ut1
        || !taiyin::tt_to_ut1_split_jd(
            to_cpp_split(*jd_tt), delta_t_seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_ut1);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_ut1_to_tt_split(
    const taiyin_split_julian_date* jd_ut1,
    double delta_t_seconds,
    taiyin_split_julian_date* out_jd_tt
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_ut1) || !out_jd_tt
        || !taiyin::ut1_to_tt_split_jd(
            to_cpp_split(*jd_ut1), delta_t_seconds, &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tt);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_tt_to_tdb_split(
    const taiyin_split_julian_date* jd_tt,
    int32_t tdb_model_id,
    taiyin_split_julian_date* out_jd_tdb
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_tt)
        || !out_jd_tdb || !valid_tdb_model(tdb_model_id)
        || !taiyin::tt_to_tdb_split_jd(
            to_cpp_split(*jd_tt), cpp_tdb_model(tdb_model_id), &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tdb);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_tdb_to_tt_split(
    const taiyin_split_julian_date* jd_tdb,
    int32_t tdb_model_id,
    taiyin_split_julian_date* out_jd_tt
) {
    taiyin::SplitJulianDate value;
    if (!taiyin_c_internal::valid_split_jd(jd_tdb)
        || !out_jd_tt || !valid_tdb_model(tdb_model_id)
        || !taiyin::tdb_to_tt_split_jd(
            to_cpp_split(*jd_tdb), cpp_tdb_model(tdb_model_id), &value)) {
        return taiyin_c_internal::invalid_argument();
    }
    from_cpp_split(value, out_jd_tt);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_make_precise_time_scales_from_utc(
    const taiyin_calendar_datetime* datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    int32_t tdb_model_id,
    taiyin_precise_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_utc)
        || !taiyin_c_internal::valid_struct(out)
        || !std::isfinite(tai_minus_utc_seconds)
        || !std::isfinite(dut1_seconds)
        || !valid_tdb_model(tdb_model_id)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::PreciseTimeScales value =
        taiyin::make_precise_time_scales_from_utc(
            taiyin_c_internal::to_cpp_datetime(*datetime_utc),
            tai_minus_utc_seconds,
            dut1_seconds,
            static_cast<taiyin::TdbModel>(tdb_model_id));
    copy_precise_time_scales(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL
taiyin_make_split_precise_time_scales_from_utc(
    const taiyin_calendar_datetime* datetime_utc,
    double tai_minus_utc_seconds,
    double dut1_seconds,
    int32_t tdb_model_id,
    taiyin_split_precise_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_utc)
        || !taiyin_c_internal::valid_struct(out)
        || !std::isfinite(tai_minus_utc_seconds)
        || !std::isfinite(dut1_seconds)
        || !valid_tdb_model(tdb_model_id)
        || !build_split_precise_time_scales(
            taiyin_c_internal::to_cpp_datetime(*datetime_utc),
            tai_minus_utc_seconds,
            dut1_seconds,
            cpp_tdb_model(tdb_model_id),
            out)) {
        return taiyin_c_internal::invalid_argument();
    }
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_make_time_scales_from_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    taiyin_precise_time_scales* out,
    taiyin_time_scale_diagnostic* diagnostic
) {
    if (!context
        || !taiyin_c_internal::valid_struct(datetime_utc)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::TimeScaleOptions options = taiyin::default_time_scale_options();
    options.policy = context->value.time_scale_policy;
    options.tdb_model_id = context->value.model_context.tdb_model_id;
    options.delta_t_model_id = context->value.delta_t_model_id;
    options.ephemeris_family_id = context->value.ephemeris_family_id;
    taiyin::PreciseTimeScales value;
    taiyin::TimeScaleDiagnostic cpp_diagnostic;
    const bool ok = taiyin::make_time_scales_from_utc(
        taiyin_c_internal::to_cpp_datetime(*datetime_utc),
        taiyin::runtime::global_earth_orientation_table(),
        &options,
        &value,
        &cpp_diagnostic);
    copy_time_diagnostic(cpp_diagnostic, diagnostic);
    if (!ok) return time_failure_status(cpp_diagnostic);
    copy_precise_time_scales(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_make_split_time_scales_from_utc(
    const taiyin_context* context,
    const taiyin_calendar_datetime* datetime_utc,
    taiyin_split_precise_time_scales* out,
    taiyin_time_scale_diagnostic* diagnostic
) {
    if (!context
        || !taiyin_c_internal::valid_struct(datetime_utc)
        || !taiyin_c_internal::valid_struct(out)
        || (diagnostic && !taiyin_c_internal::valid_struct(diagnostic))) {
        return taiyin_c_internal::invalid_argument();
    }
    taiyin::TimeScaleOptions options = taiyin::default_time_scale_options();
    options.policy = context->value.time_scale_policy;
    options.tdb_model_id = context->value.model_context.tdb_model_id;
    options.delta_t_model_id = context->value.delta_t_model_id;
    options.ephemeris_family_id = context->value.ephemeris_family_id;
    taiyin::PreciseTimeScales value;
    taiyin::TimeScaleDiagnostic cpp_diagnostic;
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_utc);
    const bool ok = taiyin::make_time_scales_from_utc(
        cpp_datetime,
        taiyin::runtime::global_earth_orientation_table(),
        &options,
        &value,
        &cpp_diagnostic);
    copy_time_diagnostic(cpp_diagnostic, diagnostic);
    if (!ok) return time_failure_status(cpp_diagnostic);

    if (cpp_diagnostic.route
        == taiyin::TimeScaleRoute::TimeScaleRoutePreciseUtcEop) {
        if (!build_split_precise_time_scales(
                cpp_datetime,
                value.tai_minus_utc_seconds,
                value.dut1_seconds,
                cpp_tdb_model(options.tdb_model_id),
                out)) {
            return taiyin::TAIYIN_ERROR_INTERNAL;
        }
        return taiyin::TAIYIN_STATUS_OK;
    }

    taiyin::SplitJulianDate utc;
    taiyin::SplitJulianDate tt;
    taiyin::SplitJulianDate tdb;
    if (!taiyin::julian_day_split(cpp_datetime, &utc)
        || !taiyin::ut1_to_tt_split_jd(
            utc, value.delta_t_seconds, &tt)
        || !taiyin::tt_to_tdb_split_jd(
            tt, cpp_tdb_model(options.tdb_model_id), &tdb)) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
    taiyin_split_precise_time_scales result;
    taiyin_split_precise_time_scales_init(&result);
    from_cpp_split(utc, &result.utc);
    from_cpp_split(tt, &result.tt);
    from_cpp_split(utc, &result.ut1);
    from_cpp_split(tdb, &result.tdb);
    result.delta_t_seconds = value.delta_t_seconds;
    *out = result;
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_make_time_scales_from_ut_delta_t(
    const taiyin_calendar_datetime* datetime_ut,
    double delta_t_seconds,
    int32_t tdb_model_id,
    taiyin_estimated_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !std::isfinite(delta_t_seconds)
        || !valid_tdb_model(tdb_model_id)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::EstimatedTimeScales value =
        taiyin::make_time_scales_from_ut_delta_t(
            taiyin_c_internal::to_cpp_datetime(*datetime_ut),
            delta_t_seconds,
            static_cast<taiyin::TdbModel>(tdb_model_id));
    copy_estimated_time_scales(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL
taiyin_make_split_time_scales_from_ut_delta_t(
    const taiyin_calendar_datetime* datetime_ut,
    double delta_t_seconds,
    int32_t tdb_model_id,
    taiyin_split_estimated_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !std::isfinite(delta_t_seconds)
        || !valid_tdb_model(tdb_model_id)
        || !build_split_estimated_time_scales(
            taiyin_c_internal::to_cpp_datetime(*datetime_ut),
            delta_t_seconds,
            cpp_tdb_model(tdb_model_id),
            out)) {
        return taiyin_c_internal::invalid_argument();
    }
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL taiyin_make_estimated_time_scales_from_ut(
    const taiyin_calendar_datetime* datetime_ut,
    int32_t tdb_model_id,
    taiyin_estimated_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_tdb_model(tdb_model_id)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::EstimatedTimeScales value =
        taiyin::make_estimated_time_scales_from_ut(
            taiyin_c_internal::to_cpp_datetime(*datetime_ut),
            static_cast<taiyin::TdbModel>(tdb_model_id));
    copy_estimated_time_scales(value, out);
    return taiyin::TAIYIN_STATUS_OK;
}

taiyin_status TAIYIN_C_CALL
taiyin_make_split_estimated_time_scales_from_ut(
    const taiyin_calendar_datetime* datetime_ut,
    int32_t tdb_model_id,
    taiyin_split_estimated_time_scales* out
) {
    if (!taiyin_c_internal::valid_struct(datetime_ut)
        || !taiyin_c_internal::valid_struct(out)
        || !valid_tdb_model(tdb_model_id)) {
        return taiyin_c_internal::invalid_argument();
    }
    const taiyin::CalendarDateTime cpp_datetime =
        taiyin_c_internal::to_cpp_datetime(*datetime_ut);
    const double delta_t_seconds =
        taiyin::estimated_delta_t_seconds_from_ut1_jd(
            taiyin::julian_day(cpp_datetime));
    if (!std::isfinite(delta_t_seconds)
        || !build_split_estimated_time_scales(
            cpp_datetime,
            delta_t_seconds,
            cpp_tdb_model(tdb_model_id),
            out)) {
        return taiyin::TAIYIN_ERROR_INTERNAL;
    }
    return taiyin::TAIYIN_STATUS_OK;
}

}  // extern "C"
