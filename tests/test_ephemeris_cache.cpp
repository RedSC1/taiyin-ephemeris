#include "taiyin/internal/ephemeris_cache.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct FakeData {
    double base;
    int id;
};

int g_destroy_count = 0;
int g_destroyed_ids[32];
int g_destroyed_count = 0;
std::mutex g_destroy_mutex;

std::atomic<int> g_active_slow_evals(0);
std::atomic<int> g_peak_slow_evals(0);
std::atomic<int> g_slow_eval_entries(0);
std::atomic<int> g_slow_eval_exits(0);

void reset_destroy_log() {
    std::lock_guard<std::mutex> lock(g_destroy_mutex);
    g_destroy_count = 0;
    g_destroyed_count = 0;
    for (int i = 0; i < 32; ++i) {
        g_destroyed_ids[i] = 0;
    }
}

void reset_slow_eval_log() {
    g_active_slow_evals.store(0);
    g_peak_slow_evals.store(0);
    g_slow_eval_entries.store(0);
    g_slow_eval_exits.store(0);
}

void record_slow_eval_enter() {
    g_slow_eval_entries.fetch_add(1);
    const int current = g_active_slow_evals.fetch_add(1) + 1;
    int observed = g_peak_slow_evals.load();
    while (current > observed
           && !g_peak_slow_evals.compare_exchange_weak(observed, current)) {}
}

void record_slow_eval_exit() {
    g_active_slow_evals.fetch_sub(1);
    g_slow_eval_exits.fetch_add(1);
}

void fake_destroy(void* ptr) {
    FakeData* data = static_cast<FakeData*>(ptr);
    {
        std::lock_guard<std::mutex> lock(g_destroy_mutex);
        if (data && g_destroyed_count < 32) {
            g_destroyed_ids[g_destroyed_count++] = data->id;
        }
        ++g_destroy_count;
    }
    delete data;
}

bool fake_position(double jd_tdb, const void* ptr, taiyin::Vector3* out) {
    const FakeData* data = static_cast<const FakeData*>(ptr);
    if (!data || !out) {
        return false;
    }
    out->x = data->base + jd_tdb;
    out->y = data->base + 2.0 * jd_tdb;
    out->z = data->base + 3.0 * jd_tdb;
    return true;
}

bool slow_fake_position(double jd_tdb, const void* ptr, taiyin::Vector3* out) {
    record_slow_eval_enter();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const bool ok = fake_position(jd_tdb, ptr, out);
    record_slow_eval_exit();
    return ok;
}

bool fake_velocity(double jd_tdb, const void* ptr, taiyin::Vector3* out) {
    const FakeData* data = static_cast<const FakeData*>(ptr);
    if (!data || !out) {
        return false;
    }
    (void)jd_tdb;
    out->x = data->base;
    out->y = data->base + 1.0;
    out->z = data->base + 2.0;
    return true;
}

bool fake_acceleration(double jd_tdb, const void* ptr, taiyin::Vector3* out) {
    const FakeData* data = static_cast<const FakeData*>(ptr);
    if (!data || !out) {
        return false;
    }
    (void)jd_tdb;
    out->x = data->base + 10.0;
    out->y = data->base + 20.0;
    out->z = data->base + 30.0;
    return true;
}

taiyin::internal::StorageEphemerisBlock make_fake_storage(int id, double base, size_t bytes) {
    taiyin::internal::StorageEphemerisBlock storage;
    storage.cache_id = 0;
    storage.format = taiyin::internal::EphemerisBlockFormat::Opm4;
    storage.position = fake_position;
    storage.velocity = fake_velocity;
    storage.acceleration = fake_acceleration;
    storage.destroy_element = fake_destroy;
    storage.total_bytes = bytes;
    storage.data_vector.push_back(new FakeData{base, id});
    return storage;
}

taiyin::internal::StorageEphemerisBlock make_slow_fake_storage(int id, double base, size_t bytes) {
    taiyin::internal::StorageEphemerisBlock storage = make_fake_storage(id, base, bytes);
    storage.position = slow_fake_position;
    return storage;
}

bool near(double actual, double expected) {
    return std::fabs(actual - expected) < 1e-12;
}

void expect_true(bool value, const char* message, int* failures) {
    if (!value) {
        std::cerr << "FAIL: " << message << "\n";
        ++(*failures);
    }
}

void expect_false(bool value, const char* message, int* failures) {
    if (value) {
        std::cerr << "FAIL: " << message << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* message, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* message, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << message << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, const char* message, int* failures) {
    if (!near(actual, expected)) {
        std::cerr << "FAIL: " << message << ": actual=" << actual << " expected=" << expected << "\n";
        ++(*failures);
    }
}

}  // namespace

int main() {
    using taiyin::internal::EphemerisBlockCache;
    using taiyin::internal::EphemerisRouteKey;
    using taiyin::internal::StorageEphemerisBlock;

    int failures = 0;

    {
        reset_destroy_log();
        EphemerisBlockCache cache(128);
        EphemerisRouteKey key(4, 10, 1, 100);
        StorageEphemerisBlock storage = make_fake_storage(1, 5.0, 32);
        expect_true(cache.insert(key, 0.0, 10.0, &storage), "insert fake block", &failures);
        expect_size(storage.total_bytes, 0, "insert takes storage ownership", &failures);
        expect_true(cache.contains(key), "contains inserted key", &failures);

        taiyin::Vector3 pos;
        expect_true(cache.eval_position(key, 2.0, &pos), "eval inserted position", &failures);
        expect_near(pos.x, 7.0, "position x", &failures);
        expect_near(pos.y, 9.0, "position y", &failures);
        expect_near(pos.z, 11.0, "position z", &failures);
        expect_size(cache.entry_count(), 1, "one entry after insert", &failures);
        expect_size(cache.total_bytes(), 32, "total bytes after insert", &failures);

        cache.clear();
        expect_int(g_destroy_count, 1, "clear destroys block once", &failures);
        expect_size(cache.entry_count(), 0, "clear removes entries", &failures);
        expect_size(cache.total_bytes(), 0, "clear resets bytes", &failures);
        cache.clear();
        expect_int(g_destroy_count, 1, "second clear does not double destroy", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(128);
        EphemerisRouteKey key(4, 10, 1, 100);
        StorageEphemerisBlock storage = make_fake_storage(11, 5.0, 32);
        expect_true(cache.insert(key, 100.0, 200.0, &storage), "insert covered block", &failures);

        taiyin::Vector3 pos;
        expect_false(cache.eval_position(key, 99.999, &pos), "coverage rejects before start", &failures);
        expect_true(cache.eval_position(key, 100.0, &pos), "coverage includes start", &failures);
        expect_true(cache.eval_position(key, 150.0, &pos), "coverage includes middle", &failures);
        expect_false(cache.eval_position(key, 200.0, &pos), "coverage excludes end", &failures);
    }

    {
        reset_destroy_log();
        EphemerisRouteKey key(4, 10, 1, 100);
        {
            EphemerisBlockCache cache(128);
            StorageEphemerisBlock storage = make_fake_storage(2, 1.0, 32);
            expect_true(cache.insert(key, 0.0, 10.0, &storage), "insert for destructor test", &failures);
        }
        expect_int(g_destroy_count, 1, "cache destructor destroys block once", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(128);
        EphemerisRouteKey key(4, 10, 1, 100);
        StorageEphemerisBlock first = make_fake_storage(3, 1.0, 32);
        StorageEphemerisBlock second = make_fake_storage(4, 9.0, 48);
        expect_true(cache.insert(key, 0.0, 10.0, &first), "insert first same key", &failures);
        expect_true(cache.insert(key, 0.0, 10.0, &second), "replace same key", &failures);
        expect_int(g_destroy_count, 1, "replace destroys old block", &failures);
        expect_int(g_destroyed_ids[0], 3, "replace destroyed first id", &failures);
        expect_size(cache.entry_count(), 1, "replace keeps one entry", &failures);
        expect_size(cache.total_bytes(), 48, "replace updates bytes", &failures);

        taiyin::Vector3 pos;
        expect_true(cache.eval_position(key, 1.0, &pos), "eval replacement", &failures);
        expect_near(pos.x, 10.0, "replacement position", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(100);
        EphemerisRouteKey key_a(1, 10, 1, 0);
        EphemerisRouteKey key_b(2, 10, 1, 0);
        EphemerisRouteKey key_c(3, 10, 1, 0);
        StorageEphemerisBlock a = make_fake_storage(10, 10.0, 40);
        StorageEphemerisBlock b = make_fake_storage(20, 20.0, 40);
        StorageEphemerisBlock c = make_fake_storage(30, 30.0, 40);
        expect_true(cache.insert(key_a, 0.0, 10.0, &a), "insert A", &failures);
        expect_true(cache.insert(key_b, 0.0, 10.0, &b), "insert B", &failures);

        taiyin::Vector3 pos;
        expect_true(cache.eval_position(key_a, 0.0, &pos), "touch A", &failures);
        expect_true(cache.insert(key_c, 0.0, 10.0, &c), "insert C triggers eviction", &failures);

        expect_true(cache.contains(key_a), "A remains after LRU eviction", &failures);
        expect_false(cache.contains(key_b), "B evicted as LRU", &failures);
        expect_true(cache.contains(key_c), "C inserted after eviction", &failures);
        expect_int(g_destroy_count, 1, "LRU eviction destroys one block", &failures);
        expect_int(g_destroyed_ids[0], 20, "LRU evicts B", &failures);
        expect_size(cache.entry_count(), 2, "two entries after eviction", &failures);
        expect_size(cache.total_bytes(), 80, "bytes after eviction", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(100);
        EphemerisRouteKey key_a(11, 10, 1, 0);
        EphemerisRouteKey key_b(12, 10, 1, 0);
        EphemerisRouteKey key_c(13, 10, 1, 0);
        StorageEphemerisBlock a = make_fake_storage(110, 10.0, 40);
        StorageEphemerisBlock b = make_fake_storage(120, 20.0, 40);
        StorageEphemerisBlock c = make_fake_storage(130, 30.0, 40);
        expect_true(cache.insert(key_a, 0.0, 10.0, &a), "insert frequency A", &failures);
        taiyin::Vector3 pos;
        for (int i = 0; i < 5; ++i) {
            expect_true(cache.eval_position(key_a, 0.0, &pos), "touch frequency A", &failures);
        }
        expect_true(cache.insert(key_b, 0.0, 10.0, &b), "insert newer cold B", &failures);
        expect_true(cache.insert(key_c, 0.0, 10.0, &c), "insert C triggers frequency eviction", &failures);

        expect_true(cache.contains(key_a), "frequent A remains despite older access", &failures);
        expect_false(cache.contains(key_b), "cold newer B evicted by frequency score", &failures);
        expect_true(cache.contains(key_c), "C inserted after frequency eviction", &failures);
        expect_int(g_destroyed_ids[0], 120, "frequency score evicts B", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(100);
        EphemerisRouteKey key_a(21, 10, 1, 0);
        EphemerisRouteKey key_b(22, 10, 1, 0);
        EphemerisRouteKey key_c(23, 10, 1, 0);
        StorageEphemerisBlock a = make_fake_storage(210, 10.0, 40);
        StorageEphemerisBlock b = make_fake_storage(220, 20.0, 40);
        StorageEphemerisBlock c = make_fake_storage(230, 30.0, 40);
        expect_true(cache.insert(key_a, 0.0, 10.0, &a, 0.0, 100), "insert high priority A", &failures);
        expect_true(cache.insert(key_b, 0.0, 10.0, &b, 0.0, 0), "insert newer low priority B", &failures);
        expect_true(cache.insert(key_c, 0.0, 10.0, &c, 0.0, 0), "insert C triggers priority eviction", &failures);

        expect_true(cache.contains(key_a), "high-priority A remains despite older access", &failures);
        expect_false(cache.contains(key_b), "newer low-priority B evicted", &failures);
        expect_true(cache.contains(key_c), "C inserted after priority eviction", &failures);
        expect_int(g_destroyed_ids[0], 220, "priority score evicts B", &failures);
    }

    {
        reset_destroy_log();
        taiyin::internal::EphemerisCachePolicy policy;
        policy.recency_weight = 0.0;
        policy.frequency_weight = 0.0;
        policy.priority_weight = 0.0;
        policy.reload_cost_weight = 0.0;
        policy.size_penalty_weight = 1.0;
        EphemerisBlockCache cache(120, policy);
        EphemerisRouteKey key_a(31, 10, 1, 0);
        EphemerisRouteKey key_b(32, 10, 1, 0);
        EphemerisRouteKey key_c(33, 10, 1, 0);
        StorageEphemerisBlock a = make_fake_storage(310, 10.0, 70);
        StorageEphemerisBlock b = make_fake_storage(320, 20.0, 30);
        StorageEphemerisBlock c = make_fake_storage(330, 30.0, 30);
        expect_true(cache.insert(key_a, 0.0, 10.0, &a), "insert large size-penalty A", &failures);
        expect_true(cache.insert(key_b, 0.0, 10.0, &b), "insert small size-penalty B", &failures);
        expect_true(cache.insert(key_c, 0.0, 10.0, &c), "insert C triggers size eviction", &failures);

        expect_false(cache.contains(key_a), "large A evicted by size penalty", &failures);
        expect_true(cache.contains(key_b), "small B remains with size penalty", &failures);
        expect_true(cache.contains(key_c), "C inserted after size eviction", &failures);
        expect_int(g_destroyed_ids[0], 310, "size penalty evicts large A", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(50);
        EphemerisRouteKey key_a(1, 10, 1, 0);
        EphemerisRouteKey key_b(2, 10, 1, 0);
        StorageEphemerisBlock a = make_fake_storage(100, 1.0, 20);
        StorageEphemerisBlock b = make_fake_storage(200, 2.0, 80);
        expect_true(cache.insert(key_a, 0.0, 10.0, &a), "insert before oversized", &failures);
        expect_true(cache.insert(key_b, 0.0, 10.0, &b), "insert oversized", &failures);
        expect_false(cache.contains(key_a), "oversized insert clears old entries", &failures);
        expect_true(cache.contains(key_b), "oversized block kept", &failures);
        expect_size(cache.entry_count(), 1, "one oversized entry", &failures);
        expect_size(cache.total_bytes(), 80, "oversized bytes kept", &failures);
        expect_int(g_destroy_count, 1, "oversized insert destroyed old block", &failures);
        expect_int(g_destroyed_ids[0], 100, "oversized destroyed A", &failures);
    }

    {
        reset_destroy_log();
        EphemerisBlockCache cache(4096);
        const int route_count = 8;
        const int reader_count = 6;
        const int iterations = 400;
        for (int i = 0; i < route_count; ++i) {
            EphemerisRouteKey key(1000 + i, 10, 1, i);
            StorageEphemerisBlock storage = make_fake_storage(1000 + i, static_cast<double>(i), 32);
            expect_true(cache.insert(key, 0.0, 10.0, &storage), "multi-user initial insert", &failures);
        }

        std::atomic<int> read_failures(0);
        std::atomic<int> write_failures(0);
        std::vector<std::thread> threads;
        for (int t = 0; t < reader_count; ++t) {
            threads.push_back(std::thread([&cache, &read_failures, t]() {
                for (int i = 0; i < iterations; ++i) {
                    const int idx = (i + t) % route_count;
                    EphemerisRouteKey key(1000 + idx, 10, 1, idx);
                    taiyin::Vector3 pos;
                    if (cache.eval_position(key, 0.5, &pos)) {
                        const double expected = static_cast<double>(idx) + 0.5;
                        if (std::fabs(pos.x - expected) > 1e-12) {
                            ++read_failures;
                        }
                    } else if (cache.contains(key)) {
                        ++read_failures;
                    }
                }
            }));
        }

        threads.push_back(std::thread([&cache, &write_failures]() {
            for (int i = 0; i < 80; ++i) {
                const int idx = i % route_count;
                EphemerisRouteKey key(1000 + idx, 10, 1, idx);
                StorageEphemerisBlock storage = make_fake_storage(2000 + i, static_cast<double>(idx), 32);
                if (!cache.insert(key, 0.0, 10.0, &storage)) {
                    ++write_failures;
                }
            }
        }));

        for (size_t i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }

        expect_int(read_failures.load(), 0, "multi-user readers saw consistent values", &failures);
        expect_int(write_failures.load(), 0, "multi-user writer inserted replacements", &failures);
        expect_size(cache.entry_count(), route_count, "multi-user route count stable", &failures);
        expect_size(cache.total_bytes(), route_count * 32, "multi-user bytes stable", &failures);

        for (int i = 0; i < route_count; ++i) {
            EphemerisRouteKey key(1000 + i, 10, 1, i);
            taiyin::Vector3 pos;
            expect_true(cache.eval_position(key, 0.5, &pos), "multi-user final route exists", &failures);
            expect_near(pos.x, static_cast<double>(i) + 0.5, "multi-user final value", &failures);
        }
    }

    {
        reset_destroy_log();
        reset_slow_eval_log();
        EphemerisBlockCache cache(4096);
        const int reader_count = 4;
        EphemerisRouteKey key(3000, 10, 1, 0);
        StorageEphemerisBlock storage = make_slow_fake_storage(3000, 3.0, 32);
        expect_true(cache.insert(key, 0.0, 10.0, &storage), "insert slow cache-hit block", &failures);

        std::atomic<bool> start(false);
        std::atomic<int> read_failures(0);
        std::vector<std::thread> threads;
        for (int i = 0; i < reader_count; ++i) {
            threads.push_back(std::thread([&]() {
                while (!start.load()) {
                    std::this_thread::yield();
                }
                taiyin::Vector3 pos;
                if (!cache.eval_position(key, 0.5, &pos)
                    || std::fabs(pos.x - 3.5) > 1e-12) {
                    read_failures.fetch_add(1);
                }
            }));
        }

        start.store(true);
        for (size_t i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }

        expect_int(read_failures.load(), 0, "slow cache-hit readers succeed", &failures);
        expect_true(g_peak_slow_evals.load() > 1, "slow cache-hit callbacks overlap", &failures);
        expect_int(g_slow_eval_entries.load(), reader_count, "slow cache-hit callback entry count", &failures);
    }

    {
        reset_destroy_log();
        reset_slow_eval_log();
        EphemerisBlockCache cache(4096);
        EphemerisRouteKey key(4000, 10, 1, 0);
        StorageEphemerisBlock storage = make_slow_fake_storage(4000, 4.0, 32);
        expect_true(cache.insert(key, 0.0, 10.0, &storage), "insert clear-during-eval block", &failures);

        std::atomic<bool> eval_finished(false);
        std::atomic<int> read_failures(0);
        taiyin::Vector3 pos;
        std::thread reader([&]() {
            if (!cache.eval_position(key, 0.5, &pos)
                || std::fabs(pos.x - 4.5) > 1e-12) {
                read_failures.fetch_add(1);
            }
            eval_finished.store(true);
        });

        while (g_slow_eval_entries.load() == 0) {
            std::this_thread::yield();
        }
        cache.clear();
        expect_false(eval_finished.load(), "clear returns while slow eval is still running", &failures);
        expect_false(cache.contains(key), "clear removes key during active eval", &failures);

        reader.join();
        expect_int(read_failures.load(), 0, "clear-during-eval reader succeeds", &failures);
        expect_int(g_destroy_count, 1, "clear-during-eval destroys once", &failures);
        expect_size(cache.entry_count(), 0, "clear-during-eval cache remains empty", &failures);
    }

    {
        reset_destroy_log();
        reset_slow_eval_log();
        EphemerisBlockCache cache(4096);
        const int reader_count = 4;
        EphemerisRouteKey key(5000, 10, 1, 0);
        StorageEphemerisBlock storage = make_slow_fake_storage(5000, 5.0, 32);
        expect_true(cache.insert(key, 0.0, 10.0, &storage), "insert slow eval-state block", &failures);

        std::atomic<bool> start(false);
        std::atomic<int> read_failures(0);
        std::vector<std::thread> threads;
        for (int i = 0; i < reader_count; ++i) {
            threads.push_back(std::thread([&]() {
                while (!start.load()) {
                    std::this_thread::yield();
                }
                taiyin::CartesianState state;
                if (!cache.eval_state(key, 0.5, &state)
                    || std::fabs(state.position_au.x - 5.5) > 1e-12
                    || std::fabs(state.velocity_au_per_day.x - 5.0) > 1e-12
                    || std::fabs(state.acceleration_au_per_day2.x - 15.0) > 1e-12) {
                    read_failures.fetch_add(1);
                }
            }));
        }

        start.store(true);
        for (size_t i = 0; i < threads.size(); ++i) {
            threads[i].join();
        }

        expect_int(read_failures.load(), 0, "slow eval-state readers succeed", &failures);
        expect_true(g_peak_slow_evals.load() > 1, "slow eval-state callbacks overlap", &failures);
        expect_int(g_slow_eval_entries.load(), reader_count, "slow eval-state callback entry count", &failures);
    }

    if (failures == 0) {
        std::cout << "All ephemeris_cache tests PASSED!\n";
        return 0;
    }

    std::cerr << failures << " ephemeris_cache tests FAILED!\n";
    return 1;
}
