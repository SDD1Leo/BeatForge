#include "beatforge/project/Project.h"

namespace beatforge::project
{
    std::array<DrumLane, audio::DrumMachine::numLanes> Project::defaultLanes()
    {
        std::array<DrumLane, audio::DrumMachine::numLanes> lanes;

        for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
            lanes[(size_t) i].name = std::string (audio::drumSoundName ((audio::DrumSound) i));

        return lanes;
    }
}
