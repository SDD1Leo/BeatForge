#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace beatforge::util
{
    // Single-producer / single-consumer queue. Lock-free and fixed size; push
    // fails (returns false) when full. Safe between exactly one producer
    // thread and one consumer thread at a time (producers serialised by a lock
    // elsewhere also count as one).
    template <typename T, std::uint32_t Capacity>
    class SpscQueue
    {
    public:
        bool push (const T& item) noexcept
        {
            const auto w = writeIndex.load (std::memory_order_relaxed);
            const auto next = (w + 1) % Capacity;
            if (next == readIndex.load (std::memory_order_acquire))
                return false;

            items[w] = item;
            writeIndex.store (next, std::memory_order_release);
            return true;
        }

        bool pop (T& item) noexcept
        {
            const auto r = readIndex.load (std::memory_order_relaxed);
            if (r == writeIndex.load (std::memory_order_acquire))
                return false;

            item = items[r];
            readIndex.store ((r + 1) % Capacity, std::memory_order_release);
            return true;
        }

        // Consumer side: look at the next item without removing it.
        const T* peek() const noexcept
        {
            const auto r = readIndex.load (std::memory_order_relaxed);
            return r == writeIndex.load (std::memory_order_acquire) ? nullptr : &items[r];
        }

    private:
        std::array<T, Capacity> items {};
        std::atomic<std::uint32_t> readIndex { 0 }, writeIndex { 0 };
    };
}
