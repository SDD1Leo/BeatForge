#include "beatforge/project/MidiExport.h"
#include "beatforge/util/Paths.h"
#include "beatforge/project/ScheduleBuilder.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>

namespace beatforge::project
{
    namespace
    {
        constexpr int drumChannel = 9;
        constexpr time::Tick drumHitLength = 120;

        bool audible (const Track& t, bool anySolo) { return ! t.muted && (! anySolo || t.soloed); }
    }

    MidiExport exportMidi (const Project& p)
    {
        MidiExport result;
        auto& file = result.file;
        file.ticksPerQuarter = time::ppq;

        const auto perBar = p.timeSignature.ticksPerBar();
        time::Tick songEnd = perBar;
        for (auto& t : p.tracks)
            for (auto& c : t.clips)
                songEnd = std::max (songEnd, c.end());
        result.length = songEnd;

        // Tempo track.
        midi::MidiFile::Track tempo;
        tempo.addText (0, midi::MidiFile::metaTrackName, p.name);
        int denominatorPower = 0;
        while ((1 << denominatorPower) < p.timeSignature.denominator)
            ++denominatorPower;
        tempo.addMeta (0, midi::MidiFile::metaTimeSignature,
                       { (std::uint8_t) p.timeSignature.numerator, (std::uint8_t) denominatorPower, 24, 8 });
        const auto microsPerQuarter = (std::uint32_t) std::lround (60'000'000.0 / p.tempo);
        tempo.addMeta (0, midi::MidiFile::metaTempo,
                       { (std::uint8_t) (microsPerQuarter >> 16), (std::uint8_t) (microsPerQuarter >> 8), (std::uint8_t) microsPerQuarter });
        tempo.addMeta (songEnd, midi::MidiFile::metaEndOfTrack, {});
        file.tracks.push_back (std::move (tempo));

        const bool anySolo = std::any_of (p.tracks.begin(), p.tracks.end(), [] (const Track& t) { return t.soloed; });
        int nextChannel = 0;

        for (auto& t : p.tracks)
        {
            if (! audible (t, anySolo))
                continue;

            midi::MidiFile::Track out;
            out.addText (0, midi::MidiFile::metaTrackName, t.name);

            // Note offs before note ons at the same tick.
            struct Note { time::Tick tick; bool on; int pitch, velocity; };
            std::vector<Note> notes;
            const auto schedule = buildSchedule (t);
            int channel = drumChannel;

            if (t.type == TrackType::drums)
            {
                const bool laneSolo = std::any_of (t.lanes.begin(), t.lanes.end(), [] (const DrumLane& l) { return l.soloed; });
                for (auto& e : schedule->events)
                    if (e.kind == audio::TrackSchedule::Kind::drumHit)
                        for (int lane = 0; lane < (int) t.lanes.size(); ++lane)
                        {
                            const auto& l = t.lanes[(size_t) lane];
                            if ((e.laneMask & (1u << lane)) == 0 || l.muted || (laneSolo && ! l.soloed))
                                continue;
                            notes.push_back ({ e.tick, true, gmDrumNote (lane), 100 });
                            notes.push_back ({ e.tick + drumHitLength, false, gmDrumNote (lane), 0 });
                        }
            }
            else
            {
                if (nextChannel == drumChannel)
                    ++nextChannel;
                channel = nextChannel % 16;
                nextChannel = (nextChannel + 1) % 16;

                const int program = t.instrument == audio::InstrumentType::monoBass ? 38 : 81;
                out.add (0, { (std::uint8_t) (0xc0 | channel), (std::uint8_t) program });

                for (auto& e : schedule->events)
                    if (e.kind != audio::TrackSchedule::Kind::drumHit)
                        notes.push_back ({ e.tick, e.kind == audio::TrackSchedule::Kind::noteOn, e.pitch, e.velocity });
            }

            std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
            {
                return a.tick != b.tick ? a.tick < b.tick : (! a.on && b.on);
            });

            for (auto& n : notes)
            {
                if (n.on)
                    ++result.notes;
                out.add (n.tick, n.on ? std::vector<std::uint8_t> { (std::uint8_t) (0x90 | channel), (std::uint8_t) n.pitch, (std::uint8_t) std::clamp (n.velocity, 1, 127) }
                                      : std::vector<std::uint8_t> { (std::uint8_t) (0x80 | channel), (std::uint8_t) n.pitch, 64 });
            }

            out.addMeta (songEnd, midi::MidiFile::metaEndOfTrack, {});
            file.tracks.push_back (std::move (out));
            ++result.tracksWritten;
        }

        return result;
    }

    std::string saveMidiFile (const Project& p, const std::filesystem::path& path)
    {
        const auto bytes = exportMidi (p).file.write();
        auto temp = path;
        temp += ".tmp";

        {
            errno = 0;
            std::ofstream out (temp, std::ios::binary | std::ios::trunc);
            if (! out)
                return "Could not write to " + pathToUtf8 (path.parent_path()) + ": " + std::strerror (errno)
                       + ". Check that the folder exists and is writable.";
            out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
            out.flush();
            if (! out)
            {
                out.close();   // Windows cannot delete a file that is still open
                std::error_code ignored;
                std::filesystem::remove (temp, ignored);
                return "Writing the MIDI file failed (the disk may be full).";
            }
        }

        std::error_code ec;
        std::filesystem::rename (temp, path, ec);
        if (ec)
        {
            std::error_code ignored;
            std::filesystem::remove (temp, ignored);
            return "Could not replace " + pathToUtf8 (path.filename()) + ": " + ec.message() + ".";
        }
        return {};
    }
}
