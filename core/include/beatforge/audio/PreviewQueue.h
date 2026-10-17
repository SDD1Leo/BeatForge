#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace beatforge::audio
{
    // Single-producer (message thread) / single-consumer (audio thread) queue
    // of note auditions from the editor. Lock-free, fixed size; when full, new
    // requests are dropped (an audition is never essential).
    class PreviewQueue
    {
    public:
        struct Request
        {
            int pitch = 60;
            int velocity = 0;   // 0 = note off
        };

        bool push (Request r) noexcept
        {
            const auto w = writeIndex.load (std::memory_order_relaxed);
            const auto next = (w + 1) % capacity;
            if (next == readIndex.load (std::memory_order_acquire))
                return false;

            items[w] = r;
            writeIndex.store (next, std::memory_order_release);
            return true;
        }

        bool pop (Request& r) noexcept
        {
            const auto rd = readIndex.load (std::memory_order_relaxed);
            if (rd == writeIndex.load (std::memory_order_acquire))
                return false;

            r = items[rd];
            readIndex.store ((rd + 1) % capacity, std::memory_order_release);
            return true;
        }

    private:
        static constexpr std::uint32_t capacity = 64;
        std::array<Request, capacity> items {};
        std::atomic<std::uint32_t> readIndex { 0 }, writeIndex { 0 };
    };
}
