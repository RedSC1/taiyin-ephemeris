#ifndef TAIYIN_RUNTIME_MAJOR_BODY_APPARENT_H
#define TAIYIN_RUNTIME_MAJOR_BODY_APPARENT_H

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/ephemeris_service.h"
#include "taiyin/state.h"
#include "taiyin/status.h"

#include <stddef.h>
#include <stdint.h>

namespace taiyin {
namespace runtime {

const size_t TAIYIN_MAJOR_BODY_COUNT = 10;

const uint32_t TAIYIN_MAJOR_BODY_SUN = 1u << 0;
const uint32_t TAIYIN_MAJOR_BODY_MOON = 1u << 1;
const uint32_t TAIYIN_MAJOR_BODY_MERCURY = 1u << 2;
const uint32_t TAIYIN_MAJOR_BODY_VENUS = 1u << 3;
const uint32_t TAIYIN_MAJOR_BODY_MARS = 1u << 4;
const uint32_t TAIYIN_MAJOR_BODY_JUPITER = 1u << 5;
const uint32_t TAIYIN_MAJOR_BODY_SATURN = 1u << 6;
const uint32_t TAIYIN_MAJOR_BODY_URANUS = 1u << 7;
const uint32_t TAIYIN_MAJOR_BODY_NEPTUNE = 1u << 8;
const uint32_t TAIYIN_MAJOR_BODY_PLUTO = 1u << 9;

const uint32_t TAIYIN_MAJOR_BODY_ALL =
    TAIYIN_MAJOR_BODY_SUN
    | TAIYIN_MAJOR_BODY_MOON
    | TAIYIN_MAJOR_BODY_MERCURY
    | TAIYIN_MAJOR_BODY_VENUS
    | TAIYIN_MAJOR_BODY_MARS
    | TAIYIN_MAJOR_BODY_JUPITER
    | TAIYIN_MAJOR_BODY_SATURN
    | TAIYIN_MAJOR_BODY_URANUS
    | TAIYIN_MAJOR_BODY_NEPTUNE
    | TAIYIN_MAJOR_BODY_PLUTO;

const uint32_t TAIYIN_MAJOR_BODY_APPARENT_LIGHT_TIME = 1u << 0;

struct AstroModelContext {
    int tdb_model_id;
    int precession_model_id;
    int nutation_model_id;
    int obliquity_model_id;
    int frame_route_id;

    AstroModelContext() noexcept;
};

struct ApparentDeflector {
    int body_id;
    double schwarzschild_radius_au;
    double limit;

    ApparentDeflector() noexcept;
};

struct ApparentOptions {
    uint32_t flags;
    int output_frame_id;
    int light_time_method_id;
    int shapiro_delay_model_id;
    int aberration_model_id;
    int deflection_model_id;
    int max_light_time_iterations;
    double light_time_tolerance_days;
    double matrix_derivative_step_days;
    const AstroModelContext* model_context;
    CartesianState observer_offset;
    const ApparentDeflector* deflectors;
    size_t deflector_count;
    int solar_deflector_index;

    ApparentOptions() noexcept;
};

struct MajorBodyApparentBatchRequest {
    double jd_tdb;
    double jd_tt;
    int observer_id;
    int center_id;
    const int* body_ids;
    size_t body_count;
    const ApparentOptions* options;

    MajorBodyApparentBatchRequest() noexcept;
};

struct MajorBodyApparentPosition {
    int body_id;
    uint32_t body_mask_bit;
    TaiyinStatus status;
    EphemerisEvalDiagnostic diagnostic;
    CartesianState geometric_state;
    CartesianState apparent_state;
    double longitude_rad;
    double latitude_rad;
    double distance_au;
    double light_time_days;
    bool cache_hit;

    MajorBodyApparentPosition() noexcept
        : body_id(0),
          body_mask_bit(0),
          status(TAIYIN_STATUS_OK),
          diagnostic(),
          geometric_state(),
          apparent_state(),
          longitude_rad(0.0),
          latitude_rad(0.0),
          distance_au(0.0),
          light_time_days(0.0),
          cache_hit(false) {}
};

struct MajorBodyApparentBatchResult {
    TaiyinStatus status;
    int failed_body_id;
    size_t body_count;
    MajorBodyApparentPosition bodies[TAIYIN_MAJOR_BODY_COUNT];

    MajorBodyApparentBatchResult() noexcept
        : status(TAIYIN_STATUS_OK), failed_body_id(0), body_count(0), bodies() {}
};

int major_body_id_for_mask_bit(uint32_t mask_bit) noexcept;
const char* major_body_name_for_id(int body_id) noexcept;

AstroModelContext get_global_astro_model_context() noexcept;
TaiyinStatus set_global_astro_model_context(const AstroModelContext& context) noexcept;
void reset_global_astro_model_context() noexcept;

ApparentOptions get_global_apparent_options() noexcept;
TaiyinStatus set_global_apparent_options(const ApparentOptions& options) noexcept;
void reset_global_apparent_options() noexcept;

TaiyinStatus set_global_apparent_deflectors(
    const ApparentDeflector* deflectors,
    size_t deflector_count,
    int solar_deflector_index
) noexcept;
size_t get_global_apparent_deflector_count() noexcept;
size_t get_global_apparent_deflectors(
    ApparentDeflector* out,
    size_t capacity,
    int* out_solar_deflector_index
) noexcept;
void reset_global_apparent_deflectors() noexcept;

TaiyinStatus eval_major_body_apparent_batch(
    EphemerisService* service,
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

TaiyinStatus eval_global_major_body_apparent_batch(
    const MajorBodyApparentBatchRequest& request,
    MajorBodyApparentBatchResult* out,
    EphemerisEvalDiagnostic* diagnostic
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_MAJOR_BODY_APPARENT_H
