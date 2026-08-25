#ifndef TAIYIN_ZIWEI_DEBUG_DUMP_H
#define TAIYIN_ZIWEI_DEBUG_DUMP_H

#include "taiyin/status.h"
#include "taiyin/ziwei/flow_calendar_adapter.h"

#include <cstdint>
#include <vector>

namespace taiyin {
namespace ziwei {

constexpr uint32_t kNumericDumpFormatVersion = 5u;

enum class NumericDumpKind : uint8_t {
    Chart = 1,
    ResolvedFlow = 2,
};

// Produces a deterministic, label-free sequence for differential tests and
// bindings. The first two values are kNumericDumpFormatVersion and kind.
// Star positions use -1 for a star absent from that layer. The exact field
// order is documented in docs/numeric-dump.md and is versioned independently
// of the C++ ABI.
Status dump_chart_numeric(
    const Chart& chart,
    std::vector<int64_t>* out
) noexcept;

Status dump_resolved_flow_numeric(
    const ResolvedFlow& flow,
    std::vector<int64_t>* out
) noexcept;

}  // namespace ziwei
}  // namespace taiyin

#endif  // TAIYIN_ZIWEI_DEBUG_DUMP_H
