#pragma once

#include <atomic>
#include <memory>

namespace beatforge::util
{
    // Hands immutable objects from the message thread to the audio thread
    // without locks, allocation or deallocation on the audio thread.
    //
    //  message thread: publish(), collectGarbage()
    //  audio thread:   acquire()
    //
    // The audio thread swaps a pending object in at a block start and parks the
    // previous one in a "retired" slot that only the message thread empties; a
    // newer object waits until that slot is free.
    template <typename T>
    class LockFreeExchange
    {
    public:
        LockFreeExchange() = default;
        ~LockFreeExchange()
        {
            delete pending.exchange (nullptr);
            delete retired.exchange (nullptr);
            delete current;
        }

        void publish (std::unique_ptr<T> object)
        {
            // Whatever was pending was never seen by the audio thread: safe to delete.
            delete pending.exchange (object.release(), std::memory_order_acq_rel);
            collectGarbage();
        }

        void collectGarbage()
        {
            delete retired.exchange (nullptr, std::memory_order_acq_rel);
        }

        const T* acquire (bool& changed) noexcept
        {
            changed = false;
            if (pending.load (std::memory_order_acquire) != nullptr && retired.load (std::memory_order_acquire) == nullptr)
            {
                if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
                {
                    retired.store (current, std::memory_order_release);
                    current = next;
                    changed = true;
                }
            }
            return current;
        }

    private:
        std::atomic<T*> pending { nullptr };
        std::atomic<T*> retired { nullptr };
        T* current = nullptr;   // audio thread only

        LockFreeExchange (const LockFreeExchange&) = delete;
        LockFreeExchange& operator= (const LockFreeExchange&) = delete;
    };
}
