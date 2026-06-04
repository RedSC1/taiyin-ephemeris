#include "taiyin/internal/route_inflight_map.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_int(int actual, int expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void test_same_key_waiters_share_loader(int* failures) {
    taiyin::internal::RouteInflightMap inflight;
    taiyin::internal::EphemerisRouteKey key(1, 0, 10, 100);

    taiyin::internal::RouteInflightGuard loader(inflight, key);
    expect_true(loader.begin() == taiyin::internal::RouteInflightLoadNow,
                "first same-key caller becomes loader", failures);

    std::atomic<int> ready(0);
    std::atomic<int> finished(0);
    std::atomic<int> load_now_count(0);
    std::vector<std::thread> waiters;
    for (int i = 0; i < 5; ++i) {
        waiters.push_back(std::thread([&]() {
            ready.fetch_add(1);
            const taiyin::internal::RouteInflightAction action = inflight.acquire(key);
            if (action == taiyin::internal::RouteInflightLoadNow) {
                load_now_count.fetch_add(1);
                inflight.release(key, true);
            } else {
                finished.fetch_add(1);
            }
        }));
    }

    while (ready.load() < 5) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    loader.end(true);

    for (size_t i = 0; i < waiters.size(); ++i) {
        waiters[i].join();
    }

    expect_int(load_now_count.load(), 0, "same-key waiters do not become loaders", failures);
    expect_int(finished.load(), 5, "same-key waiters wake after loader release", failures);
}

void test_different_keys_load_independently(int* failures) {
    taiyin::internal::RouteInflightMap inflight;
    std::atomic<bool> start(false);
    std::atomic<int> load_now_count(0);
    std::atomic<int> active_loaders(0);
    std::atomic<int> peak_loaders(0);
    std::vector<std::thread> loaders;

    for (int i = 0; i < 4; ++i) {
        loaders.push_back(std::thread([&, i]() {
            taiyin::internal::EphemerisRouteKey key(100 + i, 0, 10, 1);
            while (!start.load()) {
                std::this_thread::yield();
            }

            taiyin::internal::RouteInflightGuard guard(inflight, key);
            if (guard.begin() == taiyin::internal::RouteInflightLoadNow) {
                load_now_count.fetch_add(1);
                const int current = active_loaders.fetch_add(1) + 1;
                int observed = peak_loaders.load();
                while (current > observed
                       && !peak_loaders.compare_exchange_weak(observed, current)) {}
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                active_loaders.fetch_sub(1);
                guard.end(true);
            }
        }));
    }

    start.store(true);
    for (size_t i = 0; i < loaders.size(); ++i) {
        loaders[i].join();
    }

    expect_int(load_now_count.load(), 4, "different keys each get a loader", failures);
    expect_true(peak_loaders.load() > 1, "different-key loaders overlap", failures);
}

void test_failure_wakes_and_allows_retry(int* failures) {
    taiyin::internal::RouteInflightMap inflight;
    taiyin::internal::EphemerisRouteKey key(2, 0, 10, 100);

    taiyin::internal::RouteInflightGuard loader(inflight, key);
    expect_true(loader.begin() == taiyin::internal::RouteInflightLoadNow,
                "failure test initial loader", failures);

    std::atomic<bool> waiter_ready(false);
    std::atomic<bool> waiter_finished(false);
    std::thread waiter([&]() {
        waiter_ready.store(true);
        const taiyin::internal::RouteInflightAction action = inflight.acquire(key);
        if (action == taiyin::internal::RouteInflightLoadFinished) {
            waiter_finished.store(true);
        }
    });

    while (!waiter_ready.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    loader.end(false);
    waiter.join();

    expect_true(waiter_finished.load(), "failed load wakes waiter", failures);

    taiyin::internal::RouteInflightGuard retry(inflight, key);
    expect_true(retry.begin() == taiyin::internal::RouteInflightLoadNow,
                "same key can retry after failed load", failures);
    retry.end(true);
}

void test_guard_cleanup_wakes_waiters(int* failures) {
    taiyin::internal::RouteInflightMap inflight;
    taiyin::internal::EphemerisRouteKey key(3, 0, 10, 100);
    std::atomic<bool> waiter_ready(false);
    std::atomic<bool> waiter_finished(false);

    std::thread waiter;
    {
        taiyin::internal::RouteInflightGuard loader(inflight, key);
        expect_true(loader.begin() == taiyin::internal::RouteInflightLoadNow,
                    "guard cleanup initial loader", failures);
        waiter = std::thread([&]() {
            waiter_ready.store(true);
            const taiyin::internal::RouteInflightAction action = inflight.acquire(key);
            if (action == taiyin::internal::RouteInflightLoadFinished) {
                waiter_finished.store(true);
            }
        });
        while (!waiter_ready.load()) {
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    waiter.join();
    expect_true(waiter_finished.load(), "guard destructor wakes waiter", failures);
}

}  // namespace

int main() {
    int failures = 0;
    test_same_key_waiters_share_loader(&failures);
    test_different_keys_load_independently(&failures);
    test_failure_wakes_and_allows_retry(&failures);
    test_guard_cleanup_wakes_waiters(&failures);

    if (failures != 0) {
        std::cerr << failures << " route inflight map test(s) failed\n";
        return 1;
    }
    return 0;
}
