#ifndef TAIYIN_INTERNAL_KEPLER_FILE_H
#define TAIYIN_INTERNAL_KEPLER_FILE_H

#include "ephemeris_catalog.h"
#include "kepler.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

const int TAIYIN_KEPLER_FILE_METHOD_ID = 3001;
const uint64_t TAIYIN_KEPLER_FILE_SOURCE_ID = 4;
const uint32_t TAIYIN_KEPLER_FILE_GENERATION = 1;
const uint32_t TAIYIN_KEPLER_FILE_PURPOSE = 0;

const char TAIYIN_DEFAULT_DATA_DIRECTORY[] = "data";
const char TAIYIN_USER_KEPLER_RELATIVE_DIRECTORY[] = "kepler/user";

std::string default_user_kepler_directory() noexcept;

std::string make_user_kepler_directory(
    const std::string& data_root
) noexcept;

std::string make_user_kepler_file_path(
    const std::string& data_root,
    int target_id,
    const std::string& label
) noexcept;

bool save_user_kepler_file(
    const std::string& data_root,
    const std::string& label,
    const KeplerElements* elements,
    size_t element_count,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    std::string* out_path
) noexcept;

bool save_kepler_file(
    const std::string& path,
    const KeplerElements* elements,
    size_t element_count,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end
) noexcept;

bool load_kepler_file(
    const std::string& path,
    std::vector<KeplerElements>* out_elements,
    EphemerisBlockDescriptor* out_descriptor
) noexcept;

bool load_kepler_file_with_block_id(
    const std::string& path,
    uint64_t block_id,
    std::vector<KeplerElements>* out_elements,
    EphemerisBlockDescriptor* out_descriptor
) noexcept;

bool compile_kepler_file(
    const std::string& path,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_KEPLER_FILE_H
