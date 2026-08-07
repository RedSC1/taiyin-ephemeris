#ifndef TAIYIN_INTERNAL_EPHEMERIS_DISCOVERY_H
#define TAIYIN_INTERNAL_EPHEMERIS_DISCOVERY_H

#include "ephemeris_catalog.h"

#include <string>
#include <vector>

namespace taiyin {
namespace internal {

struct EphemerisDiscoveryOptions {
    bool strict;

    EphemerisDiscoveryOptions()
        : strict(false) {}
};

enum EphemerisDiscoveryStatus {
    DiscoveryNotApplicable,
    DiscoveryOk,
    DiscoveryError,
};

typedef EphemerisDiscoveryStatus (*EphemerisDiscoverFileFn)(
    const std::string& path,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
);

void append_builtin_ephemeris_discoverers(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept;

bool cache_bucket_id_for_jd(
    const EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    int* out_bucket_id
) noexcept;

bool make_cache_bucket_descriptor_for_jd(
    const EphemerisBlockDescriptor& source,
    const SplitJulianDate& jd_tdb,
    EphemerisBlockDescriptor* out
) noexcept;

bool discover_ephemeris_descriptors_from_directory(
    const std::string& root,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

bool discover_ephemeris_catalog_from_directory(
    const std::string& root,
    const std::vector<EphemerisDiscoverFileFn>& discoverers,
    const EphemerisDiscoveryOptions& options,
    EphemerisBlockCatalog* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_DISCOVERY_H
