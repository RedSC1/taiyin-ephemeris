#ifndef TAIYIN_INTERNAL_OPM4_CATALOG_DISCOVERY_H
#define TAIYIN_INTERNAL_OPM4_CATALOG_DISCOVERY_H

#include "ephemeris_discovery.h"

#include <string>
#include <vector>

namespace taiyin {
namespace internal {

EphemerisDiscoveryStatus discover_opm4_file(
    const std::string& path,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

void append_opm4_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept;

bool collect_opm4_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

bool discover_opm4_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_OPM4_CATALOG_DISCOVERY_H
