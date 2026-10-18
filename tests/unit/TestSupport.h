#pragma once

#ifdef _WIN32
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace beatforge::test
{
    // This test process's id, for unique temporary names.
    inline long processId()
    {
       #ifdef _WIN32
        return (long) ::_getpid();
       #else
        return (long) ::getpid();
       #endif
    }
}
