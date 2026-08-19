#ifndef TAIYIN_C_HELIACAL_H
#define TAIYIN_C_HELIACAL_H

#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAIYIN_HELIACAL_OPTION_INCLUDE_MOONLIGHT (UINT64_C(1) << 32)
#define TAIYIN_HELIACAL_OPTION_STRICT_METEOROLOGY (UINT64_C(1) << 33)

enum taiyin_heliacal_event_kind {
    TAIYIN_C_HELIACAL_EVENT_MORNING_FIRST = 1,
    TAIYIN_C_HELIACAL_EVENT_MORNING_LAST = 2,
    TAIYIN_C_HELIACAL_EVENT_EVENING_FIRST = 3,
    TAIYIN_C_HELIACAL_EVENT_EVENING_LAST = 4
};

typedef struct taiyin_heliacal_visibility_conditions {
    uint32_t struct_size;
    double extinction_mag_per_airmass;
    double sky_brightness_nanolambert;
    double night_sky_brightness_nanolambert;
} taiyin_heliacal_visibility_conditions;

typedef struct taiyin_heliacal_visibility_result {
    uint32_t struct_size;
    uint8_t visible;
    uint8_t reserved[3];
    int32_t model_id;
    int32_t extinction_model_id;
    int32_t twilight_model_id;
    int32_t moonlight_model_id;
    int32_t visual_threshold_model_id;
    double target_magnitude;
    double limiting_magnitude;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    double target_sun_separation_rad;
    double airmass;
    double extinction_mag_per_airmass;
    double extinction_mag;
    double sky_brightness_nanolambert;
    double moonlight_brightness_nanolambert;
    double threshold_illuminance_footcandles;
    double target_illuminance_footcandles;
    double visibility_margin_magnitude;
    double required_sun_altitude_rad;
    double solar_depression_margin_rad;
} taiyin_heliacal_visibility_result;

typedef struct taiyin_heliacal_visibility_search_result {
    uint32_t struct_size;
    int32_t event_kind;
    taiyin_split_julian_date jd_ut;
    taiyin_split_julian_date window_start_jd_ut;
    taiyin_split_julian_date window_end_jd_ut;
    int32_t scanned_day_count;
    int32_t sampled_window_count;
    int32_t visibility_evaluation_count;
    taiyin_heliacal_visibility_result visibility;
} taiyin_heliacal_visibility_search_result;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_heliacal_visibility_conditions_init(
    taiyin_heliacal_visibility_conditions* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_heliacal_visibility_result_init(
    taiyin_heliacal_visibility_result* value
);
TAIYIN_C_API void TAIYIN_C_CALL taiyin_heliacal_visibility_search_result_init(
    taiyin_heliacal_visibility_search_result* value
);

TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_body_heliacal_visibility_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL taiyin_calc_star_heliacal_visibility_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_body_heliacal_visibility_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t event_kind,
    double max_search_days,
    uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_call_result TAIYIN_C_CALL
taiyin_search_next_star_heliacal_visibility_ut(
    const taiyin_context* context,
    const char* star_key,
    const taiyin_split_julian_date* jd_start_ut,
    int32_t event_kind,
    double max_search_days,
    uint64_t flags,
    const taiyin_heliacal_visibility_conditions* conditions,
    taiyin_heliacal_visibility_search_result* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
