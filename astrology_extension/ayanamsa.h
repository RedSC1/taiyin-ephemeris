#ifndef TAIYIN_AYANAMSA_H
#define TAIYIN_AYANAMSA_H

#include "taiyin/coordinates.h"
#include "taiyin/internal/ephemeris_block.h"

namespace taiyin {

enum AyanamsaId {
    AYANAMSA_FAGAN_BRADLEY = 0,
    AYANAMSA_LAHIRI = 1,
    AYANAMSA_RAMAN = 3,
    AYANAMSA_KRISHNAMURTI = 5,
    AYANAMSA_TRUE_CHITRAPAKSHA = 27,
    AYANAMSA_GALACTIC_CENTER = 17
};

enum AstrologyPrecessionModelId {
    PRECESSION_ASTROLOGY_START = 900,
    PRECESSION_NEWCOMB_B1900 = PRECESSION_ASTROLOGY_START,
    PRECESSION_LINEAR = 901
};

// 1. Definition of Ayanamsa function pointer, aligning with core PrecessionFn / NutationFn
// - jd_tt: target epoch Julian Date in TT
// - data: optional context pointer for specific model parameters (e.g. precession matrix, obliquity, star block)
// - out_ayanamsa_deg: output parameter for the calculated ayanamsa offset in degrees
typedef bool (*AyanamsaFn)(double jd_tt, const void* data, double* out_ayanamsa_deg);

// 2. Registry management interfaces
void register_ayanamsa_model(int id, AyanamsaFn fn);
bool eval_ayanamsa(int id, double jd_tt, const void* data, double* out_ayanamsa_deg);

// 3. Evaluation parameter carrier aligning with core DispatchData convention
struct AyanamsaDispatchData {
    int precession_model_id;                              // Which precession model to use (e.g. IAU2006, PRECESSION_NEWCOMB_B1900)
    double true_obliquity_rad;                            // Obliquity of date
    const internal::CompiledEphemerisBlock* dynamic_block; // Compiled block for the dynamic target star
};

}  // namespace taiyin

#endif  // TAIYIN_AYANAMSA_H
