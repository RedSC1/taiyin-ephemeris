#ifndef TAIYIN_RUNTIME_SOURCE_SWITCH_TRACKER_H
#define TAIYIN_RUNTIME_SOURCE_SWITCH_TRACKER_H

#include "taiyin/status.h"

#include <stdint.h>
#include <unordered_map>

namespace taiyin {

// Tracks the winning ephemeris source id per (target, center) pair across the
// evaluations of one search operation.  When a repeated pair is served by a
// different source id than its previous evaluation, the operation's flags
// word gains kResultFlagFallbackOccurred: an iterative solver (eclipse,
// solar term, new moon) bracketing on those values has crossed a source
// boundary, which is a continuity hazard regardless of which source is more
// precise.
struct SourceSwitchTracker {
    explicit SourceSwitchTracker(uint32_t* operation_flags) noexcept
        : flags(operation_flags) {}

    void observe(int target_id, int center_id, uint64_t source_id) noexcept {
        if (!flags || source_id == 0) {
            return;
        }
        const uint64_t key =
            (static_cast<uint64_t>(static_cast<uint32_t>(target_id)) << 32)
            | static_cast<uint32_t>(center_id);
        try {
            const auto inserted = last_source_ids.emplace(key, source_id);
            if (!inserted.second && inserted.first->second != source_id) {
                *flags |= kResultFlagFallbackOccurred;
                inserted.first->second = source_id;
            }
        } catch (...) {
            // An allocation failure must never fail the underlying evaluation.
        }
    }

    uint32_t* flags;
    std::unordered_map<uint64_t, uint64_t> last_source_ids;
};

}  // namespace taiyin

#endif  // TAIYIN_RUNTIME_SOURCE_SWITCH_TRACKER_H
