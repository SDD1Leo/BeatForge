#pragma once

#include "beatforge/audio/DrumMachine.h"
#include "beatforge/audio/Instruments.h"
#include "beatforge/midi/Note.h"
#include "beatforge/time/MusicalTime.h"
#include "beatforge/time/TempoMap.h"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace beatforge::project
{
    using TrackId = std::uint32_t;
    using ClipId = std::uint32_t;

    enum class TrackType { drums, instrument };

    constexpr int stepsPerPattern = audio::DrumMachine::numSteps;   // 16 sixteenths
    constexpr time::Tick patternTicks = stepsPerPattern * 240;      // one 4/4 bar

    // Mixer-like settings of one drum sound on a drum track.
    struct DrumLane
    {
        std::string name;
        float gainDb = 0.0f;
        bool muted = false;
        bool soloed = false;
        std::string extra = "{}";   // unknown keys (JSON object)

        bool operator== (const DrumLane&) const = default;
    };

    using DrumPattern = std::array<std::array<bool, stepsPerPattern>, audio::DrumMachine::numLanes>;

    // A clip on a track's timeline. A drum clip plays its 16-step pattern,
    // repeating every bar for the clip's length; an instrument clip plays its
    // notes (clip-local ticks; notes beyond the clip length are kept but silent).
    struct Clip
    {
        ClipId id = 0;
        time::Tick start = 0;        // on the song timeline
        time::Tick length = patternTicks;

        DrumPattern pattern {};                         // drum clips
        std::vector<midi::Note> notes;                  // instrument clips
        std::map<midi::NoteId, std::string> noteExtra;  // unknown keys per note
        std::string invalidNotes = "[]";                // unreadable notes, kept verbatim
        std::string extra = "{}";

        time::Tick end() const noexcept { return start + length; }
        bool operator== (const Clip&) const = default;
    };

    struct Track
    {
        TrackId id = 0;
        TrackType type = TrackType::drums;
        std::string name;
        float volumeDb = 0.0f;   // -60 (silent) .. +6
        float pan = 0.0f;        // -1 (left) .. +1 (right)
        bool muted = false;
        bool soloed = false;

        // Drum tracks.
        std::array<DrumLane, audio::DrumMachine::numLanes> lanes = defaultLanes();
        std::string extraLanes = "[]";

        // Instrument tracks: the chosen instrument and the settings of each.
        audio::InstrumentType instrument = audio::InstrumentType::polySynth;
        std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> instrumentParams = defaultInstrumentParams();
        std::array<std::string, audio::numInstrumentTypes> extraInstrumentParams { "{}", "{}" };

        std::vector<Clip> clips;   // kept sorted by start
        std::string extra = "{}";

        static std::array<DrumLane, audio::DrumMachine::numLanes> defaultLanes();
        static std::array<std::array<float, audio::numParams>, audio::numInstrumentTypes> defaultInstrumentParams();

        bool operator== (const Track&) const = default;
    };

    // Everything a project file preserves. Plain data: no GUI, no engine.
    // Unknown fields found when loading are kept in the `extra*` JSON strings
    // and written back on save, so data from newer versions is not lost.
    struct Project
    {
        std::string name = "Untitled";
        double tempo = time::defaultTempo;
        time::TimeSignature timeSignature;
        bool metronomeEnabled = false;
        float masterVolumeDb = 0.0f;      // applied to the sum of the tracks (not the metronome)
        bool loopEnabled = false;
        time::Tick loopStart = 0, loopEnd = 4 * patternTicks;

        std::vector<Track> tracks;

        std::string extraTopLevel = "{}";   // unknown top-level keys
        std::string extraTracks = "[]";     // tracks of types this version cannot play

        // A new project: a Drums track with an empty one-bar clip, and a Synth track.
        static Project makeDefault();

        static constexpr int maxTracks = 16;
        static constexpr int maxNameLength = 64;
        static constexpr int maxLaneNameLength = 32;
        static constexpr time::Tick maxClipLength = 64 * patternTicks;
        static constexpr float minVolumeDb = -60.0f, maxVolumeDb = 6.0f;

        bool operator== (const Project&) const = default;
    };
}
