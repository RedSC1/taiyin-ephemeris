#ifndef TAIYIN_INTERNAL_EPHEMERIS_SOURCE_IDENTITY_H
#define TAIYIN_INTERNAL_EPHEMERIS_SOURCE_IDENTITY_H

#include <cstdint>
#include <string>

namespace taiyin {
namespace internal {

// `format` identifies a container such as SPK or OPM2. `source_id` instead
// identifies the product family whose routes may safely be selected together.
// Values below are stable Taiyin-reserved identities. Unknown user files keep
// the legacy external identities and remain fully usable.
//
// OPM2 stores its product id as a little-endian uint32 in header bytes 24..27.
// Zero means unspecified in the file format. Version-1 files created before the
// field was assigned used zero there and are treated as Taiyin prerelease data
// by the runtime for backward compatibility.
const uint32_t OPM2_SOURCE_UNDEFINED = 0;
const uint32_t OPM2_SOURCE_TAIYIN_PRERELEASE = 1;
const uint32_t OPM2_SOURCE_TAIYIN_DE442_REBUILT = 2;
const uint64_t OPM2_SOURCE_LEGACY = OPM2_SOURCE_TAIYIN_PRERELEASE;
const uint64_t SPK_SOURCE_EXTERNAL = 2;

const uint64_t SPK_SOURCE_JPL_DE431 = 0x53504b00000001afULL;
const uint64_t SPK_SOURCE_JPL_DE440 = 0x53504b00000001b8ULL;
const uint64_t SPK_SOURCE_JPL_DE441 = 0x53504b00000001b9ULL;
const uint64_t SPK_SOURCE_JPL_DE442 = 0x53504b00000001baULL;
const uint64_t SPK_SOURCE_JPL_DE438 = 0x53504b00000001b6ULL;
const uint64_t SPK_SOURCE_JPL_DE435 = 0x53504b00000001b3ULL;
const uint64_t SPK_SOURCE_JPL_DE432 = 0x53504b00000001b0ULL;
const uint64_t SPK_SOURCE_JPL_DE430 = 0x53504b00000001aeULL;
const uint64_t SPK_SOURCE_JPL_DE423 = 0x53504b00000001a7ULL;
const uint64_t SPK_SOURCE_JPL_DE421 = 0x53504b00000001a5ULL;
const uint64_t SPK_SOURCE_JPL_DE418 = 0x53504b00000001a2ULL;
const uint64_t SPK_SOURCE_JPL_DE414 = 0x53504b000000019eULL;
const uint64_t SPK_SOURCE_JPL_DE413 = 0x53504b000000019dULL;
const uint64_t SPK_SOURCE_JPL_DE410 = 0x53504b000000019aULL;
const uint64_t SPK_SOURCE_JPL_DE408 = 0x53504b0000000198ULL;
const uint64_t SPK_SOURCE_JPL_DE406 = 0x53504b0000000196ULL;
const uint64_t SPK_SOURCE_JPL_DE405 = 0x53504b0000000195ULL;
const uint64_t SPK_SOURCE_JPL_DE403 = 0x53504b0000000193ULL;
const uint64_t SPK_SOURCE_JPL_DE245 = 0x53504b00000000f5ULL;
const uint64_t SPK_SOURCE_JPL_DE202 = 0x53504b00000000caULL;
const uint64_t SPK_SOURCE_JPL_DE200 = 0x53504b00000000c8ULL;
const uint64_t SPK_SOURCE_JPL_DE130 = 0x53504b0000000082ULL;
const uint64_t SPK_SOURCE_JPL_DE125 = 0x53504b000000007dULL;
const uint64_t SPK_SOURCE_JPL_DE118 = 0x53504b0000000076ULL;
const uint64_t SPK_SOURCE_JPL_DE102 = 0x53504b0000000066ULL;

const uint64_t SPK_SOURCE_JPL_MAR099 = 0x53504b0100000063ULL;
const uint64_t SPK_SOURCE_JPL_JUP365 = 0x53504b020000016dULL;
const uint64_t SPK_SOURCE_JPL_JUP349 = 0x53504b020000015dULL;
const uint64_t SPK_SOURCE_JPL_JUP348 = 0x53504b020000015cULL;
const uint64_t SPK_SOURCE_JPL_JUP347 = 0x53504b020000015bULL;
const uint64_t SPK_SOURCE_JPL_SAT480 = 0x53504b03000001e0ULL;
const uint64_t SPK_SOURCE_JPL_SAT459 = 0x53504b03000001cbULL;
const uint64_t SPK_SOURCE_JPL_SAT458 = 0x53504b03000001caULL;
const uint64_t SPK_SOURCE_JPL_SAT457 = 0x53504b03000001c9ULL;
const uint64_t SPK_SOURCE_JPL_SAT456 = 0x53504b03000001c8ULL;
const uint64_t SPK_SOURCE_JPL_SAT455 = 0x53504b03000001c7ULL;
const uint64_t SPK_SOURCE_JPL_SAT441 = 0x53504b03000001b9ULL;
const uint64_t SPK_SOURCE_JPL_SAT415 = 0x53504b030000019fULL;
const uint64_t SPK_SOURCE_JPL_URA184 = 0x53504b04000000b8ULL;
const uint64_t SPK_SOURCE_JPL_URA182 = 0x53504b04000000b6ULL;
const uint64_t SPK_SOURCE_JPL_URA117 = 0x53504b0400000075ULL;
const uint64_t SPK_SOURCE_JPL_NEP105 = 0x53504b0500000069ULL;
const uint64_t SPK_SOURCE_JPL_NEP104 = 0x53504b0500000068ULL;
const uint64_t SPK_SOURCE_JPL_NEP098 = 0x53504b0500000062ULL;
const uint64_t SPK_SOURCE_JPL_NEP097 = 0x53504b0500000061ULL;
const uint64_t SPK_SOURCE_JPL_PLU060 = 0x53504b060000003cULL;

// These classifiers intentionally treat the filename/root as the caller's
// declaration. Taiyin does not attempt to authenticate an arbitrary renamed
// data file. Unrecognized names return the ordinary external identity.
uint64_t classify_spk_source_id_from_path(const std::string& path) noexcept;

// Recognized JPL product metadata. Unknown/third-party SPK files remain fully
// usable but deliberately receive no inferred product priority.
bool is_jpl_de_spk_source_id(uint64_t source_id) noexcept;
int default_spk_source_priority(uint64_t source_id) noexcept;
uint64_t normalize_opm2_source_id(uint32_t header_source_id) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_EPHEMERIS_SOURCE_IDENTITY_H
