#include "taiyin/runtime/calc_spec.h"

#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/runtime/runtime.h"

namespace taiyin {
namespace runtime {
namespace {

const uint32_t SUPPORTED_CALC_FLAGS =
    TAIYIN_CALC_FORM_MASK
    | TAIYIN_CALC_FRAME_MASK
    | TAIYIN_CALC_EPOCH_MASK
    | TAIYIN_CALC_DATE_FRAME_MASK
    | TAIYIN_CALC_ORIGIN_MASK
    | TAIYIN_CALC_POSITION_MASK
    | TAIYIN_CALC_RADIANS
    | TAIYIN_CALC_SPEED
    | TAIYIN_CALC_NO_ABERRATION
    | TAIYIN_CALC_NO_DEFLECTION;

bool has_observer_location(const NativeCalcContext& context) noexcept {
    return context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION);
}

bool has_topocentric_offset(const NativeCalcContext& context) noexcept {
    return context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET);
}

Status decode_form(uint32_t flags, CalcOutputForm* out) noexcept {
    switch (flags & TAIYIN_CALC_FORM_MASK) {
    case TAIYIN_CALC_FORM_SPHERICAL:
        *out = CalcOutputSpherical;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_FORM_XYZ:
        *out = CalcOutputCartesian;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status decode_frame(uint32_t flags, CalcFrame* out) noexcept {
    switch (flags & TAIYIN_CALC_FRAME_MASK) {
    case TAIYIN_CALC_FRAME_ECLIPTIC:
        *out = CalcFrameEcliptic;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_FRAME_EQUATOR:
        *out = CalcFrameEquator;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_FRAME_ICRF:
        *out = CalcFrameIcrf;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_FRAME_CIRS:
        *out = CalcFrameCirs;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status decode_epoch(uint32_t flags, CalcEpoch* out) noexcept {
    switch (flags & TAIYIN_CALC_EPOCH_MASK) {
    case TAIYIN_CALC_EPOCH_OF_DATE:
        *out = CalcEpochOfDate;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_EPOCH_J2000:
        *out = CalcEpochJ2000;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status decode_date_frame(uint32_t flags, CalcDateFrame* out) noexcept {
    switch (flags & TAIYIN_CALC_DATE_FRAME_MASK) {
    case TAIYIN_CALC_DATE_FRAME_TRUE:
        *out = CalcDateFrameTrue;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_DATE_FRAME_MEAN:
        *out = CalcDateFrameMean;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status decode_origin(uint32_t flags, CalcOrigin* out) noexcept {
    switch (flags & TAIYIN_CALC_ORIGIN_MASK) {
    case TAIYIN_CALC_ORIGIN_GEOCENTRIC:
        *out = CalcOriginGeocentric;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_ORIGIN_TOPOCENTRIC_SIMPLE:
        *out = CalcOriginTopocentricSimple;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_ORIGIN_TOPOCENTRIC_PRECISE:
        *out = CalcOriginTopocentricPrecise;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_ORIGIN_HELIOCENTRIC:
        *out = CalcOriginHeliocentric;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_ORIGIN_BARYCENTRIC:
        *out = CalcOriginBarycentric;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status decode_position(uint32_t flags, CalcPositionMode* out) noexcept {
    switch (flags & TAIYIN_CALC_POSITION_MASK) {
    case TAIYIN_CALC_POSITION_APPARENT:
        *out = CalcPositionApparent;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_POSITION_ASTROMETRIC:
        *out = CalcPositionAstrometric;
        return TAIYIN_STATUS_OK;
    case TAIYIN_CALC_POSITION_TRUE:
        *out = CalcPositionTrue;
        return TAIYIN_STATUS_OK;
    default:
        return TAIYIN_ERROR_UNSUPPORTED;
    }
}

Status resolve_output_frame(const CalcSpec& spec, int* out_frame) noexcept {
    if (!out_frame) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if (spec.frame == CalcFrameCirs) {
        if (spec.epoch != CalcEpochOfDate || spec.date_frame != CalcDateFrameTrue) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        *out_frame = TAIYIN_APPARENT_FRAME_CIRS;
        return TAIYIN_STATUS_OK;
    }
    if (spec.frame == CalcFrameIcrf) {
        if (spec.date_frame != CalcDateFrameTrue) {
            return TAIYIN_ERROR_UNSUPPORTED;
        }
        *out_frame = TAIYIN_APPARENT_FRAME_ICRF;
        return TAIYIN_STATUS_OK;
    }
    if (spec.epoch == CalcEpochJ2000) {
        *out_frame = spec.frame == CalcFrameEquator
            ? TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR
            : TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC;
        return TAIYIN_STATUS_OK;
    }
    if (spec.date_frame == CalcDateFrameMean) {
        *out_frame = spec.frame == CalcFrameEquator
            ? TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE
            : TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE;
        return TAIYIN_STATUS_OK;
    }
    *out_frame = spec.frame == CalcFrameEquator
        ? TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE
        : TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return TAIYIN_STATUS_OK;
}

uint32_t resolve_apparent_flags(const NativeCalcContext& context, const CalcSpec& spec, uint32_t flags) noexcept {
    uint32_t apparent_flags = context.apparent_options.flags | TAIYIN_APPARENT_SPHERICAL;
    apparent_flags &= ~TAIYIN_APPARENT_ACCELERATION;
    if (spec.speed) {
        apparent_flags |= TAIYIN_APPARENT_VELOCITY;
    } else {
        apparent_flags &= ~TAIYIN_APPARENT_VELOCITY;
    }
    if (spec.form == CalcOutputCartesian) {
        apparent_flags &= ~TAIYIN_APPARENT_SPHERICAL;
    }
    if (spec.observer.use_topocentric_offset) {
        apparent_flags |= TAIYIN_APPARENT_TOPOCENTRIC;
    } else {
        apparent_flags &= ~TAIYIN_APPARENT_TOPOCENTRIC;
    }

    if (spec.position == CalcPositionTrue) {
        apparent_flags &= ~(TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY);
    } else if (spec.position == CalcPositionAstrometric) {
        apparent_flags |= TAIYIN_APPARENT_LIGHT_TIME;
        apparent_flags &= ~(TAIYIN_APPARENT_ABERRATION
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_SHAPIRO_DELAY);
    } else {
        if ((flags & TAIYIN_CALC_NO_ABERRATION) != 0u) {
            apparent_flags &= ~TAIYIN_APPARENT_ABERRATION;
        }
        if ((flags & TAIYIN_CALC_NO_DEFLECTION) != 0u) {
            apparent_flags &= ~TAIYIN_APPARENT_DEFLECTION;
        }
    }
    return apparent_flags;
}

}  // namespace

CalcObserverSpec::CalcObserverSpec() noexcept
    : observer_id(TAIYIN_BODY_EARTH),
      center_id(TAIYIN_BODY_SUN),
      use_topocentric_offset(false),
      origin(CalcOriginGeocentric) {}

CalcSpec::CalcSpec() noexcept
    : form(CalcOutputSpherical),
      frame(CalcFrameEcliptic),
      epoch(CalcEpochOfDate),
      date_frame(CalcDateFrameTrue),
      position(CalcPositionApparent),
      radians(false),
      speed(false),
      observer(),
      apparent_output_frame_id(TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE),
      apparent_flags(TAIYIN_APPARENT_LIGHT_TIME | TAIYIN_APPARENT_SPHERICAL) {}

Status resolve_calc_spec(
    const NativeCalcContext* context,
    uint32_t flags,
    CalcSpec* out
) noexcept {
    if (!context || !out) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    if ((flags & ~SUPPORTED_CALC_FLAGS) != 0u) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    CalcSpec spec;
    Status status = decode_form(flags, &spec.form);
    if (status != TAIYIN_STATUS_OK) return status;
    status = decode_frame(flags, &spec.frame);
    if (status != TAIYIN_STATUS_OK) return status;
    status = decode_epoch(flags, &spec.epoch);
    if (status != TAIYIN_STATUS_OK) return status;
    status = decode_date_frame(flags, &spec.date_frame);
    if (status != TAIYIN_STATUS_OK) return status;
    status = decode_origin(flags, &spec.observer.origin);
    if (status != TAIYIN_STATUS_OK) return status;
    status = decode_position(flags, &spec.position);
    if (status != TAIYIN_STATUS_OK) return status;

    spec.radians = (flags & TAIYIN_CALC_RADIANS) != 0u;
    spec.speed = (flags & TAIYIN_CALC_SPEED) != 0u;
    spec.observer.observer_id = context->observer_id;
    spec.observer.center_id = context->center_id;

    if (spec.observer.origin == CalcOriginTopocentricSimple) {
        if (!has_observer_location(*context) && !has_topocentric_offset(*context)) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        spec.observer.use_topocentric_offset = true;
    } else if (spec.observer.origin == CalcOriginTopocentricPrecise) {
        if (!has_observer_location(*context) || !global_earth_orientation_table()) {
            return TAIYIN_ERROR_INVALID_ARGUMENT;
        }
        spec.observer.use_topocentric_offset = true;
    } else if (spec.observer.origin == CalcOriginHeliocentric || spec.observer.origin == CalcOriginBarycentric) {
        return TAIYIN_ERROR_UNSUPPORTED;
    }

    status = resolve_output_frame(spec, &spec.apparent_output_frame_id);
    if (status != TAIYIN_STATUS_OK) {
        return status;
    }
    spec.apparent_flags = resolve_apparent_flags(*context, spec, flags);

    *out = spec;
    return TAIYIN_STATUS_OK;
}

}  // namespace runtime
}  // namespace taiyin
