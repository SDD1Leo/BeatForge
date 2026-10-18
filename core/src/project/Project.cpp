#include "beatforge/project/Project.h"

namespace beatforge::project
{
    std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> Track::defaultInstrumentParams()
    {
        std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> all {};
        for (int t = 0; t < audio::numInstrumentTypes; ++t)
            for (int p = 0; p < audio::numParams; ++p)
                all[(size_t) t][(size_t) p] = audio::defaultValue ((audio::InstrumentType) t, (audio::Param) p);
        return all;
    }

    std::array<DrumLane, audio::DrumMachine::numLanes> Track::defaultLanes()
    {
        std::array<DrumLane, audio::DrumMachine::numLanes> lanes;
        for (int i = 0; i < audio::DrumMachine::numLanes; ++i)
            lanes[(size_t) i].name = std::string (audio::drumSoundName ((audio::DrumSound) i));
        return lanes;
    }

    Project Project::makeDefault()
    {
        Project p;

        Track drums;
        drums.id = 1;
        drums.type = TrackType::drums;
        drums.name = "Drums";
        Clip beat;
        beat.id = 1;
        beat.start = 0;
        beat.length = patternTicks;
        drums.clips.push_back (beat);

        Track synth;
        synth.id = 2;
        synth.type = TrackType::instrument;
        synth.name = "Synth";

        p.tracks = { drums, synth };
        return p;
    }
}
