#ifndef TAIYIN_RUNTIME_BUILTIN_BODY_RULES_H
#define TAIYIN_RUNTIME_BUILTIN_BODY_RULES_H

#include "taiyin/runtime/body_registry.h"

namespace taiyin {
namespace runtime {

Status eval_earth_from_emb_moon(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
);

Status eval_moon_from_emb_moon(
    EphemerisEngine* service,
    const EphemerisRequest& request,
    EphemerisResult* out,
    EphemerisEvalDiagnostic* diagnostic
);

bool register_builtin_body_rules(EphemerisBodyRegistry& registry) noexcept;

}  // namespace runtime
}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_BUILTIN_BODY_RULES_H
