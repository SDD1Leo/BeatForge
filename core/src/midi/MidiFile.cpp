#include "beatforge/midi/MidiFile.h"

#include <algorithm>

namespace beatforge::midi
{
    namespace
    {
        void putVarLen (std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            std::uint8_t buf[5];
            int n = 0;
            buf[n++] = (std::uint8_t) (v & 0x7f);
            while ((v >>= 7) != 0)
                buf[n++] = (std::uint8_t) (0x80 | (v & 0x7f));
            while (n > 0)
                out.push_back (buf[--n]);
        }

        void put32 (std::vector<std::uint8_t>& out, std::uint32_t v)
        {
            for (int shift = 24; shift >= 0; shift -= 8)
                out.push_back ((std::uint8_t) (v >> shift));
        }

        struct Reader
        {
            const std::vector<std::uint8_t>& data;
            size_t pos = 0, end = 0;

            bool has (size_t n) const { return pos + n <= end; }
            std::uint32_t read (int bytes) { std::uint32_t v = 0; while (bytes-- > 0) v = v << 8 | data[pos++]; return v; }
            bool varLen (std::uint32_t& v)
            {
                v = 0;
                for (int i = 0; i < 4; ++i)
                {
                    if (! has (1)) return false;
                    const auto b = data[pos++];
                    v = v << 7 | (b & 0x7f);
                    if ((b & 0x80) == 0) return true;
                }
                return false;
            }
        };

        int dataBytesFor (std::uint8_t status) { const auto k = status & 0xf0; return k == 0xc0 || k == 0xd0 ? 1 : 2; }
    }

    std::vector<std::uint8_t> MidiFile::Event::metaData() const
    {
        if (! isMeta()) return {};
        size_t pos = 2;
        std::uint32_t len = 0;
        while (pos < bytes.size())
        {
            const auto b = bytes[pos++];
            len = len << 7 | (b & 0x7f);
            if ((b & 0x80) == 0) break;
        }
        return { bytes.begin() + (std::ptrdiff_t) std::min (pos, bytes.size()),
                 bytes.begin() + (std::ptrdiff_t) std::min (pos + len, bytes.size()) };
    }

    void MidiFile::Track::addMeta (std::int64_t tick, std::uint8_t type, const std::vector<std::uint8_t>& data)
    {
        std::vector<std::uint8_t> b { 0xff, type };
        putVarLen (b, (std::uint32_t) data.size());
        b.insert (b.end(), data.begin(), data.end());
        add (tick, std::move (b));
    }

    void MidiFile::Track::addText (std::int64_t tick, std::uint8_t type, const std::string& text)
    {
        addMeta (tick, type, { text.begin(), text.end() });
    }

    std::int64_t MidiFile::Track::endTick() const noexcept
    {
        std::int64_t last = 0;
        for (auto& e : events)
            last = std::max (last, e.tick);
        return last;
    }

    //==============================================================================
    std::vector<std::uint8_t> MidiFile::write() const
    {
        std::vector<std::uint8_t> out { 'M', 'T', 'h', 'd' };
        put32 (out, 6);
        const int format = tracks.size() == 1 ? 0 : 1;
        out.insert (out.end(), { 0, (std::uint8_t) format, (std::uint8_t) (tracks.size() >> 8), (std::uint8_t) tracks.size(),
                                 (std::uint8_t) (ticksPerQuarter >> 8), (std::uint8_t) ticksPerQuarter });

        for (auto& track : tracks)
        {
            auto events = track.events;
            std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.tick < b.tick; });

            // One End of Track, last.
            std::int64_t endTick = 0;
            for (auto& e : events)
                endTick = std::max (endTick, e.tick);
            std::erase_if (events, [] (const Event& e) { return e.metaType() == metaEndOfTrack; });

            std::vector<std::uint8_t> body;
            std::int64_t now = 0;
            for (auto& e : events)
            {
                putVarLen (body, (std::uint32_t) std::max<std::int64_t> (0, e.tick - now));
                now = std::max (now, e.tick);
                body.insert (body.end(), e.bytes.begin(), e.bytes.end());
            }
            putVarLen (body, (std::uint32_t) std::max<std::int64_t> (0, endTick - now));
            body.insert (body.end(), { 0xff, metaEndOfTrack, 0 });

            out.insert (out.end(), { 'M', 'T', 'r', 'k' });
            put32 (out, (std::uint32_t) body.size());
            out.insert (out.end(), body.begin(), body.end());
        }
        return out;
    }

    std::optional<MidiFile> MidiFile::read (const std::vector<std::uint8_t>& data, std::string& error)
    {
        Reader r { data, 0, data.size() };
        if (! r.has (14) || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
            return error = "Not a Standard MIDI File (no MThd header).", std::nullopt;

        r.pos = 4;
        const auto headerLength = r.read (4);
        const auto format = r.read (2);
        const auto numTracks = r.read (2);
        const auto division = r.read (2);
        if (headerLength < 6 || format > 1)
            return error = "Unsupported MIDI file format " + std::to_string (format) + ".", std::nullopt;
        if ((division & 0x8000) != 0 || division == 0)
            return error = "MIDI files with SMPTE timing are not supported.", std::nullopt;
        r.pos = 8 + headerLength;

        MidiFile file;
        file.ticksPerQuarter = (int) division;

        for (std::uint32_t t = 0; t < numTracks; ++t)
        {
            if (! r.has (8) || data[r.pos] != 'M' || data[r.pos + 1] != 'T' || data[r.pos + 2] != 'r' || data[r.pos + 3] != 'k')
                return error = "MIDI track " + std::to_string (t + 1) + " is missing or damaged.", std::nullopt;
            r.pos += 4;
            const auto length = r.read (4);
            if (! r.has (length))
                return error = "MIDI track " + std::to_string (t + 1) + " is truncated.", std::nullopt;

            Reader tr { data, r.pos, r.pos + length };
            r.pos += length;

            Track track;
            std::int64_t tick = 0;
            std::uint8_t running = 0;
            while (tr.pos < tr.end)
            {
                std::uint32_t delta = 0;
                if (! tr.varLen (delta) || ! tr.has (1))
                    return error = "MIDI track " + std::to_string (t + 1) + " is damaged.", std::nullopt;
                tick += delta;

                auto status = data[tr.pos];
                if (status == 0xff || status == 0xf0 || status == 0xf7)
                {
                    ++tr.pos;
                    std::uint8_t type = 0;
                    if (status == 0xff)
                    {
                        if (! tr.has (1)) return error = "MIDI track " + std::to_string (t + 1) + " is damaged.", std::nullopt;
                        type = data[tr.pos++];
                    }
                    std::uint32_t len = 0;
                    if (! tr.varLen (len) || ! tr.has (len))
                        return error = "MIDI track " + std::to_string (t + 1) + " is damaged.", std::nullopt;
                    if (status == 0xff && type == metaEndOfTrack)
                        break;   // not reported: write() adds it back
                    if (status == 0xff)
                        track.addMeta (tick, type, { data.begin() + (std::ptrdiff_t) tr.pos, data.begin() + (std::ptrdiff_t) (tr.pos + len) });
                    tr.pos += len;   // SysEx is skipped
                    continue;
                }

                if ((status & 0x80) != 0) { running = status; ++tr.pos; }
                else if (running == 0)    return error = "MIDI track " + std::to_string (t + 1) + " is damaged.", std::nullopt;
                else                      status = running;

                const int n = dataBytesFor (status);
                if (! tr.has ((size_t) n))
                    return error = "MIDI track " + std::to_string (t + 1) + " is truncated.", std::nullopt;
                std::vector<std::uint8_t> bytes { status };
                for (int i = 0; i < n; ++i)
                    bytes.push_back (data[tr.pos++]);
                track.add (tick, std::move (bytes));
            }
            file.tracks.push_back (std::move (track));
        }
        return file;
    }
}
