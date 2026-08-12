#include "taiyin/runtime/heliacal_visibility.h"

#include "runtime/visibility/heliacal_visibility_internal.h"
#include "runtime/core/native_context_checks.h"

#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/observed_position.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/runtime/star_position.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace taiyin {
namespace runtime {
namespace {

constexpr double kBelokrylovReferenceExtinction = 0.25;
constexpr double kBelokrylovBrightMagnitudeBoundary = 4.2;
constexpr double kBelokrylovBrightSolarAltitudeBoundaryDeg = -7.70;
constexpr double kBelokrylovProximityStartDeg = 58.0;
constexpr double kBelokrylovProximitySlope = 0.0338;
constexpr double kHeliacalSunsetUpperLimitDeg = -0.85;
constexpr double kSchaeferNightSkyBrightnessNanolambert = 180.0;
constexpr double kSchaeferNanolambertToErg = 1.02e-15;
constexpr double kSchaeferSolarMagnitude = -26.74;
constexpr double kSchaeferZeroMagnitudeReference = -11.05;
constexpr double kSchaeferMinimumSeparationDeg = 0.1;
constexpr double kSchaeferTwilightLowSunDeg = -3.0;
constexpr double kSchaeferDaylightHighSunDeg = 4.0;
constexpr double kSchaeferScotopicThresholdNanolambert = 1479.1;
constexpr double kSchaeferExtinctionWavelengthMicrometer = 0.55;
constexpr double kStandardRelativeHumidityPercent = 40.0;
constexpr double kAstronomicalToOpticalDepth = 0.921034037197618;

uint64_t heliacal_observed_flags(uint64_t flags) noexcept {
    const uint32_t position_flags = static_cast<uint32_t>(flags);
    uint64_t observed_flags = TAIYIN_OBSERVED_TOPOCENTRIC | TAIYIN_OBSERVED_HORIZONTAL;
    if ((position_flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_TRUEPOS;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_ASTROMETRIC;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_ABERR;
    }
    if ((position_flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) {
        observed_flags |= TAIYIN_OBSERVED_NO_GDEFL;
    }
    return observed_flags;
}

double nan_value() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

void clear_result(HeliacalVisibilityResult* out) noexcept {
    if (out) {
        *out = HeliacalVisibilityResult();
    }
}

double clamp_unit(double value) noexcept {
    return std::max(-1.0, std::min(1.0, value));
}

double horizontal_separation_rad(
    double altitude_0_rad,
    double azimuth_0_rad,
    double altitude_1_rad,
    double azimuth_1_rad
) noexcept {
    const double cosine = std::sin(altitude_0_rad) * std::sin(altitude_1_rad)
        + std::cos(altitude_0_rad) * std::cos(altitude_1_rad)
            * std::cos(azimuth_0_rad - azimuth_1_rad);
    return std::acos(clamp_unit(cosine));
}

double schaefer_airmass_from_altitude_rad(double altitude_rad) noexcept {
    const double zenith_distance_rad = std::min(
        0.5 * TAIYIN_PI,
        std::max(0.0, 0.5 * TAIYIN_PI - altitude_rad));
    const double cosine_z = std::cos(zenith_distance_rad);
    return 1.0 / (cosine_z + 0.025 * std::exp(-11.0 * cosine_z));
}

double schaefer_transmission(double extinction_mag_per_airmass, double airmass) noexcept {
    return std::pow(10.0, -0.4 * extinction_mag_per_airmass * airmass);
}

double schaefer_phase_function(double separation_deg) noexcept {
    const double separation = std::max(kSchaeferMinimumSeparationDeg, separation_deg);
    const double separation_rad = separation * TAIYIN_DEG_TO_RAD;
    return 6.2e7 / (separation * separation)
        + std::pow(10.0, 6.15 - separation / 40.0)
        + std::pow(10.0, 5.36) * (1.06 + std::cos(separation_rad) * std::cos(separation_rad));
}

double schaefer_sky_brightness_nanolambert(
    const HeliacalVisibilityModelInput& input
) noexcept {
    if (std::isfinite(input.sky_brightness_nanolambert)) {
        return input.sky_brightness_nanolambert;
    }

    const double target_airmass = schaefer_airmass_from_altitude_rad(input.target_altitude_rad);
    const double sun_airmass = schaefer_airmass_from_altitude_rad(input.sun_altitude_rad);
    const double target_transmission = schaefer_transmission(
        input.extinction_mag_per_airmass, target_airmass);
    const double sun_transmission = schaefer_transmission(
        input.extinction_mag_per_airmass, sun_airmass);
    const double target_zenith_distance_deg = std::min(
        90.0,
        std::max(0.0, 90.0 - input.target_altitude_rad * TAIYIN_RAD_TO_DEG));
    const double sun_altitude_deg = input.sun_altitude_rad * TAIYIN_RAD_TO_DEG;
    const double sun_zenith_distance_deg = std::min(
        180.0,
        std::max(0.0, 90.0 - sun_altitude_deg));
    const double separation_deg = std::max(
        kSchaeferMinimumSeparationDeg,
        input.target_sun_separation_rad * TAIYIN_RAD_TO_DEG);
    const double solar_scale = std::pow(
        10.0,
        -0.4 * (kSchaeferSolarMagnitude - kSchaeferZeroMagnitudeReference + 43.27));
    const double daylight_erg = solar_scale * (1.0 - target_transmission)
        * (schaefer_phase_function(separation_deg) * sun_transmission
            + 440000.0 * (1.0 - sun_transmission));
    const double twilight_erg = std::pow(
        10.0,
        -0.4 * (kSchaeferSolarMagnitude - kSchaeferZeroMagnitudeReference - 57.5
            + sun_zenith_distance_deg
            - target_zenith_distance_deg / (360.0 * input.extinction_mag_per_airmass)))
        * (1.0 - target_transmission) * 100.0 / separation_deg;
    const double sky_erg = sun_altitude_deg < kSchaeferTwilightLowSunDeg
        ? twilight_erg
        : (sun_altitude_deg > kSchaeferDaylightHighSunDeg
            ? daylight_erg
            : std::min(daylight_erg, twilight_erg));
    const double night_sky = std::isfinite(input.night_sky_brightness_nanolambert)
        ? input.night_sky_brightness_nanolambert
        : kSchaeferNightSkyBrightnessNanolambert;
    const double sine_zenith_distance = std::sin(target_zenith_distance_deg * TAIYIN_DEG_TO_RAD);
    const double angular_night_factor = 0.4 + 0.6 / std::sqrt(
        1.0 - 0.96 * sine_zenith_distance * sine_zenith_distance);
    return sky_erg / kSchaeferNanolambertToErg
        + night_sky * angular_night_factor * target_transmission;
}

double krisciunas_schaefer_scattering_airmass(double altitude_rad) noexcept {
    const double zenith_distance_rad = std::min(
        0.5 * TAIYIN_PI,
        std::max(0.0, 0.5 * TAIYIN_PI - altitude_rad));
    const double sine_z = std::sin(zenith_distance_rad);
    return 1.0 / std::sqrt(1.0 - 0.96 * sine_z * sine_z);
}

double krisciunas_schaefer_moonlight_nanolambert(
    const HeliacalVisibilityModelInput& input
) noexcept {
    if (!input.include_moonlight || input.moon_altitude_rad <= 0.0) return 0.0;

    const double phase_angle_deg = std::max(
        0.0,
        std::min(180.0, std::fabs(input.moon_phase_angle_rad * TAIYIN_RAD_TO_DEG)));
    const double separation_deg = std::max(
        kSchaeferMinimumSeparationDeg,
        horizontal_separation_rad(
            input.target_altitude_rad,
            input.target_azimuth_rad,
            input.moon_altitude_rad,
            input.moon_azimuth_rad) * TAIYIN_RAD_TO_DEG);
    const double separation_rad = separation_deg * TAIYIN_DEG_TO_RAD;
    const double rayleigh = std::pow(10.0, 5.36)
        * (1.06 + std::cos(separation_rad) * std::cos(separation_rad));
    const double mie = separation_deg < 10.0
        ? 6.2e7 / (separation_deg * separation_deg)
        : std::pow(10.0, 6.15 - separation_deg / 40.0);
    const double moon_illuminance_above_atmosphere = std::pow(
        10.0,
        -0.4 * (3.84 + 0.026 * phase_angle_deg
            + 4.0e-9 * std::pow(phase_angle_deg, 4.0)));
    const double moon_scattering_airmass =
        krisciunas_schaefer_scattering_airmass(input.moon_altitude_rad);
    const double target_scattering_airmass =
        krisciunas_schaefer_scattering_airmass(input.target_altitude_rad);
    const double moon_transmission = schaefer_transmission(
        input.extinction_mag_per_airmass, moon_scattering_airmass);
    const double target_scattered_fraction = 1.0 - schaefer_transmission(
        input.extinction_mag_per_airmass, target_scattering_airmass);
    return (rayleigh + mie) * moon_illuminance_above_atmosphere
        * moon_transmission * target_scattered_fraction;
}

Status sample_body_horizontal(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_right_ascension_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_altitude_rad || !out_azimuth_rad || !valid_heliacal_visibility_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_altitude_rad = nan_value();
    *out_azimuth_rad = nan_value();
    if (out_right_ascension_rad) *out_right_ascension_rad = nan_value();
    NativeCalcContext equatorial_context;
    const NativeCalcContext* evaluation_context = context;
    if (out_right_ascension_rad) {
        equatorial_context = *context;
        equatorial_context.apparent_options.model_context = &equatorial_context.model_context;
        equatorial_context.apparent_options.output_frame_id = TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE;
        evaluation_context = &equatorial_context;
    }
    ObservedPosition observed;
    const Status status = calc_observed_ut(
        evaluation_context,
        jd_ut,
        &body_id,
        1,
        heliacal_observed_flags(flags),
        &observed,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (observed.status != TAIYIN_STATUS_OK) return observed.status;
    if (!std::isfinite(observed.horizontal.altitude_rad)
        || !std::isfinite(observed.horizontal.azimuth_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_altitude_rad = observed.horizontal.altitude_rad;
    *out_azimuth_rad = observed.horizontal.azimuth_rad;
    if (out_right_ascension_rad) {
        if (!std::isfinite(observed.apparent.longitude_rad)) {
            return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
        }
        *out_right_ascension_rad = observed.apparent.longitude_rad;
    }
    return TAIYIN_STATUS_OK;
}

Status sample_star_horizontal(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_altitude_rad || !out_azimuth_rad || !valid_heliacal_visibility_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_altitude_rad = nan_value();
    *out_azimuth_rad = nan_value();
    ObservedPosition observed;
    const Status status = calc_observed_star_ut(
        context,
        star_key,
        jd_ut,
        heliacal_observed_flags(flags),
        &observed,
        diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (observed.status != TAIYIN_STATUS_OK) return observed.status;
    if (!std::isfinite(observed.horizontal.altitude_rad)
        || !std::isfinite(observed.horizontal.azimuth_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_altitude_rad = observed.horizontal.altitude_rad;
    *out_azimuth_rad = observed.horizontal.azimuth_rad;
    return TAIYIN_STATUS_OK;
}

Status sample_moonlight_geometry(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_phase_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_altitude_rad || !out_azimuth_rad || !out_phase_angle_rad) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_altitude_rad = nan_value();
    *out_azimuth_rad = nan_value();
    *out_phase_angle_rad = nan_value();
    Status status = sample_body_horizontal(
        context, TAIYIN_BODY_MOON, jd_ut, flags,
        out_altitude_rad, out_azimuth_rad, nullptr, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    BodyPhenomena moon;
    status = calc_body_phenomena_ut(
        context, TAIYIN_BODY_MOON, jd_ut, static_cast<uint32_t>(flags), &moon, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(moon.phase_angle_rad)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    *out_phase_angle_rad = moon.phase_angle_rad;
    return TAIYIN_STATUS_OK;
}

Status sample_optional_moonlight_geometry(
    const NativeCalcContext* context,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    int include_moonlight,
    double* out_altitude_rad,
    double* out_azimuth_rad,
    double* out_phase_angle_rad,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    if (!out_altitude_rad || !out_azimuth_rad || !out_phase_angle_rad
        || (include_moonlight != 0 && include_moonlight != 1)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_altitude_rad = nan_value();
    *out_azimuth_rad = nan_value();
    *out_phase_angle_rad = nan_value();
    if (include_moonlight == 0) return TAIYIN_STATUS_OK;
    return sample_moonlight_geometry(
        context, jd_ut, flags, out_altitude_rad, out_azimuth_rad, out_phase_angle_rad, diagnostic);
}

double schaefer_visual_wavelength_micrometer(double sun_altitude_rad) noexcept {
    const double twilight_progress = std::max(0.0, std::min(
        6.0, -sun_altitude_rad * TAIYIN_RAD_TO_DEG - 12.0));
    return kSchaeferExtinctionWavelengthMicrometer
        - 0.04 * twilight_progress / 6.0;
}

bool schaefer_atmosphere_values(
    const NativeCalcContext& context,
    bool strict,
    double* out_pressure_mbar,
    double* out_temperature_celsius,
    double* out_relative_humidity
) noexcept {
    if (!out_pressure_mbar || !out_temperature_celsius || !out_relative_humidity) return false;
    const bool have_pressure = context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_PRESSURE);
    const bool have_temperature = context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_TEMPERATURE);
    const bool have_humidity = context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE_HUMIDITY);
    if (strict && (!have_pressure || !have_temperature || !have_humidity)) return false;
    const bool allow_standard = (context.atmosphere_policy_flags
        & TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK) != 0u;
    if ((!have_pressure || !have_temperature || !have_humidity) && !allow_standard) return false;

    const double height_m = context.observer_location.height_m;
    const double standard_pressure_base = 1.0 - 0.0065 * height_m / 288.15;
    if (!std::isfinite(height_m) || !(standard_pressure_base > 0.0)) return false;
    const double pressure = have_pressure ? context.atmosphere.pressure_mbar
        : 1013.25 * std::pow(standard_pressure_base, 5.255);
    const double temperature = have_temperature ? context.atmosphere.temperature_celsius
        : 15.0 - 0.0065 * height_m;
    const double humidity = have_humidity ? context.atmosphere.relative_humidity
        : kStandardRelativeHumidityPercent;
    if (!std::isfinite(pressure) || !(pressure > 0.0)
        || !std::isfinite(temperature)
        || !std::isfinite(humidity) || !(humidity >= 0.0) || !(humidity <= 100.0)) {
        return false;
    }
    *out_pressure_mbar = pressure;
    *out_temperature_celsius = temperature;
    *out_relative_humidity = humidity;
    return true;
}

Status resolve_schaefer_2000_extinction(
    const NativeCalcContext& context,
    const HeliacalVisibilityConditions* conditions,
    uint64_t flags,
    double sun_altitude_rad,
    double sun_right_ascension_rad,
    double* out_extinction_mag_per_airmass
) noexcept {
    if (!out_extinction_mag_per_airmass || !std::isfinite(sun_altitude_rad)
        || !std::isfinite(sun_right_ascension_rad)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_extinction_mag_per_airmass = nan_value();
    if (conditions && std::isfinite(conditions->extinction_mag_per_airmass)) {
        if (!(conditions->extinction_mag_per_airmass > 0.0)) return TAIYIN_ERROR_INVALID_ARGUMENT;
        *out_extinction_mag_per_airmass = conditions->extinction_mag_per_airmass;
        return TAIYIN_STATUS_OK;
    }

    const bool strict = (flags & TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY) != 0u;
    double pressure_mbar = nan_value();
    double temperature_celsius = nan_value();
    double humidity_percent = nan_value();
    if (!schaefer_atmosphere_values(
            context, strict, &pressure_mbar, &temperature_celsius, &humidity_percent)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const bool have_meteorological_range =
        context.fields.has(TAIYIN_NATIVE_FIELD_METEOROLOGICAL_RANGE);
    const double meteorological_range_km = context.meteorological_range_km;
    if (strict && (!have_meteorological_range || !std::isfinite(meteorological_range_km)
            || !(meteorological_range_km >= 1.0))) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (have_meteorological_range
        && (!std::isfinite(meteorological_range_km) || !(meteorological_range_km >= 1.0))) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (!have_meteorological_range && !(humidity_percent > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const double height_m = context.observer_location.height_m;
    const double wavelength = schaefer_visual_wavelength_micrometer(sun_altitude_rad);
    const double rayleigh = 0.1066 * (pressure_mbar / 1013.25)
        * std::pow(wavelength / kSchaeferExtinctionWavelengthMicrometer, -4.0);
    const double water = 0.031 * 0.94 * (humidity_percent / 100.0)
        * std::exp(temperature_celsius / 15.0) * std::exp(-height_m / 3000.0);
    const double twilight_progress = std::max(0.0, std::min(
        6.0, -sun_altitude_rad * TAIYIN_RAD_TO_DEG - 12.0));
    const double ozone = 0.031 * (3.0 + 0.4 * (
        context.observer_location.latitude_rad * std::cos(sun_right_ascension_rad)
        - std::cos(3.0 * context.observer_location.latitude_rad))) / 3.0
        * (100.0 - 11.6 * twilight_progress) / 100.0;
    double aerosol = nan_value();
    if (std::isfinite(meteorological_range_km)) {
        const double beta_visibility = 3.912 / meteorological_range_km;
        const double beta_aerosol = std::max(0.0, beta_visibility
            - (water / 3000.0 + rayleigh / 8515.0) * 1000.0 * kAstronomicalToOpticalDepth);
        aerosol = beta_aerosol * 3745.0 / 1000.0 / kAstronomicalToOpticalDepth;
    } else {
        const double latitude_sign = context.observer_location.latitude_rad < 0.0 ? -1.0 : 1.0;
        aerosol = 0.1 * std::exp(-height_m / 3745.0)
            * std::pow(1.0 - 0.32 / std::log(humidity_percent / 100.0), 1.33)
            * (1.0 + 0.33 * latitude_sign * std::sin(sun_right_ascension_rad))
            * std::pow(wavelength / kSchaeferExtinctionWavelengthMicrometer, -1.3);
    }
    const double extinction = rayleigh + water + ozone + aerosol;
    if (!std::isfinite(extinction) || !(extinction > 0.0)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    *out_extinction_mag_per_airmass = extinction;
    return TAIYIN_STATUS_OK;
}

Status evaluate_visibility(
    const NativeCalcContext* context,
    double target_magnitude,
    double target_altitude_rad,
    double target_azimuth_rad,
    double sun_altitude_rad,
    double sun_azimuth_rad,
    double sun_right_ascension_rad,
    double moon_altitude_rad,
    double moon_azimuth_rad,
    double moon_phase_angle_rad,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out
) noexcept {
    if (!context || !out || !std::isfinite(target_magnitude)
        || !std::isfinite(target_altitude_rad) || !std::isfinite(target_azimuth_rad)
        || !std::isfinite(sun_altitude_rad) || !std::isfinite(sun_azimuth_rad)
        || !std::isfinite(sun_right_ascension_rad) || !valid_heliacal_visibility_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    const int include_moonlight = (flags & TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT) != 0u
        && (!conditions || !std::isfinite(conditions->sky_brightness_nanolambert)) ? 1 : 0;

    dispatch::HeliacalVisibilityModelEntry profile;
    if (!dispatch::find_heliacal_visibility_model(context->heliacal_visibility_model_id, &profile)
        || !profile.eval || !(profile.default_extinction_mag_per_airmass > 0.0)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    if (include_moonlight != 0
        && profile.moonlight_model_id == dispatch::HELIACAL_MOONLIGHT_NONE) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    double extinction = nan_value();
    Status status = TAIYIN_STATUS_OK;
    if (profile.extinction_model_id == dispatch::HELIACAL_EXTINCTION_SCHAEFER_2000) {
        status = resolve_schaefer_2000_extinction(
            *context, conditions, flags, sun_altitude_rad, sun_right_ascension_rad, &extinction);
    } else {
        extinction = conditions && std::isfinite(conditions->extinction_mag_per_airmass)
            ? conditions->extinction_mag_per_airmass : profile.default_extinction_mag_per_airmass;
        if ((flags & TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY) != 0u
            && (!conditions || !std::isfinite(conditions->extinction_mag_per_airmass))) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
    }
    if (status != TAIYIN_STATUS_OK) return status;
    if (!std::isfinite(extinction) || !(extinction > 0.0)) return TAIYIN_ERROR_INVALID_ARGUMENT;

    HeliacalVisibilityModelInput input;
    input.target_magnitude = target_magnitude;
    input.target_altitude_rad = target_altitude_rad;
    input.target_azimuth_rad = target_azimuth_rad;
    input.sun_altitude_rad = sun_altitude_rad;
    input.sun_azimuth_rad = sun_azimuth_rad;
    input.target_sun_separation_rad = horizontal_separation_rad(
        target_altitude_rad, target_azimuth_rad, sun_altitude_rad, sun_azimuth_rad);
    input.extinction_mag_per_airmass = extinction;
    input.sky_brightness_nanolambert = conditions
        ? conditions->sky_brightness_nanolambert : nan_value();
    input.night_sky_brightness_nanolambert = conditions
        ? conditions->night_sky_brightness_nanolambert : nan_value();
    input.moon_altitude_rad = moon_altitude_rad;
    input.moon_azimuth_rad = moon_azimuth_rad;
    input.moon_phase_angle_rad = moon_phase_angle_rad;
    input.include_moonlight = include_moonlight;

    HeliacalVisibilityResult result;
    if (!dispatch::eval_heliacal_visibility(profile.model_id, &input, &result)) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }
    result.model_id = profile.model_id;
    result.extinction_model_id = profile.extinction_model_id;
    result.twilight_model_id = profile.twilight_model_id;
    result.moonlight_model_id = profile.moonlight_model_id;
    result.visual_threshold_model_id = profile.visual_threshold_model_id;
    *out = result;
    return TAIYIN_STATUS_OK;
}

}  // namespace

bool valid_heliacal_visibility_flags(uint64_t flags) noexcept {
    const uint32_t position_flags = static_cast<uint32_t>(flags);
    const uint32_t supported_position_flags = TAIYIN_NATIVE_POSITION_TRUEPOS
        | TAIYIN_NATIVE_POSITION_ASTROMETRIC
        | TAIYIN_NATIVE_POSITION_NO_ABERR
        | TAIYIN_NATIVE_POSITION_NO_GDEFL;
    const uint64_t supported_flags = static_cast<uint64_t>(supported_position_flags)
        | TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT
        | TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY;
    return (position_flags & ~supported_position_flags) == 0u
        && (flags & ~supported_flags) == 0u;
}

bool valid_heliacal_body_target(int body_id) noexcept {
    return body_id != TAIYIN_BODY_SUN
        && body_id != TAIYIN_BODY_MOON
        && body_id != TAIYIN_BODY_EARTH
        && body_id != TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER;
}

HeliacalVisibilityConditions::HeliacalVisibilityConditions() noexcept
    : extinction_mag_per_airmass(nan_value()),
      sky_brightness_nanolambert(nan_value()),
      night_sky_brightness_nanolambert(nan_value()) {}

HeliacalVisibilityModelInput::HeliacalVisibilityModelInput() noexcept
    : target_magnitude(nan_value()),
      target_altitude_rad(nan_value()),
      target_azimuth_rad(nan_value()),
      sun_altitude_rad(nan_value()),
      sun_azimuth_rad(nan_value()),
      target_sun_separation_rad(nan_value()),
      extinction_mag_per_airmass(nan_value()),
      sky_brightness_nanolambert(nan_value()),
      night_sky_brightness_nanolambert(nan_value()),
      moon_altitude_rad(nan_value()),
      moon_azimuth_rad(nan_value()),
      moon_phase_angle_rad(nan_value()),
      include_moonlight(0) {}

HeliacalVisibilityResult::HeliacalVisibilityResult() noexcept
    : visible(0),
      model_id(-1),
      extinction_model_id(-1),
      twilight_model_id(-1),
      moonlight_model_id(-1),
      visual_threshold_model_id(-1),
      target_magnitude(nan_value()),
      limiting_magnitude(nan_value()),
      target_altitude_rad(nan_value()),
      target_azimuth_rad(nan_value()),
      sun_altitude_rad(nan_value()),
      sun_azimuth_rad(nan_value()),
      target_sun_separation_rad(nan_value()),
      airmass(nan_value()),
      extinction_mag_per_airmass(nan_value()),
      extinction_mag(nan_value()),
      sky_brightness_nanolambert(nan_value()),
      moonlight_brightness_nanolambert(nan_value()),
      threshold_illuminance_footcandles(nan_value()),
      target_illuminance_footcandles(nan_value()),
      visibility_margin_magnitude(nan_value()),
      required_sun_altitude_rad(nan_value()),
      solar_depression_margin_rad(nan_value()) {}

bool heliacal_visibility_eval_belokrylov_2011(
    const HeliacalVisibilityModelInput* input,
    HeliacalVisibilityResult* out
) noexcept {
    clear_result(out);
    if (!input || !out || !std::isfinite(input->target_magnitude)
        || !std::isfinite(input->target_altitude_rad)
        || !std::isfinite(input->target_azimuth_rad)
        || !std::isfinite(input->sun_altitude_rad)
        || !std::isfinite(input->sun_azimuth_rad)
        || !std::isfinite(input->target_sun_separation_rad)
        || !(input->extinction_mag_per_airmass > 0.0)) {
        return false;
    }

    const double target_altitude_deg = input->target_altitude_rad * TAIYIN_RAD_TO_DEG;
    const double sun_altitude_deg = input->sun_altitude_rad * TAIYIN_RAD_TO_DEG;
    const double separation_deg = input->target_sun_separation_rad * TAIYIN_RAD_TO_DEG;
    const double zenith_distance_rad = std::min(
        0.5 * TAIYIN_PI,
        std::max(0.0, 0.5 * TAIYIN_PI - input->target_altitude_rad));
    const double cosine_z = std::cos(zenith_distance_rad);
    const double airmass = 1.0 / (cosine_z + 0.025 * std::exp(-11.0 * cosine_z));
    const double extinction_mag = input->extinction_mag_per_airmass * (airmass - 1.0)
        + (input->extinction_mag_per_airmass - kBelokrylovReferenceExtinction);
    const double extinguished_magnitude = input->target_magnitude + extinction_mag;

    const double base_required_sun_altitude_deg =
        extinguished_magnitude <= kBelokrylovBrightMagnitudeBoundary
            ? -2.47 - 1.23 * extinguished_magnitude
            : 15.62 - 6.61 * extinguished_magnitude;
    const double proximity_adjustment_deg = separation_deg < kBelokrylovProximityStartDeg
        ? -kBelokrylovProximitySlope * (kBelokrylovProximityStartDeg - separation_deg)
        : 0.0;
    const double required_sun_altitude_deg = std::min(
        kHeliacalSunsetUpperLimitDeg,
        base_required_sun_altitude_deg + proximity_adjustment_deg);
    const double base_actual_sun_altitude_deg = sun_altitude_deg - proximity_adjustment_deg;
    const double limiting_extinguished_magnitude =
        base_actual_sun_altitude_deg >= kBelokrylovBrightSolarAltitudeBoundaryDeg
            ? (-2.47 - base_actual_sun_altitude_deg) / 1.23
            : (15.62 - base_actual_sun_altitude_deg) / 6.61;
    const double limiting_magnitude = limiting_extinguished_magnitude - extinction_mag;
    const double margin_rad = (required_sun_altitude_deg - sun_altitude_deg) * TAIYIN_DEG_TO_RAD;

    if (!std::isfinite(airmass) || !std::isfinite(extinction_mag)
        || !std::isfinite(required_sun_altitude_deg)
        || !std::isfinite(limiting_magnitude) || !std::isfinite(margin_rad)) {
        return false;
    }

    out->visible = target_altitude_deg > 0.0
        && sun_altitude_deg <= kHeliacalSunsetUpperLimitDeg
        && margin_rad >= 0.0;
    out->target_magnitude = input->target_magnitude;
    out->limiting_magnitude = limiting_magnitude;
    out->target_altitude_rad = input->target_altitude_rad;
    out->target_azimuth_rad = input->target_azimuth_rad;
    out->sun_altitude_rad = input->sun_altitude_rad;
    out->sun_azimuth_rad = input->sun_azimuth_rad;
    out->target_sun_separation_rad = input->target_sun_separation_rad;
    out->airmass = airmass;
    out->extinction_mag_per_airmass = input->extinction_mag_per_airmass;
    out->extinction_mag = extinction_mag;
    out->visibility_margin_magnitude = limiting_magnitude - input->target_magnitude;
    out->required_sun_altitude_rad = required_sun_altitude_deg * TAIYIN_DEG_TO_RAD;
    out->solar_depression_margin_rad = margin_rad;
    return true;
}

bool heliacal_visibility_eval_schaefer_1993(
    const HeliacalVisibilityModelInput* input,
    HeliacalVisibilityResult* out
) noexcept {
    clear_result(out);
    if (!input || !out || !std::isfinite(input->target_magnitude)
        || !std::isfinite(input->target_altitude_rad)
        || !std::isfinite(input->target_azimuth_rad)
        || !std::isfinite(input->sun_altitude_rad)
        || !std::isfinite(input->sun_azimuth_rad)
        || !std::isfinite(input->target_sun_separation_rad)
        || !(input->extinction_mag_per_airmass > 0.0)
        || (input->include_moonlight != 0 && input->include_moonlight != 1)
        || (input->include_moonlight != 0
            && !std::isfinite(input->sky_brightness_nanolambert)
            && (!std::isfinite(input->moon_altitude_rad)
                || !std::isfinite(input->moon_azimuth_rad)
                || !std::isfinite(input->moon_phase_angle_rad)))
        || (std::isfinite(input->sky_brightness_nanolambert)
            && !(input->sky_brightness_nanolambert > 0.0))
        || (std::isfinite(input->night_sky_brightness_nanolambert)
            && !(input->night_sky_brightness_nanolambert > 0.0))) {
        return false;
    }

    const double airmass = schaefer_airmass_from_altitude_rad(input->target_altitude_rad);
    const double extinction_mag = input->extinction_mag_per_airmass * airmass;
    const bool direct_sky_measurement = std::isfinite(input->sky_brightness_nanolambert);
    const double moonlight_brightness = direct_sky_measurement
        ? 0.0 : krisciunas_schaefer_moonlight_nanolambert(*input);
    const double sky_brightness = schaefer_sky_brightness_nanolambert(*input)
        + moonlight_brightness;
    const bool scotopic = sky_brightness < kSchaeferScotopicThresholdNanolambert;
    const double threshold_c = scotopic ? 1.5848931924611e-10 : 4.4668359215096e-9;
    const double threshold_k = scotopic ? 0.012589254117942 : 1.2589254117942e-6;
    const double threshold_illuminance = threshold_c
        * std::pow(1.0 + std::sqrt(threshold_k * sky_brightness), 2.0);
    const double target_illuminance = std::pow(
        10.0, -0.4 * (input->target_magnitude + 16.57 + extinction_mag));
    const double limiting_magnitude = -16.57
        - 2.5 * std::log10(threshold_illuminance) - extinction_mag;
    const double visibility_margin = limiting_magnitude - input->target_magnitude;

    if (!std::isfinite(airmass) || !std::isfinite(extinction_mag)
        || !std::isfinite(sky_brightness) || !(sky_brightness > 0.0)
        || !std::isfinite(moonlight_brightness) || !(moonlight_brightness >= 0.0)
        || !std::isfinite(threshold_illuminance) || !(threshold_illuminance > 0.0)
        || !std::isfinite(target_illuminance) || !(target_illuminance > 0.0)
        || !std::isfinite(limiting_magnitude) || !std::isfinite(visibility_margin)) {
        return false;
    }

    out->visible = input->target_altitude_rad > 0.0 && visibility_margin >= 0.0;
    out->target_magnitude = input->target_magnitude;
    out->limiting_magnitude = limiting_magnitude;
    out->target_altitude_rad = input->target_altitude_rad;
    out->target_azimuth_rad = input->target_azimuth_rad;
    out->sun_altitude_rad = input->sun_altitude_rad;
    out->sun_azimuth_rad = input->sun_azimuth_rad;
    out->target_sun_separation_rad = input->target_sun_separation_rad;
    out->airmass = airmass;
    out->extinction_mag_per_airmass = input->extinction_mag_per_airmass;
    out->extinction_mag = extinction_mag;
    out->sky_brightness_nanolambert = sky_brightness;
    out->moonlight_brightness_nanolambert = moonlight_brightness;
    out->threshold_illuminance_footcandles = threshold_illuminance;
    out->target_illuminance_footcandles = target_illuminance;
    out->visibility_margin_magnitude = visibility_margin;
    return true;
}

Status calc_body_heliacal_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_result(out);
    if (!context || !out || !split_julian_date_is_finite(jd_ut)
        || !valid_heliacal_body_target(body_id)
        || !native_context_has_observer_location(*context)
        || !valid_heliacal_visibility_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double target_altitude = nan_value();
    double target_azimuth = nan_value();
    Status status = sample_body_horizontal(
        context, body_id, jd_ut, flags, &target_altitude, &target_azimuth, nullptr, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double sun_altitude = nan_value();
    double sun_azimuth = nan_value();
    double sun_right_ascension = nan_value();
    EphemerisEvalDiagnostic scratch;
    status = sample_body_horizontal(
        context, TAIYIN_BODY_SUN, jd_ut, flags,
        &sun_altitude, &sun_azimuth, &sun_right_ascension, &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }

    BodyPhenomena phenomena;
    status = calc_body_phenomena_ut(
        context, body_id, jd_ut, static_cast<uint32_t>(flags), &phenomena, &scratch);
    if (status != TAIYIN_STATUS_OK || !std::isfinite(phenomena.apparent_magnitude)) {
        if (diagnostic) *diagnostic = scratch;
        return status != TAIYIN_STATUS_OK ? status : TAIYIN_ERROR_UNSUPPORTED;
    }
    const int include_moonlight = (flags & TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT) != 0u
        && (!conditions || !std::isfinite(conditions->sky_brightness_nanolambert)) ? 1 : 0;
    double moon_altitude = nan_value();
    double moon_azimuth = nan_value();
    double moon_phase_angle = nan_value();
    status = sample_optional_moonlight_geometry(
        context,
        jd_ut,
        flags,
        include_moonlight,
        &moon_altitude,
        &moon_azimuth,
        &moon_phase_angle,
        &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }
    return evaluate_visibility(
        context,
        phenomena.apparent_magnitude,
        target_altitude,
        target_azimuth,
        sun_altitude,
        sun_azimuth,
        sun_right_ascension,
        moon_altitude,
        moon_azimuth,
        moon_phase_angle,
        flags,
        conditions,
        out);
}

Status calc_star_heliacal_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const SplitJulianDate& jd_ut,
    uint64_t flags,
    const HeliacalVisibilityConditions* conditions,
    HeliacalVisibilityResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept {
    clear_result(out);
    if (!context || !star_key || star_key[0] == '\0' || !out
        || !split_julian_date_is_finite(jd_ut)
        || !native_context_has_observer_location(*context)
        || !valid_heliacal_visibility_flags(flags)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    double target_magnitude = nan_value();
    Status status = find_global_star_magnitude(star_key, &target_magnitude);
    if (status != TAIYIN_STATUS_OK) return status;

    double target_altitude = nan_value();
    double target_azimuth = nan_value();
    status = sample_star_horizontal(
        context, star_key, jd_ut, flags, &target_altitude, &target_azimuth, diagnostic);
    if (status != TAIYIN_STATUS_OK) return status;

    double sun_altitude = nan_value();
    double sun_azimuth = nan_value();
    double sun_right_ascension = nan_value();
    EphemerisEvalDiagnostic scratch;
    status = sample_body_horizontal(
        context, TAIYIN_BODY_SUN, jd_ut, flags,
        &sun_altitude, &sun_azimuth, &sun_right_ascension, &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }
    const int include_moonlight = (flags & TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT) != 0u
        && (!conditions || !std::isfinite(conditions->sky_brightness_nanolambert)) ? 1 : 0;
    double moon_altitude = nan_value();
    double moon_azimuth = nan_value();
    double moon_phase_angle = nan_value();
    status = sample_optional_moonlight_geometry(
        context,
        jd_ut,
        flags,
        include_moonlight,
        &moon_altitude,
        &moon_azimuth,
        &moon_phase_angle,
        &scratch);
    if (status != TAIYIN_STATUS_OK) {
        if (diagnostic) *diagnostic = scratch;
        return status;
    }
    return evaluate_visibility(
        context,
        target_magnitude,
        target_altitude,
        target_azimuth,
        sun_altitude,
        sun_azimuth,
        sun_right_ascension,
        moon_altitude,
        moon_azimuth,
        moon_phase_angle,
        flags,
        conditions,
        out);
}

}  // namespace runtime
}  // namespace taiyin
