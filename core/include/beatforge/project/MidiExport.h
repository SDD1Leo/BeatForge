#pragma once

#include "beatforge/midi/MidiFile.h"
#include "beatforge/project/Project.h"

#include <filesystem>
#include <string>

namespace beatforge::project
{
    // The arrangement as a Standard MIDI File (format 1, 960 PPQ): what you
    // hear, from bar 1 to the end of the last clip.
    //
    //  - Track 1: song name, tempo, time signature.
    //  - One track per audible synth track (muted / not-soloed tracks are left
    //    out): its name, a GM program (Poly → Lead 2 sawtooth, Bass → Synth
    //    Bass 1), notes as played (cut at clip ends, later clips take over).
    //    Channels 1-9, 11-16 in track order.
    //  - Drum tracks on channel 10 with General MIDI notes: kick 36, snare 38,
    //    closed hat 42, open hat 46, clap 39 (velocity 100, 1/32 long);
    //    muted lanes are left out.
    struct MidiExport
    {
        midi::MidiFile file;
        int tracksWritten = 0;   // not counting the tempo track
        int notes = 0;
        time::Tick length = 0;
    };

    MidiExport exportMidi (const Project&);

    // Writes atomically (temporary file, then rename). Empty string = success.
    std::string saveMidiFile (const Project&, const std::filesystem::path&);

    constexpr int gmDrumNote (int lane) noexcept
    {
        constexpr int notes[] { 36, 38, 42, 46, 39 };   // DrumSound order
        return lane >= 0 && lane < 5 ? notes[lane] : 36;
    }
}
