#ifndef TAIYIN_RUNTIME_PHENOMENA_INTERNAL_H
#define TAIYIN_RUNTIME_PHENOMENA_INTERNAL_H

namespace taiyin {
namespace runtime {
namespace internal {

double phenomena_sun_apparent_magnitude(double observer_body_distance_au) noexcept;
double phenomena_moon_apparent_magnitude(
    double phase_angle_deg,
    double observer_body_distance_au,
    double sun_body_distance_au,
    bool before_full
) noexcept;
double phenomena_mars_magnitude_correction(char kind, double angle_deg) noexcept;
double phenomena_neptune_phase_magnitude_term(SplitJulianDate jd, double phase_angle_deg) noexcept;
double phenomena_hg_phase_function(double phase_angle_deg, double h, double g) noexcept;

}  // namespace internal
}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_PHENOMENA_INTERNAL_H
