#include <beatforge/project/Session.h>
#include <beatforge/project/TakeRecorder.h>

#include <catch2/catch_test_macros.hpp>

using namespace beatforge;
using project::Session;
using project::TakeRecorder;
using project::TrackType;
using Ev = audio::AudioEngine::RecordedEvent;

namespace
{
    constexpr time::Tick bar = 3840;

    project::TrackId synthTrack (const Session& s)
    {
        for (auto& t : s.getProject().tracks)
            if (t.type == TrackType::instrument)
                return t.id;
        return 0;
    }

    const project::Track& trackOf (const Session& s, project::TrackId id) { return *s.findTrack (id); }

    void on (TakeRecorder& r, double tick, int pitch, int vel = 100) { r.add ({ tick, pitch, vel }); }
    void off (TakeRecorder& r, double tick, int pitch)               { r.add ({ tick, pitch, 0 }); }
    void end (TakeRecorder& r, double tick)                          { r.add ({ tick, Ev::endOfTake, 0 }); }
}

TEST_CASE ("A take creates a clip from the note's bar and is one undo step", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = synthTrack (session);
    const auto undoBefore = session.canUndo();

    TakeRecorder rec (session);
    REQUIRE (rec.begin (synth));
    on (rec, bar + 480.4, 60, 90);              // bar 2, beat 1.5
    off (rec, bar + 959.6, 60);
    on (rec, 2 * bar + 100, 64);                // runs into bar 3: the clip grows
    off (rec, 2 * bar + 400, 64);
    end (rec, 2 * bar + 500);
    REQUIRE_FALSE (rec.isActive());
    REQUIRE (rec.getNotesRecorded() == 2);

    const auto& t = trackOf (session, synth);
    REQUIRE (t.clips.size() == 1);
    const auto& c = t.clips.front();
    REQUIRE (c.start == bar);
    REQUIRE (c.length == 2 * bar);
    REQUIRE (c.notes.size() == 2);
    REQUIRE (c.notes[0].pitch == 60);
    REQUIRE (c.notes[0].velocity == 90);
    REQUIRE (c.notes[0].start == 480);          // rounded to the nearest tick
    REQUIRE (c.notes[0].length == 480);
    REQUIRE (c.notes[1].start == bar + 100);
    REQUIRE (c.notes[1].length == 300);
    REQUIRE (rec.getLastClip() == c.id);

    session.undo();                              // the whole take at once
    REQUIRE (trackOf (session, synth).clips.empty());
    REQUIRE (session.canUndo() == undoBefore);
}

TEST_CASE ("Notes under an existing clip are added to it (overdub)", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = synthTrack (session);
    const auto existing = session.createClip (synth, 0, 4 * bar);
    session.addNote (existing, { 0, 48, 100, 0, 480 });

    TakeRecorder rec (session);
    rec.begin (synth);
    on (rec, 1000, 67);
    off (rec, 1200, 67);
    end (rec, 2000);

    const auto& t = trackOf (session, synth);
    REQUIRE (t.clips.size() == 1);
    REQUIRE (t.clips.front().notes.size() == 2);
    REQUIRE (t.clips.front().notes[1].start == 1000);
}

TEST_CASE ("Loop recording fills the loop region; a note held over the wrap ends at the loop end", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = synthTrack (session);
    session.setLoop (true, 2 * bar, 4 * bar);    // bars 3-4

    TakeRecorder rec (session);
    rec.begin (synth);
    on (rec, 2 * bar + 240, 60);
    off (rec, 2 * bar + 480, 60);
    on (rec, 4 * bar - 480, 62);                 // held over the wrap...
    off (rec, 2 * bar + 120, 62);                // ...released in the next pass
    on (rec, 2 * bar + 960, 64);                 // second pass: same clip
    off (rec, 2 * bar + 1200, 64);
    end (rec, 2 * bar + 1500);

    const auto& t = trackOf (session, synth);
    REQUIRE (t.clips.size() == 1);
    const auto& c = t.clips.front();
    REQUIRE (c.start == 2 * bar);
    REQUIRE (c.length == 2 * bar);
    REQUIRE (c.notes.size() == 3);
    REQUIRE (c.notes[1].pitch == 62);
    REQUIRE (c.notes[1].end() == 2 * bar);       // ends at the loop end
}

TEST_CASE ("Keys still held when the take ends are closed there", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = synthTrack (session);

    TakeRecorder rec (session);
    rec.begin (synth);
    on (rec, 100, 60);
    on (rec, 200, 60);                           // retrigger without note off
    on (rec, 300, 64);
    end (rec, 1000);

    const auto& notes = trackOf (session, synth).clips.front().notes;
    REQUIRE (notes.size() == 3);
    REQUIRE (notes[0].length == 100);            // ended by the retrigger
    REQUIRE (notes[1].end() == 1000);
    REQUIRE (notes[2].end() == 1000);

    SECTION ("finish() without an end event closes at the last position seen")
    {
        TakeRecorder rec2 (session);
        rec2.begin (synth);
        on (rec2, 2000, 70);
        off (rec2, 2100, 71);                    // never pressed: ignored
        rec2.finish();
        REQUIRE (trackOf (session, synth).clips.front().notes.back().length == 100);   // up to tick 2100
    }
}

TEST_CASE ("A take's clip does not grow over another clip", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = synthTrack (session);
    const auto later = session.createClip (synth, 3 * bar, bar);

    TakeRecorder rec (session);
    rec.begin (synth);
    on (rec, 100, 60);  off (rec, 200, 60);              // new clip: bar 1
    on (rec, bar + 100, 62);  off (rec, bar + 200, 62);   // grows to bar 2
    on (rec, 3 * bar + 100, 64);  off (rec, 3 * bar + 200, 64);   // inside the later clip
    on (rec, 4 * bar + 100, 65);  off (rec, 4 * bar + 200, 65);   // beyond it: a new clip
    end (rec, 4 * bar + 300);

    const auto& t = trackOf (session, synth);
    REQUIRE (t.clips.size() == 3);
    REQUIRE (t.clips[0].start == 0);
    REQUIRE (t.clips[0].length == 2 * bar);
    REQUIRE (t.clips[1].id == later);
    REQUIRE (t.clips[1].notes.size() == 1);
    REQUIRE (t.clips[2].start == 4 * bar);
}

TEST_CASE ("Only synth tracks can record", "[record]")
{
    audio::AudioEngine engine;
    Session session (engine);
    TakeRecorder rec (session);
    REQUIRE_FALSE (rec.begin (session.getProject().tracks.front().id));   // drums
    REQUIRE_FALSE (rec.begin (999));
    REQUIRE_FALSE (rec.isActive());
}

TEST_CASE ("Tracks can be reordered without changing their sound", "[session][tracks]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto a = session.getProject().tracks[0].id, b = session.getProject().tracks[1].id;
    const auto c = session.addTrack (TrackType::drums);
    const auto slotBefore = session.slotOf (a);

    REQUIRE (session.moveTrack (a, 2));
    REQUIRE (session.getProject().tracks[0].id == b);
    REQUIRE (session.getProject().tracks[1].id == c);
    REQUIRE (session.getProject().tracks[2].id == a);
    REQUIRE (session.slotOf (a) == slotBefore);

    REQUIRE_FALSE (session.moveTrack (a, 2));    // no change
    REQUIRE (session.moveTrack (a, -5));          // clamped
    REQUIRE (session.getProject().tracks[0].id == a);
    REQUIRE_FALSE (session.moveTrack (999, 0));

    session.undo();
    REQUIRE (session.getProject().tracks[2].id == a);
}
