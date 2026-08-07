#include "taiyin/internal/writer_preferred_rwlock.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

namespace {

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

void test_concurrent_readers(int* failures) {
    taiyin::internal::WriterPreferredRwLock lock;
    const int reader_count = 8;
    std::atomic<bool> start(false);
    std::atomic<int> active_readers(0);
    std::atomic<int> peak_readers(0);
    std::vector<std::thread> threads;

    for (int i = 0; i < reader_count; ++i) {
        threads.push_back(std::thread([&]() {
            while (!start.load()) {
                std::this_thread::yield();
            }

            taiyin::internal::ReadLockGuard guard(lock);
            const int active = active_readers.fetch_add(1) + 1;
            int peak = peak_readers.load();
            while (active > peak && !peak_readers.compare_exchange_weak(peak, active)) {
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            active_readers.fetch_sub(1);
        }));
    }

    start.store(true);
    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    expect_true(peak_readers.load() > 1, "read locks allow concurrent readers", failures);
}

void test_writer_excludes_readers(int* failures) {
    taiyin::internal::WriterPreferredRwLock lock;
    std::atomic<bool> reader_entered(false);

    lock.lock_write();
    std::thread reader([&]() {
        taiyin::internal::ReadLockGuard guard(lock);
        reader_entered.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    expect_false(reader_entered.load(), "reader waits while writer is active", failures);

    lock.unlock_write();
    reader.join();
    expect_true(reader_entered.load(), "reader enters after writer exits", failures);
}

void test_writer_preference_blocks_new_readers(int* failures) {
    taiyin::internal::WriterPreferredRwLock lock;
    std::atomic<bool> writer_started(false);
    std::atomic<int> order(0);
    std::atomic<int> writer_order(0);
    std::atomic<int> reader_order(0);

    lock.lock_read();

    std::thread writer([&]() {
        writer_started.store(true);
        taiyin::internal::WriteLockGuard guard(lock);
        writer_order.store(++order);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    });

    while (!writer_started.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    std::thread late_reader([&]() {
        taiyin::internal::ReadLockGuard guard(lock);
        reader_order.store(++order);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    lock.unlock_read();

    writer.join();
    late_reader.join();

    expect_int(writer_order.load(), 1, "waiting writer enters before late reader", failures);
    expect_int(reader_order.load(), 2, "late reader enters after waiting writer", failures);
}

void test_writers_serialize(int* failures) {
    taiyin::internal::WriterPreferredRwLock lock;
    const int writer_count = 4;
    const int iterations = 1000;
    int counter = 0;
    std::vector<std::thread> threads;

    for (int i = 0; i < writer_count; ++i) {
        threads.push_back(std::thread([&]() {
            for (int j = 0; j < iterations; ++j) {
                taiyin::internal::WriteLockGuard guard(lock);
                const int next = counter + 1;
                counter = next;
            }
        }));
    }

    for (size_t i = 0; i < threads.size(); ++i) {
        threads[i].join();
    }

    expect_int(counter, writer_count * iterations, "writers serialize shared mutation", failures);
}

}  // namespace

int main() {
    int failures = 0;

    test_concurrent_readers(&failures);
    test_writer_excludes_readers(&failures);
    test_writer_preference_blocks_new_readers(&failures);
    test_writers_serialize(&failures);

    if (failures == 0) {
        std::cout << "All writer_preferred_rwlock tests PASSED!\n";
        return 0;
    }

    std::cerr << failures << " writer_preferred_rwlock tests FAILED!\n";
    return 1;
}
