#pragma once

#include "beatforge/util/SpscQueue.h"

namespace beatforge::audio
{
    // Note auditions from the editor (message thread → audio thread). When
    // full, new requests are dropped (an audition is never essential).
    struct PreviewRequest
    {
        int slot = 0;       // track slot
        int pitch = 60;
        int velocity = 0;   // 0 = note off
    };

    using PreviewQueue = util::SpscQueue<PreviewRequest, 64>;
}
