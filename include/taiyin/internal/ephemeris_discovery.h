#ifndef TAIYIN_INTERNAL_EPHEMERIS_DISCOVERY_H
#define TAIYIN_INTERNAL_EPHEMERIS_DISCOVERY_H

#include "ephemeris_catalog.h"
#include "taiyin/time.h"

#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const double EPHEMERIS_DISCOVERY_J2000_JD = JD_J2000;
const double EPHEMERIS_DISCOVERY_JULIAN_CENTURY_DAYS = DAYS_PER_JULIAN_CENTURY;
const double EPHEMERIS_DISCOVERY_TEN_JULIAN_YEARS_DAYS = 10.0 * DAYS_PER_JULIAN_YEAR;

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

bool make_cache_bucket_descriptor_for_jd(
    const EphemerisBlockDescriptor& source,
    double jd_tdb,
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
