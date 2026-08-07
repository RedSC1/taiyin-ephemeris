#ifndef TAIYIN_C_PHENOMENA_H
#define TAIYIN_C_PHENOMENA_H

#include "taiyin/c/base.h"
#include "taiyin/c/context.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct taiyin_body_phenomena {
    uint32_t struct_size;
    double phase_angle_rad;
    double illuminated_fraction;
    double solar_elongation_rad;
    double apparent_diameter_rad;
    double apparent_magnitude;
    double horizontal_parallax_rad;
} taiyin_body_phenomena;

TAIYIN_C_API void TAIYIN_C_CALL taiyin_body_phenomena_init(
    taiyin_body_phenomena* value
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_body_phenomena_tt(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_tt,
    uint64_t flags,
    taiyin_body_phenomena* out,
    taiyin_ephemeris_diagnostic* diagnostic
);
TAIYIN_C_API taiyin_status TAIYIN_C_CALL taiyin_calc_body_phenomena_ut(
    const taiyin_context* context,
    int32_t body_id,
    const taiyin_split_julian_date* jd_ut,
    uint64_t flags,
    taiyin_body_phenomena* out,
    taiyin_ephemeris_diagnostic* diagnostic
);

#ifdef __cplusplus
}
#endif

#endif
