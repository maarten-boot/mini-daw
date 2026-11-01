#pragma once

// Lock-free single-producer / single-consumer ring buffer (SPEC-NF-01, SPEC-REC-05).
//
// The audio thread writes, the disk thread reads. write() and read() never
// block, allocate or take locks. Exactly one thread may call write() and
// exactly one (other) thread may call read() at a time.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>

namespace minidaw {

// The padding between the two counters is intended (separate cache lines),
// so MSVC's "structure was padded due to alignment specifier" is switched off.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

template <typename T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing copies elements with memcpy");

public:
    // Capacity is rounded up to a power of two. Allocates; call before the
    // audio thread starts using the buffer.
    explicit SpscRing(std::size_t minCapacity)
    {
        std::size_t cap = 1;
        while (cap < minCapacity) cap <<= 1;
        capacity_ = cap;
        mask_ = cap - 1;
        data_ = std::make_unique<T[]>(cap);
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    std::size_t capacity() const { return capacity_; }

    // Elements that can be written now (producer side).
    std::size_t writeSpace() const
    {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        return capacity_ - (w - r);
    }

    // Elements that can be read now (consumer side).
    std::size_t readAvailable() const
    {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        return w - r;
    }

    // Writes all `count` elements, or nothing if there is not enough space.
    bool write(const T* src, std::size_t count)
    {
        const std::size_t w = write_.load(std::memory_order_relaxed);
        const std::size_t r = read_.load(std::memory_order_acquire);
        if (capacity_ - (w - r) < count) return false;
        copyIn(w, src, count);
        write_.store(w + count, std::memory_order_release);
        return true;
    }

    // Reserve space for `count` elements and let the caller fill them
    // element by element (used to de-interleave without a temporary buffer).
    // Returns false if there is not enough space. Call commitWrite(count) after.
    bool canWrite(std::size_t count) const { return writeSpace() >= count; }
    T& slot(std::size_t offset) { return data_[(write_.load(std::memory_order_relaxed) + offset) & mask_]; }
    void commitWrite(std::size_t count)
    {
        write_.store(write_.load(std::memory_order_relaxed) + count, std::memory_order_release);
    }

    // Reads up to `maxCount` elements; returns how many were read.
    std::size_t read(T* dst, std::size_t maxCount)
    {
        const std::size_t r = read_.load(std::memory_order_relaxed);
        const std::size_t w = write_.load(std::memory_order_acquire);
        const std::size_t count = std::min(maxCount, w - r);
        const std::size_t start = r & mask_;
        const std::size_t first = std::min(count, capacity_ - start);
        std::memcpy(dst, data_.get() + start, first * sizeof(T));
        std::memcpy(dst + first, data_.get(), (count - first) * sizeof(T));
        read_.store(r + count, std::memory_order_release);
        return count;
    }

private:
    void copyIn(std::size_t w, const T* src, std::size_t count)
    {
        const std::size_t start = w & mask_;
        const std::size_t first = std::min(count, capacity_ - start);
        std::memcpy(data_.get() + start, src, first * sizeof(T));
        std::memcpy(data_.get(), src + first, (count - first) * sizeof(T));
    }

    std::unique_ptr<T[]> data_;
    std::size_t capacity_ = 0;
    std::size_t mask_ = 0;
    // Monotonic counters; the difference is the fill level. Separate cache
    // lines so producer and consumer do not slow each other down.
    alignas(64) std::atomic<std::size_t> write_{0};
    alignas(64) std::atomic<std::size_t> read_{0};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace minidaw
