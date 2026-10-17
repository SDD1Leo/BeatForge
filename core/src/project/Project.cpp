#include "beatforge/project/Project.h"

namespace beatforge::project
{
    std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> Project::defaultInstrumentParams()
    {
        std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> all {};
        for (int t = 0; t < audio::numInstrumentTypes; ++t)
            for (int p = 0; p < audio::numParams; ++p)
                all[(size_t) t][(size_t) p] = audio::defaultValue ((audio::InstrumentType) t, (audio::Param) p);
        return all;
    }

    std::array<DrumLane, audio::DrumMachine::numLanes> Project::defaultLanes()
    {
        std::array<DrumLane, audio::DrumMachine::numLanes> lanes;

        for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
            lanes[(size_t) i].name = std::string (audio::drumSoundName ((audio::DrumSound) i));

        return lanes;
    }
}
