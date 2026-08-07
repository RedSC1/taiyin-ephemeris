#ifndef TAIYIN_ASTROLOGY_TARGETS_H
#define TAIYIN_ASTROLOGY_TARGETS_H

#include "taiyin/status.h"

namespace taiyin {
namespace astrology {

// These extension-only IDs intentionally live outside NAIF/SPICE and the
// runtime's dynamically assigned catalog-body range. TAIYIN_BODY_LILITH is
// already the physical asteroid 1181 Lilith and is not reused here.
constexpr int TAIYIN_ASTROLOGY_TARGET_TRUE_NODE = -100001;
constexpr int TAIYIN_ASTROLOGY_TARGET_TRUE_DESCENDING_NODE = -100002;
constexpr int TAIYIN_ASTROLOGY_TARGET_MEAN_NODE = -100003;
constexpr int TAIYIN_ASTROLOGY_TARGET_MEAN_DESCENDING_NODE = -100004;
constexpr int TAIYIN_ASTROLOGY_TARGET_MEAN_LILITH = -100005;
constexpr int TAIYIN_ASTROLOGY_TARGET_OSCULATING_LILITH = -100006;
constexpr int TAIYIN_ASTROLOGY_TARGET_FITTED_LILITH = -100007;

// Registers built-in astrology evaluators with runtime::calc_position_*.
// Call once during setup after linking taiyin_astrology_extension and before
// concurrent calculations. Repeated calls are harmless.
Status register_builtin_astrology_targets() noexcept;

}  // namespace astrology
}  // namespace taiyin

#endif  // TAIYIN_ASTROLOGY_TARGETS_H
