#include "beatforge/midi/NoteSchedule.h"

#include <algorithm>

namespace beatforge::midi
{
    std::unique_ptr<NoteSchedule> NoteSchedule::build (const Clip& clip)
    {
        auto schedule = std::make_unique<NoteSchedule>();
        schedule->clipLength = std::max<time::Tick> (1, clip.length);
        schedule->events.reserve (clip.notes.size() * 2);

        for (auto note : clip.notes)
        {
            // Notes beyond a shortened clip are kept in the project but silent.
            if (note.start >= schedule->clipLength)
                continue;

            note = constrained (note, schedule->clipLength);
            schedule->notes.push_back (note);

            const auto offTick = note.end() >= schedule->clipLength ? 0 : note.end();
            schedule->events.push_back ({ note.start, true, note.pitch, note.velocity, note.id });
            schedule->events.push_back ({ offTick, false, note.pitch, 0, note.id });
        }

        std::stable_sort (schedule->events.begin(), schedule->events.end(), [] (const NoteEvent& a, const NoteEvent& b)
        {
            if (a.tick != b.tick) return a.tick < b.tick;
            return ! a.on && b.on;   // offs first
        });

        return schedule;
    }

    bool NoteSchedule::covers (NoteId id, double t) const noexcept
    {
        for (auto& n : notes)
            if (n.id == id)
                return t >= (double) n.start && t < (double) n.end();
        return false;
    }

    //==============================================================================
    ScheduleExchange::~ScheduleExchange()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    void ScheduleExchange::publish (std::unique_ptr<NoteSchedule> schedule)
    {
        // Whatever was pending was never seen by the audio thread: safe to delete.
        delete pending.exchange (schedule.release(), std::memory_order_acq_rel);
        collectGarbage();
    }

    void ScheduleExchange::collectGarbage()
    {
        delete retired.exchange (nullptr, std::memory_order_acq_rel);
    }

    const NoteSchedule* ScheduleExchange::acquire (bool& changed) noexcept
    {
        changed = false;

        // Only take a new schedule when the retired slot is free, so the old one
        // always has somewhere to go (the message thread empties that slot).
        if (pending.load (std::memory_order_acquire) != nullptr && retired.load (std::memory_order_acquire) == nullptr)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retired.store (current, std::memory_order_release);
                current = next;
                changed = true;
            }
        }

        return current;
    }
}
