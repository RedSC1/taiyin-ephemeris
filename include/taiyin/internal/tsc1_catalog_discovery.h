#ifndef TAIYIN_INTERNAL_TSC1_CATALOG_DISCOVERY_H
#define TAIYIN_INTERNAL_TSC1_CATALOG_DISCOVERY_H

#include "ephemeris_discovery.h"

#include <string>
#include <vector>

namespace taiyin {
namespace internal {

EphemerisDiscoveryStatus discover_tsc1_file(
    const std::string& path,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

void append_tsc1_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept;

bool collect_tsc1_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_TSC1_CATALOG_DISCOVERY_H
