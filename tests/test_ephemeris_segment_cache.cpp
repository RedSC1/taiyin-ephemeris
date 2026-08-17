#include "taiyin/internal/ephemeris_segment_cache.h"

#include <cassert>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {

std::atomic<int> g_destroy_count(0);

struct Payload {
    int value;
};

void destroy_payload(void* data) {
    Payload* payload = static_cast<Payload*>(data);
    delete payload;
    ++g_destroy_count;
}

taiyin::internal::EphemerisSegmentCacheData make_payload(int value) {
    Payload* payload = new Payload();
    payload->value = value;
    return taiyin::internal::EphemerisSegmentCacheData(payload, destroy_payload);
}

struct ReadHoldState {
    std::atomic<bool>* entered;
    std::atomic<bool>* release;
    int* observed_value;
};

bool hold_payload_read(const void* data, void* user) {
    const Payload* payload = static_cast<const Payload*>(data);
    ReadHoldState* state = static_cast<ReadHoldState*>(user);
    *state->observed_value = payload ? payload->value : -1;
    state->entered->store(true);
    while (!state->release->load()) {
        std::this_thread::yield();
    }
    return true;
}

bool copy_payload_value(const void* data, void* user) {
    const Payload* payload = static_cast<const Payload*>(data);
    int* out = static_cast<int*>(user);
    *out = payload ? payload->value : -1;
    return true;
}

taiyin::internal::EphemerisSegmentCacheKey make_key(uint32_t kind, uint64_t source_id, uint64_t item_id) {
    return taiyin::internal::EphemerisSegmentCacheKey(
        kind,
        static_cast<int>(source_id),
        10,
        20,
        taiyin::internal::IcrfJ2000Equatorial,
        taiyin::internal::EphemerisBlockKey(source_id, 1, 1, 0),
        static_cast<int64_t>(item_id));
}

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

void expect_payload(
    taiyin::internal::EphemerisSegmentCache& cache,
    const taiyin::internal::EphemerisSegmentCacheKey& key,
    int expected,
    const char* label
) {
    int observed = 0;
    expect_true(cache.with_data(key, copy_payload_value, &observed), label);
    if (observed != expected) {
        std::fprintf(stderr, "expected payload %d got %d: %s\n", expected, observed, label);
        assert(false);
    }
}

void test_insert_find_and_replace() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindOpm2Segment;

    g_destroy_count = 0;
    EphemerisSegmentCache cache(2);
    const taiyin::internal::EphemerisSegmentCacheKey key =
        make_key(EphemerisSegmentCacheKindOpm2Segment, 10, 20);

    expect_true(cache.insert(key, make_payload(42)), "insert first payload");
    expect_size(cache.entry_count(), 1, "entry count after insert");
    expect_payload(cache, key, 42, "find first payload");
    int copied = 0;
    expect_true(cache.with_data(key, copy_payload_value, &copied), "with_data copies payload");
    expect_true(copied == 42, "with_data observes payload value");

    expect_true(cache.insert(key, make_payload(84)), "replace same key");
    expect_size(cache.entry_count(), 1, "entry count after replace");
    expect_payload(cache, key, 84, "find replaced payload");
    expect_true(g_destroy_count == 1, "replace destroys old payload");

    cache.clear();
    expect_size(cache.entry_count(), 0, "entry count after clear");
    expect_true(g_destroy_count == 2, "clear destroys remaining payload");
}

void test_clock_eviction() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindOpm2Segment;

    g_destroy_count = 0;
    EphemerisSegmentCache cache(2);
    const taiyin::internal::EphemerisSegmentCacheKey key_a =
        make_key(EphemerisSegmentCacheKindOpm2Segment, 1, 100);
    const taiyin::internal::EphemerisSegmentCacheKey key_b =
        make_key(EphemerisSegmentCacheKindOpm2Segment, 1, 200);
    const taiyin::internal::EphemerisSegmentCacheKey key_c =
        make_key(EphemerisSegmentCacheKindOpm2Segment, 1, 300);

    expect_true(cache.insert(key_a, make_payload(100)), "insert a");
    expect_true(cache.insert(key_b, make_payload(200)), "insert b");
    expect_payload(cache, key_b, 200, "touch b before eviction");

    expect_true(cache.insert(key_c, make_payload(300)), "insert c");
    expect_size(cache.entry_count(), 2, "entry count after clock eviction");
    expect_false(cache.contains(key_a), "clock evicts first unrefreshed key");
    expect_payload(cache, key_b, 200, "b survives first eviction");
    expect_payload(cache, key_c, 300, "c inserted");
    expect_true(g_destroy_count == 1, "eviction destroys one payload");
}

void test_erase_and_zero_capacity() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindSpkSegment;

    g_destroy_count = 0;
    const taiyin::internal::EphemerisSegmentCacheKey key =
        make_key(EphemerisSegmentCacheKindSpkSegment, 7, 11);

    EphemerisSegmentCache cache(1);
    expect_true(cache.insert(key, make_payload(11)), "insert before erase");
    expect_true(cache.erase(key), "erase existing");
    expect_false(cache.contains(key), "erased key absent");
    expect_true(g_destroy_count == 1, "erase destroys payload");
    expect_false(cache.erase(key), "erase missing");

    EphemerisSegmentCache empty_cache(0);
    taiyin::internal::EphemerisSegmentCacheData data = make_payload(99);
    expect_false(empty_cache.insert(key, data), "zero capacity rejects insert");
    expect_true(g_destroy_count == 1, "zero capacity does not take ownership");
    destroy_payload(data.data);
    expect_true(g_destroy_count == 2, "caller still owns rejected payload");
}

void test_with_data_pins_payload_without_holding_read_lock() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindSpkSegment;

    g_destroy_count = 0;
    EphemerisSegmentCache cache(1);
    const taiyin::internal::EphemerisSegmentCacheKey key =
        make_key(EphemerisSegmentCacheKindSpkSegment, 77, 88);

    expect_true(cache.insert(key, make_payload(123)), "insert locked payload");

    std::atomic<bool> entered(false);
    std::atomic<bool> release(false);
    std::atomic<bool> erased(false);
    int observed_value = 0;
    ReadHoldState state;
    state.entered = &entered;
    state.release = &release;
    state.observed_value = &observed_value;

    std::thread reader([&]() {
        expect_true(cache.with_data(key, hold_payload_read, &state), "reader with_data succeeds");
    });

    while (!entered.load()) {
        std::this_thread::yield();
    }

    std::thread writer([&]() {
        erased.store(cache.erase(key));
    });

    for (int i = 0; i < 200 && !erased.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect_true(erased.load(), "erase completes while read callback holds pinned data");
    expect_true(g_destroy_count.load() == 0, "pinned payload survives concurrent erase");
    expect_true(observed_value == 123, "read callback saw payload");

    release.store(true);
    reader.join();
    writer.join();

    expect_true(g_destroy_count.load() == 1, "payload destroyed after read callback exits");
}

struct ReentrantClearState {
    taiyin::internal::EphemerisSegmentCache* cache;
    int observed_before;
    int observed_after;
};

bool clear_cache_from_callback(const void* data, void* user) {
    const Payload* payload = static_cast<const Payload*>(data);
    ReentrantClearState* state = static_cast<ReentrantClearState*>(user);
    state->observed_before = payload ? payload->value : -1;
    state->cache->clear();
    state->observed_after = payload ? payload->value : -1;
    return true;
}

void test_with_data_allows_reentrant_clear() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindSpkSegment;

    g_destroy_count = 0;
    EphemerisSegmentCache cache(1);
    const taiyin::internal::EphemerisSegmentCacheKey key =
        make_key(EphemerisSegmentCacheKindSpkSegment, 91, 92);
    expect_true(cache.insert(key, make_payload(456)), "insert reentrant payload");

    ReentrantClearState state;
    state.cache = &cache;
    state.observed_before = 0;
    state.observed_after = 0;
    expect_true(
        cache.with_data(key, clear_cache_from_callback, &state),
        "with_data callback may clear cache");
    expect_true(state.observed_before == 456, "reentrant callback sees payload before clear");
    expect_true(state.observed_after == 456, "pinned payload survives reentrant clear");
    expect_size(cache.entry_count(), 0, "reentrant clear empties cache");
    expect_true(g_destroy_count.load() == 1, "reentrant payload destroyed after callback");
}

struct ReentrantDestroyPayload {
    taiyin::internal::EphemerisSegmentCache* cache;
    std::atomic<bool>* destroyed;
    size_t* entry_count_seen;
};

void destroy_payload_with_cache_read(void* data) {
    ReentrantDestroyPayload* payload =
        static_cast<ReentrantDestroyPayload*>(data);
    *payload->entry_count_seen = payload->cache->entry_count();
    payload->destroyed->store(true);
    delete payload;
}

void test_payload_destroy_runs_outside_cache_lock() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheData;
    using taiyin::internal::EphemerisSegmentCacheKindSpkSegment;

    EphemerisSegmentCache cache(1);
    const taiyin::internal::EphemerisSegmentCacheKey key =
        make_key(EphemerisSegmentCacheKindSpkSegment, 93, 94);
    std::atomic<bool> destroyed(false);
    size_t entry_count_seen = 99;
    ReentrantDestroyPayload* payload = new ReentrantDestroyPayload();
    payload->cache = &cache;
    payload->destroyed = &destroyed;
    payload->entry_count_seen = &entry_count_seen;

    expect_true(
        cache.insert(
            key,
            EphemerisSegmentCacheData(payload, destroy_payload_with_cache_read)),
        "insert payload with reentrant deleter");
    expect_true(cache.erase(key), "erase payload with reentrant deleter");
    expect_true(destroyed.load(), "payload deleter ran");
    expect_size(entry_count_seen, 0, "payload deleter safely re-enters cache");
}

void test_concurrent_read_write_pressure() {
    using taiyin::internal::EphemerisSegmentCache;
    using taiyin::internal::EphemerisSegmentCacheKindOpm2Segment;

    g_destroy_count = 0;
    EphemerisSegmentCache cache(8);
    std::atomic<bool> start(false);
    std::atomic<int> ready(0);
    std::atomic<int> done(0);
    std::atomic<int> reads(0);

    std::thread writer([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 800; ++i) {
            const taiyin::internal::EphemerisSegmentCacheKey key =
                make_key(EphemerisSegmentCacheKindOpm2Segment, static_cast<uint64_t>(i % 13), i);
            expect_true(cache.insert(key, make_payload(i)), "concurrent insert");
            if ((i % 5) == 0) {
                cache.erase(make_key(EphemerisSegmentCacheKindOpm2Segment, static_cast<uint64_t>((i + 3) % 13), i - 3));
            }
            if ((i % 211) == 0) {
                cache.clear();
            }
        }
        done.fetch_add(1);
    });

    std::thread reader_a([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 1200; ++i) {
            const taiyin::internal::EphemerisSegmentCacheKey key =
                make_key(EphemerisSegmentCacheKindOpm2Segment, static_cast<uint64_t>(i % 13), i);
            int observed = 0;
            if (cache.with_data(key, copy_payload_value, &observed)) {
                reads.fetch_add(1);
            }
            static_cast<void>(cache.contains(key));
        }
        done.fetch_add(1);
    });

    std::thread reader_b([&]() {
        ready.fetch_add(1);
        while (!start.load()) {
            std::this_thread::yield();
        }
        for (int i = 0; i < 1200; ++i) {
            static_cast<void>(cache.entry_count());
            static_cast<void>(cache.max_entries());
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
    expect_true(done.load() == 3, "segment cache concurrent operations complete");

    writer.join();
    reader_a.join();
    reader_b.join();

    expect_true(cache.entry_count() <= cache.max_entries(), "segment cache stays within capacity");
    static_cast<void>(reads.load());
    cache.clear();
}

}  // namespace

int main() {
    test_insert_find_and_replace();
    test_clock_eviction();
    test_erase_and_zero_capacity();
    test_with_data_pins_payload_without_holding_read_lock();
    test_with_data_allows_reentrant_clear();
    test_payload_destroy_runs_outside_cache_lock();
    test_concurrent_read_write_pressure();
    return 0;
}
