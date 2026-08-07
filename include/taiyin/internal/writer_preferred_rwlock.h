#ifndef TAIYIN_INTERNAL_WRITER_PREFERRED_RWLOCK_H
#define TAIYIN_INTERNAL_WRITER_PREFERRED_RWLOCK_H

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace taiyin {
namespace internal {

class WriterPreferredRwLock {
public:
    WriterPreferredRwLock() noexcept
        : active_readers_(0), waiting_writers_(0), writer_active_(false) {}

    WriterPreferredRwLock(const WriterPreferredRwLock&) = delete;
    WriterPreferredRwLock& operator=(const WriterPreferredRwLock&) = delete;

    void lock_read() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        while (writer_active_ || waiting_writers_ > 0) {
            readers_cv_.wait(lock);
        }
        ++active_readers_;
    }

    void unlock_read() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        if (active_readers_ > 0) {
            --active_readers_;
        }
        if (active_readers_ == 0) {
            writers_cv_.notify_one();
        }
    }

    void lock_write() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        ++waiting_writers_;
        while (writer_active_ || active_readers_ > 0) {
            writers_cv_.wait(lock);
        }
        --waiting_writers_;
        writer_active_ = true;
    }

    void unlock_write() noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        writer_active_ = false;
        if (waiting_writers_ > 0) {
            writers_cv_.notify_one();
        } else {
            readers_cv_.notify_all();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable readers_cv_;
    std::condition_variable writers_cv_;
    size_t active_readers_;
    size_t waiting_writers_;
    bool writer_active_;
};

class ReadLockGuard {
public:
    explicit ReadLockGuard(WriterPreferredRwLock& lock) noexcept : lock_(lock) {
        lock_.lock_read();
    }

    ~ReadLockGuard() noexcept {
        lock_.unlock_read();
    }

    ReadLockGuard(const ReadLockGuard&) = delete;
    ReadLockGuard& operator=(const ReadLockGuard&) = delete;

private:
    WriterPreferredRwLock& lock_;
};

class WriteLockGuard {
public:
    explicit WriteLockGuard(WriterPreferredRwLock& lock) noexcept : lock_(lock) {
        lock_.lock_write();
    }

    ~WriteLockGuard() noexcept {
        lock_.unlock_write();
    }

    WriteLockGuard(const WriteLockGuard&) = delete;
    WriteLockGuard& operator=(const WriteLockGuard&) = delete;

private:
    WriterPreferredRwLock& lock_;
};

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_WRITER_PREFERRED_RWLOCK_H
