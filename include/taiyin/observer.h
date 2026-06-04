#ifndef TAIYIN_OBSERVER_H
#define TAIYIN_OBSERVER_H

#include "coordinates.h"
#include "vector3.h"

namespace taiyin {

struct HorizontalCoordinates {
    double azimuth_rad;
    double altitude_rad;
    double distance_au;
};

struct HorizontalRates {
    double azimuth_rate_rad_per_day;
    double altitude_rate_rad_per_day;
    double distance_rate_au_per_day;
};

enum RefractionModel {
    Bennett,
    Skyfield,
    Hybrid,
    AuerStandish,
    Sofa,
};

Vector3 local_east_itrf(double longitude_rad, double latitude_rad) noexcept;
Vector3 local_north_itrf(double longitude_rad, double latitude_rad) noexcept;
Vector3 local_up_itrf(double longitude_rad, double latitude_rad) noexcept;

Vector3 geodetic_to_ecef_m(double longitude_rad, double latitude_rad, double height_m) noexcept;
Matrix3x3 polar_motion_matrix(double xp_rad, double yp_rad, double sp_rad) noexcept;
Matrix3x3 earth_rotation_matrix(double sidereal_angle_rad) noexcept;
Vector3 terrestrial_to_intermediate_m(
    const Vector3& itrf_m,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept;
Matrix3x3 terrestrial_to_true_equator_of_date_matrix(
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept;
Matrix3x3 terrestrial_to_cirs_matrix(
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept;
Vector3 terrestrial_to_true_equator_of_date_au(
    const Vector3& itrf_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept;
Vector3 terrestrial_to_cirs_au(
    const Vector3& itrf_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad
) noexcept;

Vector3 observer_geocentric_simple_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt
) noexcept;
bool observer_geocentric_simple_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    Vector3* out_velocity_au_per_day
) noexcept;
bool observer_geocentric_simple_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    Vector3* out_acceleration_au_per_day2
) noexcept;

bool observer_geocentric_true_equator_of_date_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    Vector3* out_position_au
) noexcept;
bool observer_geocentric_true_equator_of_date_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double derivative_step_days,
    Vector3* out_velocity_au_per_day
) noexcept;
bool observer_geocentric_true_equator_of_date_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    Vector3* out_velocity_au_per_day
) noexcept;
bool observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days,
    Vector3* out_acceleration_au_per_day2
) noexcept;
bool observer_geocentric_true_equator_of_date_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double jd_tt,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept;

bool observer_geocentric_cirs_position_au(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    Vector3* out_position_au
) noexcept;
bool observer_geocentric_cirs_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double derivative_step_days,
    Vector3* out_velocity_au_per_day
) noexcept;
bool observer_geocentric_cirs_velocity_au_per_day(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    Vector3* out_velocity_au_per_day
) noexcept;
bool observer_geocentric_cirs_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    double derivative_step_days,
    Vector3* out_acceleration_au_per_day2
) noexcept;
bool observer_geocentric_cirs_acceleration_au_per_day2(
    double longitude_rad,
    double latitude_rad,
    double height_m,
    double jd_ut1,
    double xp_rad,
    double yp_rad,
    double sp_rad,
    double xp_rate_rad_per_day,
    double yp_rate_rad_per_day,
    double sp_rate_rad_per_day,
    double dut1_rate_seconds_per_day,
    double lod_seconds,
    double lod_rate_seconds_per_day,
    Vector3* out_acceleration_au_per_day2
) noexcept;

Vector3 topocentric_position_au(const Vector3& target_geocentric_au, const Vector3& observer_geocentric_au) noexcept;
Vector3 topocentric_velocity_au_per_day(
    const Vector3& target_geocentric_velocity_au_per_day,
    const Vector3& observer_geocentric_velocity_au_per_day
) noexcept;
Vector3 topocentric_acceleration_au_per_day2(
    const Vector3& target_geocentric_acceleration_au_per_day2,
    const Vector3& observer_geocentric_acceleration_au_per_day2
) noexcept;
HorizontalCoordinates topocentric_position_to_horizontal(
    const Vector3& topocentric_equatorial_au,
    double local_sidereal_rad,
    double latitude_rad
) noexcept;
bool topocentric_velocity_to_horizontal_rates(
    const Vector3& topocentric_equatorial_position_au,
    const Vector3& topocentric_equatorial_velocity_au_per_day,
    double local_sidereal_rad,
    double local_sidereal_rate_rad_per_day,
    double latitude_rad,
    HorizontalRates* out_rates
) noexcept;

double atmospheric_refraction_bennett_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept;
double atmospheric_refraction_skyfield_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    int max_iterations,
    double tolerance_deg
) noexcept;
double atmospheric_refraction_skyfield_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept;
double atmospheric_refraction_hybrid_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept;
double atmospheric_refraction_auer_standish_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    int max_iterations,
    double tolerance_rad
) noexcept;
double atmospheric_refraction_auer_standish_rad(double altitude_rad, double pressure_mbar, double temperature_celsius) noexcept;
double atmospheric_refraction_sofa_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer
) noexcept;
double atmospheric_refraction_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept;
double atmospheric_refraction_derivative_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model,
    double derivative_step_rad
) noexcept;
double atmospheric_refraction_derivative_rad(
    double altitude_rad,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept;
HorizontalCoordinates refract_horizontal_coordinates(
    const HorizontalCoordinates& horizontal,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept;
HorizontalRates refract_horizontal_rates(
    const HorizontalCoordinates& unrefracted_position,
    const HorizontalRates& unrefracted_rates,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model,
    double derivative_step_rad
) noexcept;
HorizontalRates refract_horizontal_rates(
    const HorizontalCoordinates& unrefracted_position,
    const HorizontalRates& unrefracted_rates,
    double pressure_mbar,
    double temperature_celsius,
    double relative_humidity,
    double wavelength_micrometer,
    RefractionModel model
) noexcept;

}  // namespace taiyin

#endif  // TAIYIN_OBSERVER_H
