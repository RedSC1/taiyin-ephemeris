#ifndef TAIYIN_INTERNAL_SPK_CATALOG_DISCOVERY_H
#define TAIYIN_INTERNAL_SPK_CATALOG_DISCOVERY_H

#include "ephemeris_discovery.h"

#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const int SPK_METHOD_ID = 2;
const uint64_t SPK_SOURCE_ID = 2;

EphemerisDiscoveryStatus discover_spk_file(
    const std::string& path,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

void append_spk_ephemeris_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_SPK_CATALOG_DISCOVERY_H
