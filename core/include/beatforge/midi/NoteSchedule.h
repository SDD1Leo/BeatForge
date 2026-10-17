#pragma once

#include "beatforge/midi/Note.h"

#include <atomic>
#include <memory>
#include <vector>

namespace beatforge::midi
{
    struct NoteEvent
    {
        time::Tick tick = 0;   // clip-local, in [0, clipLength)
        bool on = false;
        int pitch = 60;
        int velocity = 0;
        NoteId id = 0;
    };

    // What the audio thread plays for one clip: note-on/off events sorted by
    // tick (offs before ons at the same tick), plus the notes themselves for
    // "is this voice still inside its note?" checks. Immutable once published.
    struct NoteSchedule
    {
        time::Tick clipLength = 3840;
        std::vector<NoteEvent> events;
        std::vector<Note> notes;

        // Note-offs at the very end of the clip are stored at tick 0 so they
        // fire as the clip loops (before the next loop's note-ons). Notes that
        // start at or after the clip end are left out (silent, not deleted).
        static std::unique_ptr<NoteSchedule> build (const Clip&);

        // True if note `id` exists and covers clip-local tick `t`.
        bool covers (NoteId id, double t) const noexcept;
    };

    // Hands immutable schedules from the message thread to the audio thread
    // without locks, allocation or deallocation on the audio thread.
    //
    //  message thread: publish(), collectGarbage()
    //  audio thread:   acquire()
    class ScheduleExchange
    {
    public:
        ScheduleExchange() = default;
        ~ScheduleExchange();

        void publish (std::unique_ptr<NoteSchedule>);
        void collectGarbage();

        // Returns the schedule to use for this block; `changed` is set when a
        // new one was taken over in this call.
        const NoteSchedule* acquire (bool& changed) noexcept;

    private:
        std::atomic<NoteSchedule*> pending { nullptr };
        std::atomic<NoteSchedule*> retired { nullptr };
        NoteSchedule* current = nullptr;   // audio thread only

        ScheduleExchange (const ScheduleExchange&) = delete;
        ScheduleExchange& operator= (const ScheduleExchange&) = delete;
    };
}
