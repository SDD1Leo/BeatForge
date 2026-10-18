#pragma once

#include "beatforge/audio/AudioEngine.h"
#include "beatforge/project/Session.h"

#include <array>
#include <vector>

namespace beatforge::project
{
    // Turns the engine's recorded live events into notes on one synth track.
    // Message-thread only; every change goes through the Session, and a whole
    // take is one undo step.
    //
    // Where notes go: into the clip under the note's start (overdub), else into
    // a clip this take created (extended bar by bar when the take runs past
    // it), else into a new clip - the loop region when recording inside an
    // active loop, otherwise from the note's bar. A note held across a loop
    // wrap ends at the loop end; keys still held when the take ends are
    // closed there.
    class TakeRecorder
    {
    public:
        explicit TakeRecorder (Session& s) : session (s) {}
        ~TakeRecorder() { if (active) finish(); }

        // Starts a take on a synth track; false if it is not one.
        bool begin (TrackId);
        bool isActive() const noexcept           { return active; }
        TrackId getTrack() const noexcept        { return trackId; }

        // Feed every event from AudioEngine::popRecorded. An end-of-take event finishes the take.
        void add (const audio::AudioEngine::RecordedEvent&);
        // Ends the take (closing held keys at the last position seen).
        void finish();

        int getNotesRecorded() const noexcept    { return notesRecorded; }
        ClipId getLastClip() const noexcept      { return lastClip; }

        static constexpr time::Tick minNoteLength = 30;   // a 1/128 note at 960 PPQ

    private:
        void commit (int pitch, double on, double off, int velocity);
        ClipId clipFor (time::Tick start, time::Tick end);

        struct Held
        {
            bool down = false;
            double tick = 0.0;
            int velocity = 0;
        };

        Session& session;
        bool active = false;
        TrackId trackId = 0;
        std::array<Held, 128> held {};
        double lastTick = 0.0;
        std::vector<ClipId> takeClips;
        ClipId lastClip = 0;
        int notesRecorded = 0;
    };
}
