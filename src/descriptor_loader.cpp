#include "taiyin/internal/descriptor_loader.h"

#include "taiyin/internal/custom_ephemeris_source_registry.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/kepler_source_registry.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/internal/spk.h"

#include <limits>
#include <vector>

namespace taiyin {
namespace internal {

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept {
    if (!out || descriptor.jd_tdb_end <= descriptor.jd_tdb_start
        || (descriptor.path.empty()
            && descriptor.format != EphemerisBlockFormat::Kepler
            && descriptor.format != EphemerisBlockFormat::Custom)) {
        return false;
    }

    switch (descriptor.format) {
        case EphemerisBlockFormat::Opm4: {
            std::vector<uint8_t> bytes;
            OPM4Header header;
            if (!load_ephemeris_file_bytes(descriptor.path, &bytes)
                || bytes.empty()
                || !parse_opm4_header(&bytes[0], bytes.size(), &header)
                || header.target_id != descriptor.target_id
                || header.center_id != descriptor.center_id
                || header.method_id != descriptor.method_id
                || header.frame_id != static_cast<int>(descriptor.frame)
                || descriptor.jd_tdb_start < header.jd_tdb_start - 1e-9
                || descriptor.jd_tdb_end > header.jd_tdb_end + 1e-9) {
                return false;
            }

            EphemerisBlockCompileOptions options;
            options.has_required_jd_tdb_range = true;
            options.required_jd_tdb_start = descriptor.jd_tdb_start;
            options.required_jd_tdb_end = descriptor.jd_tdb_end;
            return compile_ephemeris_block(&bytes[0], bytes.size(), &options, out);
        }

        case EphemerisBlockFormat::Spk:
            return compile_spk_ephemeris_block_from_file(
                descriptor.path,
                descriptor.target_id,
                descriptor.center_id,
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::Kepler:
            if (!descriptor.path.empty()) {
                return compile_kepler_file(
                    descriptor.path,
                    descriptor.jd_tdb_start,
                    descriptor.jd_tdb_end,
                    out);
            }
            return compile_memory_kepler_source(descriptor, out);

        case EphemerisBlockFormat::Tkc1:
            if (descriptor.source_key.block_id > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            return tkc1_compile_object_storage_block_from_file(
                descriptor.path,
                static_cast<uint32_t>(descriptor.source_key.block_id),
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::Custom:
            return compile_custom_ephemeris_source(descriptor, out);

        default:
            return false;
    }
}

bool load_descriptor_into_cache(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache
) noexcept {
    return load_descriptor_into_cache(descriptor, cache, 0);
}

bool load_descriptor_into_cache(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    int priority_bias
) noexcept {
    if (!cache) {
        return false;
    }

    StorageEphemerisBlock storage;
    if (!load_descriptor_ephemeris_block(descriptor, &storage)) {
        return false;
    }

    const bool inserted = cache->insert(
        descriptor.route_key,
        descriptor.jd_tdb_start,
        descriptor.jd_tdb_end,
        &storage,
        0.0,
        priority_bias);
    if (!inserted) {
        destroy_storage_ephemeris_block(&storage);
    }
    return inserted;
}

bool eval_descriptor_position(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept {
    if (!cache || !out) {
        return false;
    }
    EphemerisBlockDescriptor bucket_descriptor;
    if (!make_cache_bucket_descriptor_for_jd(descriptor, jd_tdb, &bucket_descriptor)) {
        return false;
    }
    if (cache->eval_position(bucket_descriptor.route_key, jd_tdb, out)) {
        return true;
    }
    if (!load_descriptor_into_cache(bucket_descriptor, cache)) {
        return false;
    }
    return cache->eval_position(bucket_descriptor.route_key, jd_tdb, out);
}

bool eval_descriptor_velocity(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept {
    if (!cache || !out) {
        return false;
    }
    EphemerisBlockDescriptor bucket_descriptor;
    if (!make_cache_bucket_descriptor_for_jd(descriptor, jd_tdb, &bucket_descriptor)) {
        return false;
    }
    if (cache->eval_velocity(bucket_descriptor.route_key, jd_tdb, out)) {
        return true;
    }
    if (!load_descriptor_into_cache(bucket_descriptor, cache)) {
        return false;
    }
    return cache->eval_velocity(bucket_descriptor.route_key, jd_tdb, out);
}

bool eval_descriptor_acceleration(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    Vector3* out
) noexcept {
    if (!cache || !out) {
        return false;
    }
    EphemerisBlockDescriptor bucket_descriptor;
    if (!make_cache_bucket_descriptor_for_jd(descriptor, jd_tdb, &bucket_descriptor)) {
        return false;
    }
    if (cache->eval_acceleration(bucket_descriptor.route_key, jd_tdb, out)) {
        return true;
    }
    if (!load_descriptor_into_cache(bucket_descriptor, cache)) {
        return false;
    }
    return cache->eval_acceleration(bucket_descriptor.route_key, jd_tdb, out);
}

bool eval_descriptor_state(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisBlockCache* cache,
    double jd_tdb,
    CartesianState* out
) noexcept {
    if (!cache || !out) {
        return false;
    }
    EphemerisBlockDescriptor bucket_descriptor;
    if (!make_cache_bucket_descriptor_for_jd(descriptor, jd_tdb, &bucket_descriptor)) {
        return false;
    }
    if (cache->eval_state(bucket_descriptor.route_key, jd_tdb, out)) {
        return true;
    }
    if (!load_descriptor_into_cache(bucket_descriptor, cache)) {
        return false;
    }
    return cache->eval_state(bucket_descriptor.route_key, jd_tdb, out);
}

}  // namespace internal
}  // namespace taiyin
