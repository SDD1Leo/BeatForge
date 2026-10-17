#include "AllocationGuard.h"

#include <cstdlib>
#include <new>

namespace
{
    thread_local bool counting = false;
    thread_local std::size_t allocations = 0;

    void* countedAlloc (std::size_t size)
    {
        if (counting)
            ++allocations;

        if (auto* p = std::malloc (size == 0 ? 1 : size))
            return p;

        throw std::bad_alloc();
    }
}

void* operator new (std::size_t size)                               { return countedAlloc (size); }
void* operator new[] (std::size_t size)                             { return countedAlloc (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return countedAlloc (size); } catch (...) { return nullptr; }
}
void operator delete (void* p) noexcept                             { std::free (p); }
void operator delete[] (void* p) noexcept                           { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept              { std::free (p); }

namespace beatforge::test
{
    ScopedAllocationCounter::ScopedAllocationCounter()
        : wasCounting (counting), startCount (allocations)
    {
        counting = true;
    }

    ScopedAllocationCounter::~ScopedAllocationCounter() { counting = wasCounting; }

    std::size_t ScopedAllocationCounter::count() const  { return allocations - startCount; }
}
