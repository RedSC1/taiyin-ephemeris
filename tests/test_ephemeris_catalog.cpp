#include "taiyin/internal/ephemeris_catalog.h"

#include <cassert>
#include <cstdio>
#include <vector>

namespace {

void expect_true(bool value, const char* label) {
    if (!value) {
        std::fprintf(stderr, "expected true: %s\n", label);
        assert(false);
    }
}

void expect_false(bool value, const char* label) {
    if (value) {
        std::fprintf(stderr, "expected false: %s\n", label);
        assert(false);
    }
}

void expect_size(size_t actual, size_t expected, const char* label) {
    if (actual != expected) {
        std::fprintf(stderr, "expected size %zu got %zu: %s\n", expected, actual, label);
        assert(false);
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
    descriptor.format = taiyin::internal::EphemerisBlockFormat::Opm4;
    descriptor.jd_tdb_start = jd_start;
    descriptor.jd_tdb_end = jd_end;
    descriptor.path = path;
    return descriptor;
}

}  // namespace

int main() {
    using taiyin::internal::EphemerisBlockCatalog;
    using taiyin::internal::EphemerisBlockDescriptor;
    using taiyin::internal::EphemerisBlockKey;
    using taiyin::internal::EphemerisBlockQuery;
    using taiyin::internal::EphemerisFrame;
    using taiyin::internal::EphemerisPriorityRegistry;
    using taiyin::internal::MethodPriorityEntry;
    using taiyin::internal::ephemeris_block_key_equal;
    using taiyin::internal::find_first_ephemeris_descriptor;
    using taiyin::internal::rank_ephemeris_descriptors;
    using taiyin::internal::select_ephemeris_descriptor;

    const int venus = 2;
    const int emb = 3;
    const int sun = 10;
    const int opm4 = 1;
    const int spk = 2;
    const int moshier = 3;

    EphemerisBlockCatalog catalog;
    expect_true(catalog.add(make_descriptor(100, venus, sun, opm4, 1000, 2451545.0, 2488070.0, "opm4")), "add venus opm4");
    expect_true(catalog.add(make_descriptor(101, venus, sun, spk, 2000, 2451545.0, 2488070.0, "spk")), "add venus spk");
    expect_true(catalog.add(make_descriptor(102, venus, emb, spk, 2001, 2451545.0, 2488070.0, "wrong-center")), "add wrong center");
    expect_true(catalog.add(make_descriptor(103, emb, sun, spk, 2002, 2451545.0, 2488070.0, "wrong-target")), "add wrong target");

    EphemerisBlockQuery query;
    query.target_id = venus;
    query.center_id = sun;
    query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    query.jd_tdb = 2451600.0;

    std::vector<const EphemerisBlockDescriptor*> candidates;
    expect_true(catalog.find_candidates(query, &candidates), "find venus candidates");
    expect_size(candidates.size(), 2, "only matching target-center-time candidates");
    expect_true(candidates[0]->route_key.bucket_id == 1000, "candidate keeps route key");

    query.jd_tdb = 2451545.0;
    expect_true(catalog.find_candidates(query, &candidates), "coverage includes start");
    query.jd_tdb = 2488070.0;
    expect_false(catalog.find_candidates(query, &candidates), "coverage excludes end");

    query.jd_tdb = 2451600.0;
    const EphemerisBlockDescriptor* first = catalog.find_first(query);
    expect_true(first != 0, "find first descriptor");
    expect_true(first->source_key.block_id == 100, "find_first keeps descriptor order");

    EphemerisPriorityRegistry priorities;
    expect_true(priorities.set_global_method_priority(opm4, 10), "set global opm4 priority");
    expect_true(priorities.set_global_method_priority(spk, 20), "set global spk priority");
    expect_true(priorities.set_global_method_priority(moshier, 5), "set global moshier priority");

    size_t priority_count = 0;
    const MethodPriorityEntry* priority_entries = priorities.global_method_priorities(&priority_count);
    expect_size(priority_count, 3, "global priority count");
    expect_true(priority_entries[0].method_id == spk, "global priority sorted high to low");
    expect_true(priority_entries[1].method_id == opm4, "global priority second");
    expect_true(priority_entries[2].method_id == moshier, "global priority third");

    const EphemerisBlockDescriptor* selected = catalog.select_best(query, priorities);
    expect_true(selected != 0, "select descriptor with global priority");
    expect_true(selected->method_id == spk, "global priority selects spk");
    expect_true(selected->route_key.bucket_id == 2000, "selected route key is cache route key");

    expect_true(priorities.set_target_method_priority(venus, opm4, 100), "set venus opm4 override");
    selected = catalog.select_best(query, priorities);
    expect_true(selected != 0, "select descriptor with target override");
    expect_true(selected->method_id == opm4, "target override selects opm4");

    std::vector<const EphemerisBlockDescriptor*> ranked;
    expect_true(catalog.rank_candidates(query, priorities, &ranked), "rank catalog candidates with target override");
    expect_size(ranked.size(), 2, "ranked catalog candidate count");
    expect_true(ranked[0]->method_id == opm4, "ranked target override first");
    expect_true(ranked[1]->method_id == spk, "unprioritized candidate appended after override");

    EphemerisBlockQuery emb_query;
    emb_query.target_id = emb;
    emb_query.center_id = sun;
    emb_query.frame = EphemerisFrame::IcrfJ2000Equatorial;
    emb_query.jd_tdb = 2451600.0;
    selected = catalog.select_best(emb_query, priorities);
    expect_true(selected != 0, "other target falls back to global priority");
    expect_true(selected->method_id == spk, "fallback global priority selects spk");

    EphemerisPriorityRegistry empty_priorities;
    selected = catalog.select_best(query, empty_priorities);
    expect_true(selected != 0, "empty priorities fall back to first candidate");
    expect_true(selected->method_id == opm4, "fallback returns first candidate");

    EphemerisBlockDescriptor raw_descriptors[3];
    raw_descriptors[0] = make_descriptor(200, venus, sun, opm4, 3000, 2451545.0, 2488070.0, "raw-opm4");
    raw_descriptors[1] = make_descriptor(201, venus, sun, spk, 3001, 2451545.0, 2488070.0, "raw-spk");
    raw_descriptors[2] = make_descriptor(202, venus, sun, moshier, 3002, 2451545.0, 2488070.0, "raw-moshier");
    first = 0;
    expect_true(find_first_ephemeris_descriptor(raw_descriptors, 3, query, &first), "find first raw descriptor");
    expect_true(first && first->source_key.block_id == 200, "raw first descriptor wins");

    candidates.clear();
    candidates.push_back(&raw_descriptors[0]);
    candidates.push_back(&raw_descriptors[1]);
    candidates.push_back(&raw_descriptors[2]);
    MethodPriorityEntry raw_priority[1];
    raw_priority[0] = MethodPriorityEntry(spk, 100);
    selected = 0;
    expect_true(select_ephemeris_descriptor(candidates, raw_priority, 1, &selected), "select raw descriptor by method");
    expect_true(selected && selected->source_key.block_id == 201, "raw selector chooses matching method");

    ranked.clear();
    expect_true(rank_ephemeris_descriptors(candidates, raw_priority, 1, &ranked), "rank raw descriptors by method");
    expect_size(ranked.size(), 3, "rank raw descriptor count");
    expect_true(ranked[0]->source_key.block_id == 201, "rank raw prioritized descriptor first");
    expect_true(ranked[1]->source_key.block_id == 200, "rank raw keeps first unprioritized order");
    expect_true(ranked[2]->source_key.block_id == 202, "rank raw keeps second unprioritized order");

    ranked.clear();
    expect_true(rank_ephemeris_descriptors(candidates, 0, 0, &ranked), "rank raw descriptors without priorities");
    expect_true(ranked[0]->source_key.block_id == 200, "rank without priorities keeps catalog order");

    EphemerisBlockKey key_a(1, 2, 3, 4);
    EphemerisBlockKey key_b(1, 2, 3, 4);
    EphemerisBlockKey key_c(1, 2, 3, 5);
    expect_true(ephemeris_block_key_equal(key_a, key_b), "equal keys");
    expect_false(ephemeris_block_key_equal(key_a, key_c), "different keys");

    return 0;
}
