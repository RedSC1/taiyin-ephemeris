#include "taiyin/internal/ephemeris_catalog.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    if (!taiyin::split_julian_date_from_double(jd, &out)) {
        std::abort();
    }
    return out;
}

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        std::abort();
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        std::abort();
    }
}

void expect_size(size_t actual, size_t expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected size %zu got %zu: %s\n", expected, actual, label);
        std::abort();
    }
}

taiyin::internal::EphemerisBlockDescriptor make_descriptor(
    uint64_t block_id,
    int target_id,
    int center_id,
    int method_id,
    int bucket_id,
    double jd_start,
    double jd_end,
    const char* path
) {
    taiyin::internal::EphemerisBlockDescriptor descriptor;
    descriptor.route_key = taiyin::internal::EphemerisRouteKey(target_id, center_id, method_id, bucket_id);
    descriptor.source_key = taiyin::internal::EphemerisBlockKey(10, block_id, 1, 0);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = taiyin::internal::EphemerisFrame::IcrfJ2000Equatorial;
    descriptor.format = taiyin::internal::EphemerisBlockFormat::FormatUnknown;
    descriptor.jd_tdb_start = jd_start;
    descriptor.jd_tdb_end = jd_end;
    descriptor.path = path;
    return descriptor;
}

void test_catalog_concurrent_read_add_copy() {
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockQuery;
    using taiyin::internal::EphemerisFrame;

    EphemerisBlockCatalog catalog;
    for (int i = 0; i < 8; ++i) {
        expect_true(
            catalog.add(make_descriptor(
                static_cast<uint64_t>(1000 + i),
                7000 + (i % 3),
                10,
                900 + (i % 2),
                2000 + i,
                2450000.0,
                2460000.0,
                "initial")),
            "add initial concurrent catalog descriptor");
    }

    std::atomic<bool> start(false);
    std::atomic<int> ready(0);
    std::atomic<int> done(0);
    std::atomic<int> finds(0);

    std::thread writer([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 300; ++i) {
            catalog.add(make_descriptor(
                static_cast<uint64_t>(2000 + i),
                7000 + (i % 5),
                10,
                900 + (i % 3),
                3000 + i,
                2450000.0,
                2460000.0,
                "writer"));
        }
        done.fetch_add(1);
    });

    std::thread reader([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 1000; ++i) {
            EphemerisBlockQuery query;
            query.target_id = 7000 + (i % 5);
            query.center_id = 10;
            query.frame = EphemerisFrame::IcrfJ2000Equatorial;
            query.jd_tdb = split_jd(2455000.0);
            std::vector<taiyin::internal::EphemerisBlockDescriptor> candidates;
            if (catalog.find_method_candidates(query, 900 + (i % 3), &candidates)) {
                finds.fetch_add(static_cast<int>(candidates.size()));
            }
        }
        done.fetch_add(1);
    });

    std::thread copier([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 120; ++i) {
            EphemerisBlockCatalog snapshot;
            snapshot = catalog;
            static_cast<void>(snapshot.size());
        }
        done.fetch_add(1);
    });

    while (ready.load() != 3) {
        std::this_thread::yield();
    }
    start.store(true);

    for (int i = 0; i < 200; ++i) {
        if (done.load() == 3) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    expect_true(done.load() == 3, "catalog concurrent read add copy complete");

    writer.join();
    reader.join();
    copier.join();

    expect_true(catalog.size() >= 308, "catalog keeps all added descriptors");
    expect_true(finds.load() > 0, "catalog readers found descriptors");
}

void test_catalog_candidates_survive_mutation() {
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockQuery;
    using taiyin::internal::EphemerisFrame;

    EphemerisBlockCatalog catalog;
    expect_true(
        catalog.add(make_descriptor(1, 42, 10, 7, 99, 2450000.0, 2460000.0, "stable")),
        "add stable candidate descriptor");

    EphemerisBlockQuery query;
    query.target_id = 42;
    query.center_id = 10;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = split_jd(2455000.0);

    std::vector<EphemerisBlockDescriptor> candidates;
    expect_true(catalog.find_method_candidates(query, 7, &candidates), "find stable candidate copy");
    expect_size(candidates.size(), 1, "stable candidate count");

    for (int i = 0; i < 1024; ++i) {
        expect_true(
            catalog.add(make_descriptor(
                static_cast<uint64_t>(100 + i),
                1000 + i,
                10,
                7,
                1000 + i,
                2450000.0,
                2460000.0,
                "growth")),
            "grow catalog after candidate copy");
    }

    expect_true(candidates[0].target_id == 42, "candidate copy keeps target after catalog growth");
    expect_true(candidates[0].route_key.bucket_id == 99, "candidate copy keeps route after catalog growth");
    expect_true(candidates[0].path == "stable", "candidate copy keeps path after catalog growth");
}

void test_catalog_source_index_copy() {
    taiyin::internal::EphemerisBlockCatalog catalog;
    taiyin::internal::EphemerisSourceIndex index;
    index.source_key = taiyin::internal::EphemerisBlockKey(2, 44, 1, 0);
    index.format = taiyin::internal::EphemerisBlockFormat::Spk;
    index.path = "source.bsp";
    index.byte_count = 1234;
    index.payload = std::shared_ptr<void>(new int(42), [](void* p) { delete static_cast<int*>(p); });

    expect_true(catalog.add_source_index(index), "add source index");

    taiyin::internal::EphemerisSourceIndex found;
    expect_true(catalog.find_source_index(index.source_key, &found), "find source index");
    expect_true(found.format == taiyin::internal::EphemerisBlockFormat::Spk, "source index keeps format");
    expect_true(found.path == "source.bsp", "source index keeps path");
    expect_size(found.byte_count, 1234, "source index keeps bytes");
    expect_true(found.payload.get() == index.payload.get(), "source index shares payload");

    taiyin::internal::EphemerisBlockCatalog copy = catalog;
    taiyin::internal::EphemerisSourceIndex copied;
    expect_true(copy.find_source_index(index.source_key, &copied), "copied catalog keeps source index");
    expect_true(copied.payload.get() == index.payload.get(), "copied source index shares payload");
}

}  // namespace

int main() {
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockKey;
    using taiyin::internal::EphemerisBlockQuery;
    using taiyin::internal::EphemerisFrame;
    using taiyin::internal::ephemeris_block_key_equal;

    const int venus = 2;
    const int emb = 3;
    const int sun = 10;
    const int primary = 1;
    const int spk = 2;
    const int missing_method = 3;

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(make_descriptor(100, venus, sun, primary, 1000, 2451545.0, 2488070.0, "primary")), "add venus primary");
    expect_true(catalog.add(make_descriptor(101, venus, sun, spk, 2000, 2451545.0, 2488070.0, "spk")), "add venus spk");
    expect_true(catalog.add(make_descriptor(102, venus, emb, spk, 2001, 2451545.0, 2488070.0, "wrong-center")), "add wrong center");
    expect_true(catalog.add(make_descriptor(103, emb, sun, spk, 2002, 2451545.0, 2488070.0, "wrong-target")), "add wrong target");

    EphemerisBlockQuery query;
    query.target_id = venus;
    query.center_id = sun;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = split_jd(2451600.0);

    std::vector<EphemerisBlockDescriptor> candidates;
    expect_true(catalog.find_method_candidates(query, primary, &candidates), "find venus primary method candidates");
    expect_size(candidates.size(), 1, "only matching primary method candidates");
    expect_true(candidates[0].route_key.bucket_id == 1000, "method candidate keeps route key");
    expect_true(
        candidates[0].jd_tdb_start_split == split_jd(candidates[0].jd_tdb_start)
            && candidates[0].jd_tdb_end_split == split_jd(candidates[0].jd_tdb_end),
        "catalog canonicalizes descriptor coverage bounds once");
    expect_true(catalog.find_method_candidates(query, spk, &candidates), "find venus SPK method candidates");
    expect_size(candidates.size(), 1, "only matching method candidates");
    expect_true(candidates[0].source_key.block_id == 101, "method candidate keeps descriptor order");
    expect_false(
        catalog.find_method_candidates(query, missing_method, &candidates),
        "missing method has no candidates");

    query.jd_tdb = split_jd(2451545.0);
    expect_true(catalog.find_method_candidates(query, primary, &candidates), "coverage includes start");
    query.jd_tdb = split_jd(2488070.0);
    expect_false(catalog.find_method_candidates(query, primary, &candidates), "coverage excludes end");

    EphemerisBlockQuery emb_query;
    emb_query.target_id = emb;
    emb_query.center_id = sun;
    emb_query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    emb_query.jd_tdb = split_jd(2451600.0);
    expect_true(catalog.find_method_candidates(emb_query, spk, &candidates), "other target finds method descriptor");
    expect_size(candidates.size(), 1, "other target method count");
    expect_true(candidates[0].method_id == spk, "other target keeps insertion order");

    EphemerisBlockKey key_a(1, 2, 3, 4);
    EphemerisBlockKey key_b(1, 2, 3, 4);
    EphemerisBlockKey key_c(1, 2, 3, 5);
    expect_true(ephemeris_block_key_equal(key_a, key_b), "equal keys");
    expect_false(ephemeris_block_key_equal(key_a, key_c), "different keys");

    test_catalog_source_index_copy();
    test_catalog_concurrent_read_add_copy();
    test_catalog_candidates_survive_mutation();

    return 0;
}
