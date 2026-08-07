#include "taiyin/internal/spk_catalog_discovery.h"

#include "taiyin/internal/spk.h"
#include "taiyin/internal/path_utils.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const double SPK_DEFAULT_CACHE_BUCKET_DAYS = 32.0;

bool spk_discovery_segment_is_supported(const SpkSegment& segment) noexcept {
    return segment.spk_type == 2 || segment.spk_type == 3 || segment.spk_type == 20 || segment.spk_type == 21;
}

EphemerisFrame frame_from_spk_frame_id(int frame_id) noexcept {
    return frame_id == 1
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

double et_seconds_to_jd_tdb(double et_seconds) noexcept {
    return JD_J2000 + et_seconds / SECONDS_PER_DAY;
}

bool make_spk_descriptor(
    const std::string& path,
    int target_id,
    int center_id,
    int frame_id,
    double start_et_seconds,
    double end_et_seconds,
    uint64_t route_block_id,
    uint64_t source_block_id,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out || path.empty() || target_id == center_id || end_et_seconds <= start_et_seconds) {
        return false;
    }

    const EphemerisFrame frame = frame_from_spk_frame_id(frame_id);
    if (frame == EphemerisFrame::FrameUnknown) {
        return false;
    }

    const double jd_start = et_seconds_to_jd_tdb(start_et_seconds);
    const double jd_end = et_seconds_to_jd_tdb(end_et_seconds);
    if (!std::isfinite(jd_start) || !std::isfinite(jd_end) || jd_end <= jd_start) {
        return false;
    }

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, SPK_METHOD_ID, static_cast<int>(route_block_id));
    descriptor.source_key = EphemerisBlockKey(SPK_SOURCE_ID, source_block_id, 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = SPK_METHOD_ID;
    descriptor.frame = frame;
    descriptor.format = EphemerisBlockFormat::Spk;
    descriptor.jd_tdb_start = jd_start;
    descriptor.jd_tdb_end = jd_end;
    descriptor.path = path;
    descriptor.cache_policy.kind = CacheFixedSpan;
    descriptor.cache_policy.origin_jd = 0.0;
    descriptor.cache_policy.span_days = SPK_DEFAULT_CACHE_BUCKET_DAYS;
    descriptor.cache_policy.first_index = static_cast<int64_t>(
        std::floor(jd_start / SPK_DEFAULT_CACHE_BUCKET_DAYS));
    descriptor.cache_policy.count = static_cast<uint64_t>(
        std::ceil(jd_end / SPK_DEFAULT_CACHE_BUCKET_DAYS)
        - static_cast<double>(descriptor.cache_policy.first_index));
    *out = descriptor;
    return true;
}

bool append_spk_descriptor(
    const std::string& path,
    int target_id,
    int center_id,
    int frame_id,
    double start_et_seconds,
    double end_et_seconds,
    uint64_t source_block_id,
    std::vector<EphemerisBlockDescriptor>* out
) {
    EphemerisBlockDescriptor descriptor;
    if (!make_spk_descriptor(
            path,
            target_id,
            center_id,
            frame_id,
            start_et_seconds,
            end_et_seconds,
            static_cast<uint64_t>(out->size() + 1),
            source_block_id,
            &descriptor)) {
        return false;
    }
    out->push_back(descriptor);
    return true;
}

bool segment_less(const SpkSegment& lhs, const SpkSegment& rhs) noexcept {
    if (lhs.center_id != rhs.center_id) return lhs.center_id < rhs.center_id;
    if (lhs.target_id != rhs.target_id) return lhs.target_id < rhs.target_id;
    if (lhs.start_et_seconds != rhs.start_et_seconds) return lhs.start_et_seconds < rhs.start_et_seconds;
    if (lhs.end_et_seconds != rhs.end_et_seconds) return lhs.end_et_seconds < rhs.end_et_seconds;
    return lhs.summary_index < rhs.summary_index;
}

int preferred_relative_center_for_shared_spk_center(int center_id) noexcept {
    if (center_id == 0) return 10;
    if (center_id == 3) return 399;
    if (center_id >= 5 && center_id <= 9) return center_id * 100 + 99;
    return 0;
}

}  // namespace

EphemerisDiscoveryStatus discover_spk_file(
    const std::string& path,
    const EphemerisDiscoveryOptions&,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || (!has_suffix_case_insensitive(path, ".bsp") && !has_suffix_case_insensitive(path, ".spk"))) {
        return DiscoveryNotApplicable;
    }

    try {
        SpkKernel* kernel = 0;
        if (!compile_spk_kernel_from_file(path, &kernel)) {
            return DiscoveryError;
        }

        std::vector<SpkSegment> segments;
        for (size_t i = 0; i < kernel->index.segments.size(); ++i) {
            const SpkSegment& segment = kernel->index.segments[i];
            if (spk_discovery_segment_is_supported(segment)
                && frame_from_spk_frame_id(segment.frame_id) != EphemerisFrame::FrameUnknown
                && segment.end_et_seconds > segment.start_et_seconds) {
                segments.push_back(segment);
            }
        }
        spk_kernel_destroy(kernel);

        if (segments.empty()) {
            return DiscoveryError;
        }
        std::sort(segments.begin(), segments.end(), segment_less);

        const size_t before = out->size();
        const uint64_t source_block_id = static_cast<uint64_t>(before + 1);
        for (size_t i = 0; i < segments.size(); ++i) {
            const SpkSegment& segment = segments[i];
            if (!append_spk_descriptor(
                    path,
                    segment.target_id,
                    segment.center_id,
                    segment.frame_id,
                    segment.start_et_seconds,
                    segment.end_et_seconds,
                    source_block_id,
                    out)) {
                out->resize(before);
                return DiscoveryError;
            }
        }

        for (size_t i = 0; i < segments.size(); ++i) {
            const SpkSegment& lhs = segments[i];
            for (size_t j = 0; j < segments.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const SpkSegment& rhs = segments[j];
                const int preferred_center = preferred_relative_center_for_shared_spk_center(lhs.center_id);
                if (lhs.center_id != rhs.center_id
                    || lhs.frame_id != rhs.frame_id
                    || lhs.target_id == rhs.target_id
                    || preferred_center == 0
                    || (lhs.target_id != preferred_center && rhs.target_id != preferred_center)) {
                    continue;
                }
                const double start_et = std::max(lhs.start_et_seconds, rhs.start_et_seconds);
                const double end_et = std::min(lhs.end_et_seconds, rhs.end_et_seconds);
                if (end_et <= start_et) {
                    continue;
                }
                if (!append_spk_descriptor(
                        path,
                        lhs.target_id,
                        rhs.target_id,
                        lhs.frame_id,
                        start_et,
                        end_et,
                        source_block_id,
                        out)) {
                    out->resize(before);
                    return DiscoveryError;
                }
            }
        }
    } catch (...) {
        return DiscoveryError;
    }

    return DiscoveryOk;
}

void append_spk_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept {
    if (!out) {
        return;
    }
    try {
        out->push_back(&discover_spk_file);
    } catch (...) {
    }
}

}  // namespace internal
}  // namespace taiyin
