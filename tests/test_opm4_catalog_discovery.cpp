#include "taiyin/angle.h"
#include "taiyin/physical_constants.h"
#include "taiyin/internal/descriptor_loader.h"
#include "taiyin/internal/ephemeris_cache.h"
#include "taiyin/internal/ephemeris_discovery.h"
#include "taiyin/internal/opm4_catalog_discovery.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void push_u8(std::vector<uint8_t>* data, uint8_t value) { data->push_back(value); }

void push_u16_le(std::vector<uint8_t>* data, uint16_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void push_u32_le(std::vector<uint8_t>* data, uint32_t value) {
    data->push_back(static_cast<uint8_t>(value & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data->push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void push_i32_le(std::vector<uint8_t>* data, int32_t value) {
    push_u32_le(data, static_cast<uint32_t>(value));
}

void push_u64_le(std::vector<uint8_t>* data, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        data->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void push_f64_le(std::vector<uint8_t>* data, double value) {
    uint8_t bytes[8];
    std::memcpy(bytes, &value, sizeof(bytes));
    for (int i = 0; i < 8; ++i) data->push_back(bytes[i]);
}

void push_i8_coeffs(std::vector<uint8_t>* data, const int8_t* values, int count) {
    const int width_byte_count = (count + 3) / 4;
    for (int i = 0; i < width_byte_count; ++i) data->push_back(0);
    for (int i = 0; i < count; ++i) data->push_back(static_cast<uint8_t>(values[i]));
}

std::vector<uint8_t> make_segment_stream_payload(double start, double end) {
    std::vector<uint8_t> payload;
    push_f64_le(&payload, start);
    push_f64_le(&payload, end);
    push_f64_le(&payload, 0.0);
    push_f64_le(&payload, taiyin::TAIYIN_PI / 2.0);
    push_f64_le(&payload, 0.0);
    const int8_t cx[] = { 1, 2 };
    const int8_t cy[] = { 3, 4 };
    const int8_t cz[] = { 5 };
    push_i8_coeffs(&payload, cx, 2);
    push_i8_coeffs(&payload, cy, 2);
    push_i8_coeffs(&payload, cz, 1);
    return payload;
}

std::vector<uint8_t> make_opm4(int target_id, int center_id, double start, double end) {
    std::vector<uint8_t> payload = make_segment_stream_payload(start, end);
    std::vector<uint8_t> data;
    push_u8(&data, 'O'); push_u8(&data, 'P'); push_u8(&data, 'M'); push_u8(&data, '4');
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u16_le(&data, 128);
    push_u64_le(&data, 128);
    push_u64_le(&data, static_cast<uint64_t>(payload.size()));
    push_i32_le(&data, target_id);
    push_i32_le(&data, center_id);
    push_i32_le(&data, 1);
    push_i32_le(&data, 1);
    push_f64_le(&data, start);
    push_f64_le(&data, end);
    push_u32_le(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 0);
    push_u8(&data, 1);
    push_u8(&data, 1);
    push_u8(&data, 1);
    for (int i = 0; i < 7; ++i) push_u8(&data, 0);
    push_f64_le(&data, 0.005);
    while (data.size() < 128) push_u8(&data, 0);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label) {
    if (std::fabs(actual - expected) > tolerance) {
        std::fprintf(stderr, "%s mismatch: actual %.17g expected %.17g\n", label, actual, expected);
        assert(false);
    }
}

void write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    FILE* f = std::fopen(path.c_str(), "wb");
    assert(f);
    assert(std::fwrite(&bytes[0], 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
}

void write_text_file(const std::string& path, const char* text) {
    FILE* f = std::fopen(path.c_str(), "wb");
    assert(f);
    assert(std::fwrite(text, 1, std::strlen(text), f) == std::strlen(text));
    std::fclose(f);
}

std::string make_temp_dir() {
    char templ[] = "/tmp/taiyin-opm4-catalog-XXXXXX";
    char* path = mkdtemp(templ);
    assert(path);
    return std::string(path);
}

bool has_suffix(const std::string& value, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len
        && value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
}

taiyin::internal::EphemerisDiscoveryStatus discover_custom_marker_file(
    const std::string& path,
    const taiyin::internal::EphemerisDiscoveryOptions&,
    std::vector<taiyin::internal::EphemerisBlockDescriptor>* out
) noexcept {
    if (!out || !has_suffix(path, ".custom")) {
        return taiyin::internal::DiscoveryNotApplicable;
    }

    taiyin::internal::EphemerisBlockDescriptor descriptor;
    descriptor.route_key = taiyin::internal::EphemerisRouteKey(900001, 10, 77, 900001);
    descriptor.source_key = taiyin::internal::EphemerisBlockKey(77, 900001, 1, 0);
    descriptor.target_id = 900001;
    descriptor.center_id = 10;
    descriptor.method_id = 77;
    descriptor.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = taiyin::internal::EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = 100.0;
    descriptor.jd_tdb_end = 200.0;
    descriptor.path = path;
    try {
        out->push_back(descriptor);
    } catch (...) {
        return taiyin::internal::DiscoveryError;
    }
    return taiyin::internal::DiscoveryOk;
}

}  // namespace

int main() {
    using taiyin::CartesianState;
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockQuery;
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisBlockFormat;
    using taiyin::internal::EphemerisDiscoverFileFn;
    using taiyin::internal::EphemerisDiscoveryOptions;
    using taiyin::internal::EphemerisFrame;
    using taiyin::internal::append_builtin_ephemeris_discoverers;
    using taiyin::internal::collect_opm4_descriptors_from_directory;
    using taiyin::internal::discover_ephemeris_descriptors_from_directory;
    using taiyin::internal::discover_opm4_catalog_from_directory;
    using taiyin::internal::eval_descriptor_state;

    const std::string root = make_temp_dir();
    const std::string mar_dir = root + "/mar";
    const std::string moon_dir = root + "/moon/1800_2100";
    assert(mkdir(mar_dir.c_str(), 0700) == 0);
    assert(mkdir((root + "/moon").c_str(), 0700) == 0);
    assert(mkdir(moon_dir.c_str(), 0700) == 0);

    const double j2000 = taiyin::internal::EPHEMERIS_DISCOVERY_J2000_JD;
    const double century = taiyin::internal::EPHEMERIS_DISCOVERY_JULIAN_CENTURY_DAYS;
    const std::string mars_path = mar_dir + "/mars_100_110.opm4";
    const std::string moon_path = moon_dir + "/moon_200_210.opm4";
    const std::string big_path = mar_dir + "/jupiter_big.opm4";
    write_file(mars_path, make_opm4(4, 10, 100.0, 110.0));
    write_file(moon_path, make_opm4(301, 399, 200.0, 210.0));
    write_file(big_path, make_opm4(5, 10, j2000 - 10.0, j2000 + century + 10.0));
    write_text_file(root + "/ignored.txt", "not an ephemeris");
    write_text_file(root + "/custom.custom", "custom source marker");

    std::vector<EphemerisBlockDescriptor> descriptors;
    expect_true(collect_opm4_descriptors_from_directory(root, &descriptors), "collect opm4 descriptors");
    expect_true(descriptors.size() == 3, "descriptor count");

    bool saw_mars = false;
    bool saw_moon = false;
    bool saw_big = false;
    for (size_t i = 0; i < descriptors.size(); ++i) {
        const EphemerisBlockDescriptor& descriptor = descriptors[i];
        expect_true(descriptor.format == EphemerisBlockFormat::Opm4, "descriptor format");
        expect_true(descriptor.method_id == 1, "descriptor method");
        expect_true(descriptor.frame == EphemerisFrame::IcrfJ2000Equatorial, "descriptor frame");
        if (descriptor.target_id == 4) {
            saw_mars = true;
            expect_true(descriptor.center_id == 10, "mars center");
            expect_true(descriptor.jd_tdb_start == 100.0 && descriptor.jd_tdb_end == 110.0, "mars coverage");
        }
        if (descriptor.target_id == 301) {
            saw_moon = true;
            expect_true(descriptor.center_id == 399, "moon center");
            expect_true(descriptor.jd_tdb_start == 200.0 && descriptor.jd_tdb_end == 210.0, "moon coverage");
        }
        if (descriptor.target_id == 5) {
            saw_big = true;
            expect_true(descriptor.center_id == 10, "big center");
            expect_true(descriptor.path == big_path, "big path");
            expect_true(descriptor.jd_tdb_start == j2000 - 10.0 && descriptor.jd_tdb_end == j2000 + century + 10.0, "big raw coverage");

            EphemerisBlockDescriptor bucket;
            expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(descriptor, j2000 - 5.0, &bucket), "big leading lazy bucket");
            expect_true(bucket.route_key.bucket_id == -1, "big leading bucket id");
            expect_true(bucket.jd_tdb_start == j2000 - 10.0 && bucket.jd_tdb_end == j2000, "big leading clipped coverage");
            expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(descriptor, j2000 + 1.0, &bucket), "big full lazy bucket");
            expect_true(bucket.route_key.bucket_id == 0, "big full bucket id");
            expect_true(bucket.jd_tdb_start == j2000 && bucket.jd_tdb_end == j2000 + century, "big full bucket coverage");
            expect_true(taiyin::internal::make_cache_bucket_descriptor_for_jd(descriptor, j2000 + century + 1.0, &bucket), "big trailing lazy bucket");
            expect_true(bucket.route_key.bucket_id == 1, "big trailing bucket id");
            expect_true(bucket.jd_tdb_start == j2000 + century && bucket.jd_tdb_end == j2000 + century + 10.0, "big trailing clipped coverage");
        }
    }
    expect_true(saw_mars && saw_moon && saw_big, "saw expected descriptors");

    std::vector<EphemerisDiscoverFileFn> discoverers;
    append_builtin_ephemeris_discoverers(&discoverers);
    discoverers.push_back(&discover_custom_marker_file);
    EphemerisDiscoveryOptions discovery_options;
    std::vector<EphemerisBlockDescriptor> generic_descriptors;
    expect_true(
        discover_ephemeris_descriptors_from_directory(root, discoverers, discovery_options, &generic_descriptors),
        "generic function-pointer discovery");
    expect_true(generic_descriptors.size() == 4, "generic descriptor count");
    bool saw_custom = false;
    for (size_t i = 0; i < generic_descriptors.size(); ++i) {
        if (generic_descriptors[i].target_id == 900001) {
            saw_custom = true;
            expect_true(generic_descriptors[i].method_id == 77, "custom method");
            expect_true(generic_descriptors[i].format == EphemerisBlockFormat::Kepler, "custom format");
        }
    }
    expect_true(saw_custom, "saw custom provider descriptor");

    EphemerisBlockCatalog catalog;
    expect_true(discover_opm4_catalog_from_directory(root, &catalog), "discover opm4 catalog");
    expect_true(catalog.size() == 3, "catalog size");

    EphemerisBlockQuery query;
    query.target_id = 4;
    query.center_id = 10;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = 105.0;
    const EphemerisBlockDescriptor* selected = catalog.find_first(query);
    expect_true(selected != 0, "find mars descriptor");
    expect_true(selected->path == mars_path, "selected mars path");

    EphemerisBlockCache cache(1024 * 1024);
    CartesianState state;
    expect_true(eval_descriptor_state(*selected, &cache, 105.0, &state), "eval selected descriptor");
    expect_near(state.position_au.x, 0.005 / taiyin::TAIYIN_AU_KM, 1e-20, "position x");

    return 0;
}
