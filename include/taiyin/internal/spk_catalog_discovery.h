#ifndef TAIYIN_INTERNAL_SPK_CATALOG_DISCOVERY_H
#define TAIYIN_INTERNAL_SPK_CATALOG_DISCOVERY_H

#include "ephemeris_discovery.h"
#include "ephemeris_source_identity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const int SPK_METHOD_ID = 2;
// Kept as a compatibility name for unclassified external SPK files.
const uint64_t SPK_SOURCE_ID = SPK_SOURCE_EXTERNAL;

// Physical primary carried beside satellites in a system-barycenter SPK.
// Zero means the shared center has no supported primary-relative synthesis.
int spk_physical_primary_for_shared_center(int center_id) noexcept;

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
