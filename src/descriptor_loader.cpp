#include "taiyin/internal/descriptor_loader.h"

#include "taiyin/internal/custom_ephemeris_method.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/kepler.h"
#include "taiyin/internal/kepler_catalog_tkc1.h"
#include "taiyin/internal/opm2.h"
#include "taiyin/internal/kepler_file.h"
#include "taiyin/internal/semi_analytic.h"
#include "taiyin/internal/spk.h"

#include <limits>
#include <memory>
#include <vector>

namespace taiyin {
namespace internal {

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    const EphemerisSourceIndex* source_index,
    StorageEphemerisBlock* out
) noexcept {
    if (!out || descriptor.jd_tdb_end <= descriptor.jd_tdb_start) {
        return false;
    }

    switch (descriptor.format) {
        case EphemerisBlockFormat::Opm2:
            {
                if (source_index
                    && source_index->format == EphemerisBlockFormat::Opm2
                    && source_index->source_key == descriptor.source_key
                    && source_index->payload) {
                    const EphemerisFileView* bytes =
                        static_cast<const EphemerisFileView*>(source_index->payload.get());
                    if (!bytes || !bytes->is_open() || !bytes->is_mapped()) {
                        return false;
                    }
                    EphemerisBlockCompileOptions options;
                    options.has_required_jd_tdb_range = true;
                    options.required_jd_tdb_start = descriptor.jd_tdb_start;
                    options.required_jd_tdb_end = descriptor.jd_tdb_end;
                    return compile_ephemeris_block(bytes->data(), bytes->size(), &options, out);
                }

                if (descriptor.path.empty()) {
                    return false;
                }
                EphemerisFileView bytes;
                Opm2EpheSection ephe;
                Opm2GridSection grid;
                if (!bytes.open_readonly(descriptor.path)
                    || !parse_opm2_summary(bytes.data(), bytes.size(), &ephe, &grid)
                    || ephe.target_id != descriptor.target_id
                    || ephe.center_id != descriptor.center_id) {
                    return false;
                }
                EphemerisBlockCompileOptions options;
                options.has_required_jd_tdb_range = true;
                options.required_jd_tdb_start = descriptor.jd_tdb_start;
                options.required_jd_tdb_end = descriptor.jd_tdb_end;
                return compile_ephemeris_block(bytes.data(), bytes.size(), &options, out);
            }

        case EphemerisBlockFormat::Spk:
            if (source_index
                && source_index->format == EphemerisBlockFormat::Spk
                && source_index->source_key == descriptor.source_key
                && source_index->payload) {
                const SpkKernel* kernel = static_cast<const SpkKernel*>(source_index->payload.get());
                return kernel
                    && compile_spk_ephemeris_block_from_kernel(
                        *kernel,
                        descriptor.target_id,
                        descriptor.center_id,
                        descriptor.jd_tdb_start,
                        descriptor.jd_tdb_end,
                        out);
            }
            if (descriptor.path.empty()) {
                return false;
            }
            return compile_spk_ephemeris_block_from_file(
                descriptor.path,
                descriptor.target_id,
                descriptor.center_id,
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::Kepler:
            if (source_index
                && source_index->format == EphemerisBlockFormat::Kepler
                && source_index->source_key == descriptor.source_key
                && source_index->payload) {
                const std::vector<KeplerElements>* elements =
                    static_cast<const std::vector<KeplerElements>*>(source_index->payload.get());
                return elements
                    && !elements->empty()
                    && compile_kepler_ephemeris_block(
                        &(*elements)[0],
                        elements->size(),
                        descriptor.jd_tdb_start,
                        descriptor.jd_tdb_end,
                        out);
            }
            if (descriptor.path.empty()) {
                return false;
            }
            return compile_kepler_file(
                descriptor.path,
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::SemiAnalytic:
            return compile_builtin_semi_analytic_ephemeris_block(
                descriptor.target_id,
                descriptor.center_id,
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::Tkc1:
            if (descriptor.path.empty()) {
                return false;
            }
            if (descriptor.object_index > static_cast<uint32_t>(std::numeric_limits<uint32_t>::max())) {
                return false;
            }
            return tkc1_compile_object_storage_block_from_file(
                descriptor.path,
                descriptor.object_index,
                descriptor.jd_tdb_start,
                descriptor.jd_tdb_end,
                out);

        case EphemerisBlockFormat::Custom:
            return load_custom_ephemeris_method_block(descriptor, out);

        default:
            return false;
    }
}

bool load_descriptor_ephemeris_block(
    const EphemerisBlockDescriptor& descriptor,
    StorageEphemerisBlock* out
) noexcept {
    return load_descriptor_ephemeris_block(descriptor, 0, out);
}

bool load_descriptor_source_index(
    const EphemerisBlockDescriptor& descriptor,
    EphemerisSourceIndex* out
) noexcept {
    if (out) {
        *out = EphemerisSourceIndex();
    }
    if (!out || descriptor.path.empty() || descriptor.source_key.source_id == 0) {
        return false;
    }

    try {
        EphemerisSourceIndex index;
        index.source_key = descriptor.source_key;
        index.format = descriptor.format;
        index.path = descriptor.path;

        switch (descriptor.format) {
            case EphemerisBlockFormat::Opm2:
                {
                    std::shared_ptr<EphemerisFileView> bytes(new EphemerisFileView());
                    Opm2EpheSection ephe;
                    Opm2GridSection grid;
                    if (!bytes->open_readonly(descriptor.path)
                        || !bytes->is_mapped()
                        || !parse_opm2_summary(bytes->data(), bytes->size(), &ephe, &grid)
                        || ephe.target_id != descriptor.target_id
                        || ephe.center_id != descriptor.center_id) {
                        return false;
                    }
                    index.byte_count = bytes->size();
                    index.payload = bytes;
                    *out = index;
                    return true;
                }

            case EphemerisBlockFormat::Spk:
                {
                    SpkKernel* kernel = 0;
                    if (!compile_spk_kernel_from_file(descriptor.path, &kernel)) {
                        return false;
                    }
                    index.byte_count = kernel->source.byte_count;
                    index.payload = std::shared_ptr<void>(kernel, spk_kernel_destroy_void);
                    *out = index;
                    return true;
                }

            case EphemerisBlockFormat::Kepler:
                {
                    std::shared_ptr<std::vector<KeplerElements> > elements(new std::vector<KeplerElements>());
                    EphemerisBlockDescriptor parsed;
                    if (!load_kepler_file_with_block_id(
                            descriptor.path,
                            descriptor.source_key.block_id,
                            elements.get(),
                            &parsed)
                        || elements->empty()
                        || parsed.target_id != descriptor.target_id
                        || parsed.center_id != descriptor.center_id
                        || parsed.method_id != descriptor.method_id
                        || parsed.frame != descriptor.frame) {
                        return false;
                    }
                    index.byte_count = elements->size() * sizeof(KeplerElements);
                    index.payload = elements;
                    *out = index;
                    return true;
                }

            case EphemerisBlockFormat::Tkc1:
                {
                    // TKC1 needs a lightweight source index (no payload) so
                    // assign_runtime_catalog_source_keys can detect cross-batch
                    // file collisions via find_source_index. The loader reads the
                    // whole file and uses object_index to locate the object.
                    index.byte_count = 0;
                    index.payload.reset();
                    *out = index;
                    return true;
                }

            default:
                return false;
        }
    } catch (...) {
        if (out) {
            *out = EphemerisSourceIndex();
        }
        return false;
    }
}

}  // namespace internal
}  // namespace taiyin
