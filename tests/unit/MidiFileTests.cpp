#include <beatforge/midi/MidiFile.h>
#include <beatforge/project/MidiExport.h>
#include <beatforge/project/Project.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <map>

using namespace beatforge;
using midi::MidiFile;

namespace
{
    std::string text (const MidiFile::Event& e)
    {
        const auto d = e.metaData();
        return { d.begin(), d.end() };
    }

    std::string trackName (const MidiFile::Track& t)
    {
        for (auto& e : t.events)
            if (e.metaType() == MidiFile::metaTrackName)
                return text (e);
        return {};
    }

    struct NoteSpan { std::int64_t start, end; int pitch, velocity, channel; };

    std::vector<NoteSpan> notesOf (const MidiFile::Track& t)
    {
        std::vector<NoteSpan> out;
        std::map<int, size_t> open;
        for (auto& e : t.events)
        {
            if (e.isNoteOn())
            {
                open[e.bytes[1]] = out.size();
                out.push_back ({ e.tick, -1, e.bytes[1], e.bytes[2], e.channel() });
            }
            else if (e.isNoteOff() && open.count (e.bytes[1]) > 0)
            {
                out[open[e.bytes[1]]].end = e.tick;
                open.erase (e.bytes[1]);
            }
        }
        return out;
    }

    project::Project song()
    {
        auto p = project::Project::makeDefault();
        p.name = "Night Drive";
        p.tempo = 124.0;
        p.timeSignature = { 4, 4 };
        auto& drums = p.tracks[0];
        drums.name = "Beat";
        drums.clips[0].length = 2 * 3840;             // bar 1-2, pattern repeats
        drums.clips[0].pattern = {};
        drums.clips[0].pattern[0][0] = drums.clips[0].pattern[0][8] = true;   // kick
        drums.clips[0].pattern[4][4] = true;                                  // clap
        drums.clips[0].pattern[1][12] = true;                                 // snare (lane muted below)
        drums.lanes[1].muted = true;

        auto& synth = p.tracks[1];
        synth.name = "Bass";
        synth.instrument = audio::InstrumentType::monoBass;
        project::Clip c;
        c.id = 7;
        c.start = 3840;                                // bar 2
        c.length = 3840;
        c.notes = { { 1, 36, 110, 0, 960 }, { 2, 43, 80, 3360, 960 } };       // second one is cut at the clip end
        synth.clips = { c };
        return p;
    }
}

TEST_CASE ("MIDI files round-trip through write and read", "[midi][file]")
{
    MidiFile f;
    f.ticksPerQuarter = 480;
    MidiFile::Track t1, t2;
    t1.addText (0, MidiFile::metaTrackName, "Tempo");
    t1.addMeta (0, MidiFile::metaTempo, { 0x07, 0xa1, 0x20 });
    t2.add (0, { 0x90, 60, 100 });
    t2.add (300000, { 0x80, 60, 64 });               // a delta needing 3 varlen bytes
    t2.add (300000, { 0xc1, 5 });
    f.tracks = { t1, t2 };

    std::string error;
    const auto bytes = f.write();
    REQUIRE (bytes[9] == 1);                           // format 1
    const auto back = MidiFile::read (bytes, error);
    REQUIRE (back.has_value());
    REQUIRE (back->ticksPerQuarter == 480);
    REQUIRE (back->tracks.size() == 2);
    REQUIRE (trackName (back->tracks[0]) == "Tempo");
    REQUIRE (back->tracks[1].events.size() == 3);      // End of Track is not reported
    REQUIRE (back->tracks[1].events[1].tick == 300000);
    REQUIRE (back->tracks[1].events[2].bytes == std::vector<std::uint8_t> { 0xc1, 5 });
}

TEST_CASE ("Broken MIDI files are refused with a reason", "[midi][file]")
{
    std::string error;
    REQUIRE_FALSE (MidiFile::read ({ 'h', 'e', 'l', 'l', 'o' }, error).has_value());
    REQUIRE (error.find ("Not a Standard MIDI File") != std::string::npos);

    auto bytes = project::exportMidi (song()).file.write();
    bytes.resize (bytes.size() - 20);
    REQUIRE_FALSE (MidiFile::read (bytes, error).has_value());
    REQUIRE (error.find ("truncated") != std::string::npos);
}

TEST_CASE ("The arrangement exports with tempo, time signature, names and notes as played", "[midi][export]")
{
    auto p = song();
    p.timeSignature = { 3, 4 };                         // 3/4: bars of 2880 ticks
    p.tracks[0].clips[0].length = 2 * 2880;
    p.tracks[1].clips[0].start = 2880;
    p.tracks[1].clips[0].length = 2880;
    p.tracks[1].clips[0].notes[1].start = 2400;        // runs past the clip end

    const auto exported = project::exportMidi (p);
    std::string error;
    const auto f = MidiFile::read (exported.file.write(), error);
    REQUIRE (f.has_value());
    REQUIRE (f->ticksPerQuarter == 960);
    REQUIRE (f->tracks.size() == 3);
    REQUIRE (exported.tracksWritten == 2);
    REQUIRE (exported.length == 2 * 2880);

    // Conductor track.
    const auto& tempo = f->tracks[0];
    REQUIRE (trackName (tempo) == "Night Drive");
    bool sawTempo = false, sawSignature = false;
    for (auto& e : tempo.events)
    {
        if (e.metaType() == MidiFile::metaTempo)
        {
            const auto d = e.metaData();
            REQUIRE ((d[0] << 16 | d[1] << 8 | d[2]) == 483871);   // 60e6 / 124
            sawTempo = true;
        }
        if (e.metaType() == MidiFile::metaTimeSignature)
        {
            REQUIRE (e.metaData() == std::vector<std::uint8_t> { 3, 2, 24, 8 });
            sawSignature = true;
        }
    }
    REQUIRE (sawTempo);
    REQUIRE (sawSignature);

    // Drums: GM notes on channel 10, pattern repeating every bar, muted snare left out.
    const auto& drums = f->tracks[1];
    REQUIRE (trackName (drums) == "Beat");
    const auto hits = notesOf (drums);
    std::vector<std::pair<std::int64_t, int>> got;
    for (auto& h : hits)
    {
        REQUIRE (h.channel == 9);
        REQUIRE (h.end == h.start + 120);
        got.push_back ({ h.start, h.pitch });
    }
    // As played: the 16-step pattern repeats every 16 sixteenths (3840 ticks),
    // also in 3/4 (a known limitation), until the clip ends at 5760.
    REQUIRE (got == std::vector<std::pair<std::int64_t, int>> {
        { 0, 36 }, { 960, 39 }, { 1920, 36 }, { 3840, 36 }, { 4800, 39 } });

    // Synth: program, channel 1, notes cut at the clip end.
    const auto& bass = f->tracks[2];
    REQUIRE (trackName (bass) == "Bass");
    bool sawProgram = false;
    for (auto& e : bass.events)
        if (e.bytes.size() == 2 && e.bytes[0] == 0xc0) { REQUIRE (e.bytes[1] == 38); sawProgram = true; }
    REQUIRE (sawProgram);
    const auto notes = notesOf (bass);
    REQUIRE (notes.size() == 2);
    REQUIRE (notes[0].start == 2880);
    REQUIRE (notes[0].end == 2880 + 960);
    REQUIRE (notes[0].velocity == 110);
    REQUIRE (notes[0].channel == 0);
    REQUIRE (notes[1].start == 2880 + 2400);
    REQUIRE (notes[1].end == 2 * 2880);               // cut by the clip end

    // Every track ends at the song end.
    for (auto& t : f->tracks)
        REQUIRE (t.endTick() <= exported.length);
}

TEST_CASE ("Export follows mute and solo like playback", "[midi][export]")
{
    auto p = song();
    p.tracks[1].muted = true;
    REQUIRE (project::exportMidi (p).tracksWritten == 1);

    p.tracks[1].muted = false;
    p.tracks[1].soloed = true;
    const auto soloed = project::exportMidi (p);
    REQUIRE (soloed.tracksWritten == 1);
    std::string error;
    REQUIRE (trackName (MidiFile::read (soloed.file.write(), error)->tracks[1]) == "Bass");
}

TEST_CASE ("Synth tracks get their own channels, skipping the drum channel", "[midi][export]")
{
    auto p = project::Project::makeDefault();
    p.tracks.erase (p.tracks.begin());               // synth tracks only
    for (int i = 1; i < 12; ++i)
    {
        auto t = p.tracks.front();
        t.id = (project::TrackId) (100 + i);
        p.tracks.push_back (t);
    }
    for (auto& t : p.tracks)
    {
        project::Clip c;
        c.id = 1;
        c.length = 3840;
        c.notes = { { 1, 60, 100, 0, 480 } };
        t.clips = { c };
    }

    std::string error;
    const auto f = MidiFile::read (project::exportMidi (p).file.write(), error);
    std::vector<int> channels;
    for (size_t i = 1; i < f->tracks.size(); ++i)
        channels.push_back (notesOf (f->tracks[i]).front().channel);
    REQUIRE (channels == std::vector<int> { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12 });
}

TEST_CASE ("MIDI export writes a file atomically", "[midi][export]")
{
    const auto dir = std::filesystem::temp_directory_path() / "beatforge-midi-test";
    std::filesystem::create_directories (dir);
    const auto path = dir / "song.mid";

    REQUIRE (project::saveMidiFile (song(), path).empty());
    std::ifstream in (path, std::ios::binary);
    std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), {});
    std::string error;
    REQUIRE (MidiFile::read (bytes, error).has_value());
    REQUIRE_FALSE (std::filesystem::exists (dir / "song.mid.tmp"));

    REQUIRE_FALSE (project::saveMidiFile (song(), dir / "missing" / "x.mid").empty());
    std::filesystem::remove_all (dir);
}
