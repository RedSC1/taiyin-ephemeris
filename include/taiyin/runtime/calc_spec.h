#ifndef TAIYIN_RUNTIME_CALC_SPEC_H
#define TAIYIN_RUNTIME_CALC_SPEC_H

#include "taiyin/runtime/native_context.h"
#include "taiyin/status.h"

#include <stdint.h>

namespace taiyin {
namespace runtime {

const uint32_t TAIYIN_CALC_FORM_MASK = 0x00000003u;
const uint32_t TAIYIN_CALC_FORM_SPHERICAL = 0u << 0;
const uint32_t TAIYIN_CALC_FORM_XYZ = 1u << 0;

const uint32_t TAIYIN_CALC_FRAME_MASK = 0x0000001cu;
const uint32_t TAIYIN_CALC_FRAME_ECLIPTIC = 0u << 2;
const uint32_t TAIYIN_CALC_FRAME_EQUATOR = 1u << 2;
const uint32_t TAIYIN_CALC_FRAME_ICRF = 2u << 2;
const uint32_t TAIYIN_CALC_FRAME_CIRS = 3u << 2;

const uint32_t TAIYIN_CALC_EPOCH_MASK = 0x00000060u;
const uint32_t TAIYIN_CALC_EPOCH_OF_DATE = 0u << 5;
const uint32_t TAIYIN_CALC_EPOCH_J2000 = 1u << 5;

const uint32_t TAIYIN_CALC_DATE_FRAME_MASK = 0x00000180u;
const uint32_t TAIYIN_CALC_DATE_FRAME_TRUE = 0u << 7;
const uint32_t TAIYIN_CALC_DATE_FRAME_MEAN = 1u << 7;

const uint32_t TAIYIN_CALC_ORIGIN_MASK = 0x00000e00u;
const uint32_t TAIYIN_CALC_ORIGIN_GEOCENTRIC = 0u << 9;
const uint32_t TAIYIN_CALC_ORIGIN_TOPOCENTRIC_SIMPLE = 1u << 9;
const uint32_t TAIYIN_CALC_ORIGIN_TOPOCENTRIC_PRECISE = 2u << 9;
const uint32_t TAIYIN_CALC_ORIGIN_HELIOCENTRIC = 3u << 9;
const uint32_t TAIYIN_CALC_ORIGIN_BARYCENTRIC = 4u << 9;

const uint32_t TAIYIN_CALC_POSITION_MASK = 0x00003000u;
const uint32_t TAIYIN_CALC_POSITION_APPARENT = 0u << 12;
const uint32_t TAIYIN_CALC_POSITION_ASTROMETRIC = 1u << 12;
const uint32_t TAIYIN_CALC_POSITION_TRUE = 2u << 12;

const uint32_t TAIYIN_CALC_RADIANS = 1u << 14;
const uint32_t TAIYIN_CALC_SPEED = 1u << 15;
const uint32_t TAIYIN_CALC_NO_ABERRATION = 1u << 16;
const uint32_t TAIYIN_CALC_NO_DEFLECTION = 1u << 17;

enum CalcOutputForm {
    CalcOutputSpherical,
    CalcOutputCartesian,
};

enum CalcFrame {
    CalcFrameEcliptic,
    CalcFrameEquator,
    CalcFrameIcrf,
    CalcFrameCirs,
};

enum CalcEpoch {
    CalcEpochOfDate,
    CalcEpochJ2000,
};

enum CalcDateFrame {
    CalcDateFrameTrue,
    CalcDateFrameMean,
};

enum CalcOrigin {
    CalcOriginGeocentric,
    CalcOriginTopocentricSimple,
    CalcOriginTopocentricPrecise,
    CalcOriginHeliocentric,
    CalcOriginBarycentric,
};

enum CalcPositionMode {
    CalcPositionApparent,
    CalcPositionAstrometric,
    CalcPositionTrue,
};

struct CalcObserverSpec {
    int observer_id;
    int center_id;
    bool use_topocentric_offset;
    CalcOrigin origin;

    CalcObserverSpec() noexcept;
};

struct CalcSpec {
    CalcOutputForm form;
    CalcFrame frame;
    CalcEpoch epoch;
    CalcDateFrame date_frame;
    CalcPositionMode position;
    bool radians;
    bool speed;
    CalcObserverSpec observer;
    int apparent_output_frame_id;
    uint32_t apparent_flags;

    CalcSpec() noexcept;
};

Status resolve_calc_spec(
    const NativeCalcContext* context,
    uint32_t flags,
    CalcSpec* out
) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_CALC_SPEC_H
