#pragma once

#include <cstddef>

namespace beatforge::test
{
    // Counts heap allocations made on the current thread while alive.
    // Backed by a global operator new replacement in AllocationGuard.cpp.
    class ScopedAllocationCounter
    {
    public:
        ScopedAllocationCounter();
        ~ScopedAllocationCounter();

        std::size_t count() const;

    private:
        bool wasCounting;
        std::size_t startCount;
    };
}
