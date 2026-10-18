#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace beatforge::midi
{
    // Standard MIDI File contents with absolute tick times. write() produces a
    // format 1 file (format 0 when there is one track); read() accepts format 0
    // and 1 with PPQ timing. Events are channel messages or meta events stored
    // as raw bytes without the delta time (meta: FF type length data).
    struct MidiFile
    {
        struct Event
        {
            std::int64_t tick = 0;
            std::vector<std::uint8_t> bytes;

            bool isMeta() const noexcept       { return bytes.size() >= 2 && bytes[0] == 0xff; }
            int metaType() const noexcept      { return isMeta() ? bytes[1] : -1; }
            std::vector<std::uint8_t> metaData() const;   // payload of a meta event
            bool isNoteOn() const noexcept     { return bytes.size() == 3 && (bytes[0] & 0xf0) == 0x90 && bytes[2] > 0; }
            bool isNoteOff() const noexcept    { return bytes.size() == 3 && ((bytes[0] & 0xf0) == 0x80 || ((bytes[0] & 0xf0) == 0x90 && bytes[2] == 0)); }
            int channel() const noexcept       { return bytes.empty() ? -1 : bytes[0] & 0x0f; }
        };

        struct Track
        {
            std::vector<Event> events;   // sorted by tick when written

            void add (std::int64_t tick, std::vector<std::uint8_t> bytes) { events.push_back ({ tick, std::move (bytes) }); }
            void addMeta (std::int64_t tick, std::uint8_t type, const std::vector<std::uint8_t>& data);
            void addText (std::int64_t tick, std::uint8_t type, const std::string& text);
            std::int64_t endTick() const noexcept;
        };

        int ticksPerQuarter = 960;
        std::vector<Track> tracks;

        // Events at the same tick keep their order. Each track ends with one End of Track.
        std::vector<std::uint8_t> write() const;
        static std::optional<MidiFile> read (const std::vector<std::uint8_t>&, std::string& error);

        static constexpr std::uint8_t metaTrackName = 0x03, metaEndOfTrack = 0x2f, metaTempo = 0x51, metaTimeSignature = 0x58;
    };
}
