#ifndef TAIYIN_C_API_CHINESE_CALENDAR_CONTEXT_INTERNAL_H
#define TAIYIN_C_API_CHINESE_CALENDAR_CONTEXT_INTERNAL_H

#include "taiyin/chinese_calendar/calendar.h"

#include <cstdint>

struct taiyin_chinese_calendar_context {
    taiyin::chinese_calendar::ChineseCalendarContext value;
};

namespace taiyin_c_internal {

// Calendar-operation counterpart of TrackedCalcContext: owns the operation
// flags word and tracker, and a context copy whose astronomy half observes
// into them.  The ChineseCalendarContext copy constructor already reseats
// the model_context self-pointer.
struct TrackedCalendarContext {
    uint32_t flags = 0u;
    taiyin::SourceSwitchTracker tracker{&flags};
    taiyin::chinese_calendar::ChineseCalendarContext value;

    explicit TrackedCalendarContext(
        const taiyin::chinese_calendar::ChineseCalendarContext& source
    ) noexcept
        : value(source) {
        value.astronomy.source_tracker = &tracker;
    }

    TrackedCalendarContext(const TrackedCalendarContext&) = delete;
    TrackedCalendarContext& operator=(const TrackedCalendarContext&) = delete;
};

}  // namespace taiyin_c_internal

#endif
