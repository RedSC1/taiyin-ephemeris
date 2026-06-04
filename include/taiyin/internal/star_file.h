#ifndef TAIYIN_INTERNAL_STAR_FILE_H
#define TAIYIN_INTERNAL_STAR_FILE_H

#include "ephemeris_discovery.h"
#include "taiyin/star_catalog_tsc1.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const char TAIYIN_USER_STAR_RELATIVE_DIRECTORY[] = "stars/user";
const uint64_t TAIYIN_STAR_FILE_SOURCE_ID = 7;
const uint32_t TAIYIN_STAR_FILE_GENERATION = 1;
const uint32_t TAIYIN_STAR_FILE_PURPOSE = 0;

struct Tsf1StarEntry {
    std::string id;
    std::string name;
    std::vector<std::string> aliases;
    double magnitude;
    double ra_deg;
    double dec_deg;
    double pm_ra_mas_yr;
    double pm_dec_mas_yr;
    double parallax_mas;
    double radial_velocity_km_s;
    double reference_epoch;

    Tsf1StarEntry();
};

std::string default_user_star_directory() noexcept;

std::string make_user_star_directory(
    const std::string& data_root
) noexcept;

std::string make_user_star_file_path(
    const std::string& data_root,
    const std::string& label
) noexcept;

bool save_user_star_file(
    const std::string& data_root,
    const std::string& label,
    const Tsf1StarEntry* entries,
    size_t entry_count,
    std::string* out_path
) noexcept;

bool save_star_file(
    const std::string& path,
    const Tsf1StarEntry* entries,
    size_t entry_count
) noexcept;

bool load_star_file(
    const std::string& path,
    std::vector<Tsf1StarEntry>* out_entries
) noexcept;

bool build_tsc1_catalog_bytes_from_star_entries(
    const Tsf1StarEntry* entries,
    size_t entry_count,
    std::vector<uint8_t>* out_bytes
) noexcept;

bool build_tsc1_catalog_bytes_from_star_file(
    const std::string& path,
    std::vector<uint8_t>* out_bytes
) noexcept;

EphemerisDiscoveryStatus discover_star_file(
    const std::string& path,
    const EphemerisDiscoveryOptions& options,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

void append_star_file_discoverer(
    std::vector<EphemerisDiscoverFileFn>* out
) noexcept;

bool collect_star_file_descriptors_from_directory(
    const std::string& root,
    std::vector<EphemerisBlockDescriptor>* out
) noexcept;

bool discover_star_file_catalog_from_directory(
    const std::string& root,
    EphemerisBlockCatalog* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_STAR_FILE_H
