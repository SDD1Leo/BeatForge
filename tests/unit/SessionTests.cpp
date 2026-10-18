#include "EngineHelpers.h"

#include <beatforge/project/ProjectFile.h>
#include <beatforge/project/Session.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace beatforge;
using project::Session;
using project::TrackType;
using Change = Session::Change;
using beatforge::test::peakIn;
using beatforge::test::render;

namespace
{
    project::TrackId firstOfType (const Session& s, TrackType type)
    {
        for (auto& t : s.getProject().tracks)
            if (t.type == type)
                return t.id;
        return 0;
    }

    project::ClipId firstClip (const Session& s, project::TrackId t)
    {
        auto* track = s.findTrack (t);
        return track != nullptr && ! track->clips.empty() ? track->clips.front().id : 0;
    }
}

TEST_CASE ("A new session has a drum track with a clip and a synth track", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto& p = session.getProject();

    REQUIRE (p.tracks.size() == 2);
    REQUIRE (p.tracks[0].type == TrackType::drums);
    REQUIRE (p.tracks[0].clips.size() == 1);
    REQUIRE (p.tracks[1].type == TrackType::instrument);
    REQUIRE (session.slotOf (p.tracks[0].id) >= 0);
    REQUIRE (engine.getSlotType (session.slotOf (p.tracks[1].id)) == audio::SlotType::instrument);
    REQUIRE_FALSE (session.isDirty());
    REQUIRE_FALSE (session.canUndo());
}

TEST_CASE ("Edits change the project, the sound and the dirty state", "[session]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);
    std::vector<Change> changes;
    session.addListener ([&] (Change c) { changes.push_back (c); });

    const auto drums = firstOfType (session, TrackType::drums);
    const auto clip = firstClip (session, drums);

    session.setStep (clip, 0, 0, true);
    REQUIRE (session.findClip (clip)->pattern[0][0]);
    REQUIRE (session.isDirty());
    REQUIRE (changes == std::vector<Change> { Change::clips, Change::metadata });

    engine.play();
    REQUIRE (peakIn (render (engine, 4800)) > 0.05f);   // the kick sounds

    session.setTempo (999.0);
    REQUIRE (session.getProject().tempo == 240.0);
    REQUIRE (engine.getTempo() == 240.0);
}

TEST_CASE ("No-op and invalid edits are ignored", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    int notifications = 0;
    session.addListener ([&] (Change) { ++notifications; });

    const auto drums = firstOfType (session, TrackType::drums);
    const auto clip = firstClip (session, drums);
    session.setTempo (120.0);
    session.setStep (clip, 0, 0, false);
    session.setStep (clip, 9, 0, true);
    session.setStep (clip, 0, 16, true);
    session.setStep (12345, 0, 0, true);
    session.setTimeSignature ({ 4, 3 });
    session.setLoop (true, 3840, 3840);
    session.addNote (clip, { 0, 60, 100, 0, 240 });     // a drum clip takes no notes
    session.moveClip (clip, 3840, firstOfType (session, TrackType::instrument));   // wrong track type
    REQUIRE (session.createClip (drums, -1, 3840) == 0);
    REQUIRE_FALSE (session.renameTrack (drums, ""));

    REQUIRE_FALSE (session.isDirty());
    REQUIRE (notifications == 0);
    REQUIRE_FALSE (session.canUndo());
}

TEST_CASE ("Tracks: add, name, mix, remove", "[session][tracks]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);

    const auto drums2 = session.addTrack (TrackType::drums);
    const auto synth2 = session.addTrack (TrackType::instrument);
    REQUIRE (session.findTrack (drums2)->name == "Drums 2");
    REQUIRE (session.findTrack (synth2)->name == "Synth 2");
    REQUIRE (engine.getSlotType (session.slotOf (drums2)) == audio::SlotType::drums);

    REQUIRE (session.renameTrack (synth2, "Bass"));
    session.setTrackVolume (synth2, -100.0f);
    session.setTrackPan (synth2, 3.0f);
    REQUIRE (session.findTrack (synth2)->volumeDb == project::Project::minVolumeDb);
    REQUIRE (session.findTrack (synth2)->pan == 1.0f);

    // A kick on the new drum track sounds; removing the track silences it.
    const auto clip = session.createClip (drums2, 0, 3840);
    session.setStep (clip, 0, 0, true);
    engine.play();
    REQUIRE (peakIn (render (engine, 2400)) > 0.05f);
    engine.stop();
    render (engine, 48000);

    const auto slot = session.slotOf (drums2);
    REQUIRE (session.removeTrack (drums2));
    REQUIRE (session.findTrack (drums2) == nullptr);
    REQUIRE (engine.getSlotType (slot) == audio::SlotType::none);
    engine.play();
    REQUIRE (peakIn (render (engine, 2400)) == 0.0f);

    // At most 16 tracks.
    while (session.getProject().tracks.size() < 16)
        REQUIRE (session.addTrack (TrackType::instrument) != 0);
    REQUIRE (session.addTrack (TrackType::drums) == 0);
}

TEST_CASE ("Clips: create, move, resize, duplicate, delete", "[session][clips]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = firstOfType (session, TrackType::instrument);
    const auto synth2 = session.addTrack (TrackType::instrument);

    const auto a = session.createClip (synth, 3840, 7680);
    REQUIRE (session.findClip (a)->start == 3840);
    session.addNote (a, { 0, 60, 100, 0, 960 });

    session.moveClip (a, 0);
    session.resizeClip (a, 3840);
    REQUIRE (session.findClip (a)->start == 0);
    REQUIRE (session.findClip (a)->length == 3840);

    const auto b = session.duplicateClip (a);
    REQUIRE (session.findClip (b)->start == 3840);
    REQUIRE (session.findClip (b)->notes == session.findClip (a)->notes);   // independent copy of the content

    session.moveClip (b, 7680, synth2);
    REQUIRE (session.trackOfClip (b) == synth2);
    REQUIRE (session.findTrack (synth)->clips.size() == 1);

    session.addNote (b, { 0, 72, 100, 0, 240 });
    REQUIRE (session.findClip (a)->notes.size() == 1);   // editing the copy leaves the original alone

    REQUIRE (session.deleteClip (a));
    REQUIRE (session.findClip (a) == nullptr);
}

TEST_CASE ("Notes in a clip: add, update, quantize, remove; kept beyond a shortened clip", "[session][clips][midi]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = firstOfType (session, TrackType::instrument);
    const auto clip = session.createClip (synth, 0, 4 * 3840);

    const auto n1 = session.addNote (clip, { 0, 60, 100, 100, 240 });
    const auto late = session.addNote (clip, { 0, 64, 100, 3 * 3840, 240 });
    auto moved = *session.findNote (clip, n1);
    moved.pitch = 65;
    session.updateNotes (clip, { moved });
    REQUIRE (session.findNote (clip, n1)->pitch == 65);

    session.quantizeNotes (clip, { n1 }, midi::quarter);
    REQUIRE (session.findNote (clip, n1)->start == 0);

    session.resizeClip (clip, 3840);
    REQUIRE (session.findNote (clip, late) != nullptr);
    session.resizeClip (clip, 4 * 3840);
    REQUIRE (session.findNote (clip, late)->start == 3 * 3840);

    session.removeNotes (clip, { n1 });
    REQUIRE (session.findNote (clip, n1) == nullptr);
}

TEST_CASE ("Undo and redo restore the project and the sound", "[session][undo]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);
    const auto drums = firstOfType (session, TrackType::drums);
    const auto clip = firstClip (session, drums);

    session.setStep (clip, 0, 0, true);
    session.setTempo (100.0);
    REQUIRE (session.canUndo());

    session.undo();   // tempo
    REQUIRE (session.getProject().tempo == 120.0);
    REQUIRE (engine.getTempo() == 120.0);
    session.undo();   // step
    REQUIRE_FALSE (session.findClip (clip)->pattern[0][0]);
    REQUIRE_FALSE (session.isDirty());   // back to the saved state
    REQUIRE_FALSE (session.canUndo());

    session.redo();
    REQUIRE (session.findClip (clip)->pattern[0][0]);
    engine.play();
    REQUIRE (peakIn (render (engine, 4800)) > 0.05f);   // the engine follows redo

    session.setStep (clip, 1, 4, true);                 // a new edit clears redo
    REQUIRE_FALSE (session.canRedo());
}

TEST_CASE ("A gesture is one undo step", "[session][undo]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto clip = firstClip (session, firstOfType (session, TrackType::drums));

    session.beginGesture();
    for (int s = 0; s < 16; ++s)
        session.setStep (clip, 2, s, true);
    session.moveClip (clip, 3840);
    session.endGesture();

    session.undo();
    REQUIRE_FALSE (session.canUndo());
    REQUIRE (session.findClip (clip)->start == 0);
    REQUIRE_FALSE (session.findClip (clip)->pattern[2][5]);
}

TEST_CASE ("Undo brings back a deleted track with its clips", "[session][undo]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = firstOfType (session, TrackType::instrument);
    const auto clip = session.createClip (synth, 0, 3840);
    session.addNote (clip, { 0, 60, 100, 0, 240 });

    session.removeTrack (synth);
    session.undo();
    REQUIRE (session.findTrack (synth) != nullptr);
    REQUIRE (session.findClip (clip)->notes.size() == 1);
    REQUIRE (engine.getSlotType (session.slotOf (synth)) == audio::SlotType::instrument);
}

TEST_CASE ("Opening a project replaces everything and clears history", "[session]")
{
    audio::AudioEngine engine;
    Session session (engine);
    session.setTempo (90.0);

    auto p = project::Project::makeDefault();
    p.tempo = 140.0;
    p.tracks.pop_back();
    std::vector<Change> changes;
    session.addListener ([&] (Change c) { changes.push_back (c); });
    session.openProject (p, std::filesystem::path ("/tmp/x.beatforge"));

    REQUIRE (session.getProject().tracks.size() == 1);
    REQUIRE (engine.getTempo() == 140.0);
    REQUIRE_FALSE (session.isDirty());
    REQUIRE_FALSE (session.canUndo());
    REQUIRE (changes.front() == Change::replaced);
    REQUIRE (session.getFile() == std::filesystem::path ("/tmp/x.beatforge"));

    session.newProject();
    REQUIRE (session.getProject().tracks.size() == 2);
    REQUIRE_FALSE (session.getFile().has_value());
}

TEST_CASE ("A saved and reopened arrangement sounds bit-identical", "[session][file]")
{
    const auto file = std::filesystem::temp_directory_path() / "beatforge-session-roundtrip.beatforge";

    std::vector<float> original;
    {
        audio::AudioEngine engine;
        engine.prepare (48000.0);
        Session session (engine);
        session.setTempo (126.0);
        const auto drums = firstOfType (session, TrackType::drums);
        const auto beat = firstClip (session, drums);
        for (int s : { 0, 4, 8, 12 }) session.setStep (beat, 0, s, true);
        for (int s = 0; s < 16; s += 2) session.setStep (beat, 2, s, true);
        session.duplicateClip (beat);
        const auto synth = firstOfType (session, TrackType::instrument);
        session.setInstrument (synth, audio::InstrumentType::monoBass);
        const auto bass = session.createClip (synth, 3840, 3840);
        for (int i = 0; i < 4; ++i) session.addNote (bass, { 0, 36 + i * 3, 110, i * 960 + 480, 480 });
        session.setTrackVolume (synth, -3.0f);
        session.setTrackPan (drums, -0.3f);
        session.setLoop (true, 0, 2 * 3840);

        REQUIRE (project::saveFile (session.getProject(), file, "test").ok());
        session.markSaved (file);
        REQUIRE_FALSE (session.isDirty());

        engine.play();
        original = render (engine, 48000 * 6);
    }

    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);
    auto loaded = project::loadFile (file);
    REQUIRE (loaded.ok());
    REQUIRE (loaded.warnings.empty());
    session.openProject (*loaded.project, file);
    engine.play();

    REQUIRE (render (engine, 48000 * 6) == original);
    std::filesystem::remove (file);
}

TEST_CASE ("Undo steps are named; gestures and AI calls name a whole step", "[session][undo]")
{
    audio::AudioEngine engine;
    Session session (engine);
    REQUIRE (session.undoLabel().empty());

    session.setTempo (100.0);
    REQUIRE (session.undoLabel() == "Change tempo");
    const auto clip = session.createClip (firstOfType (session, TrackType::instrument), 0, 3840);
    REQUIRE (session.undoLabel() == "Create clip");

    session.beginGesture ("Record take");
    session.addNote (clip, { 0, 60, 100, 0, 480 });
    session.addNote (clip, { 0, 62, 100, 480, 480 });
    session.endGesture();
    REQUIRE (session.undoLabel() == "Record take");

    session.undo();
    REQUIRE (session.redoLabel() == "Record take");
    REQUIRE (session.undoLabel() == "Create clip");
    session.redo();
    REQUIRE (session.undoLabel() == "Record take");
    REQUIRE (session.redoLabel().empty());

    // Without a gesture label the first edit names the step.
    session.beginGesture();
    session.moveClip (clip, 3840);
    session.moveClip (clip, 7680);
    session.endGesture();
    REQUIRE (session.undoLabel() == "Move clip");
}

TEST_CASE ("Master volume is saved, loaded, undoable and limited", "[session][file]")
{
    audio::AudioEngine engine;
    Session session (engine);
    session.setMasterVolume (-4.5f);
    REQUIRE (session.getProject().masterVolumeDb == -4.5f);
    REQUIRE (session.undoLabel() == "Master volume");
    session.setMasterVolume (40.0f);
    REQUIRE (session.getProject().masterVolumeDb == project::Project::maxVolumeDb);

    const auto loaded = project::fromJson (project::toJson (session.getProject(), "test"));
    REQUIRE (loaded.project->masterVolumeDb == project::Project::maxVolumeDb);
    REQUIRE (loaded.warnings.empty());

    session.undo();
    REQUIRE (session.getProject().masterVolumeDb == -4.5f);
}

TEST_CASE ("Presets load in one undo step and every combination keeps headroom", "[session][presets]")
{
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    Session session (engine);
    const auto synth = firstOfType (session, TrackType::instrument);
    const auto bassTrack = session.addTrack (TrackType::instrument);

    const auto& acid = *audio::findPreset (audio::InstrumentType::monoBass, "acid");
    REQUIRE (session.applyPreset (bassTrack, acid));
    REQUIRE (session.findTrack (bassTrack)->instrument == audio::InstrumentType::monoBass);
    REQUIRE (session.getInstrumentParam (bassTrack, audio::InstrumentType::monoBass, audio::Param::resonance) == 0.8f);
    REQUIRE (session.undoLabel() == "Preset: Acid");
    REQUIRE_FALSE (session.applyPreset (firstOfType (session, TrackType::drums), acid));

    // Busy beat + 4-note chords + bass: below 0 dBFS with every poly x bass preset.
    const auto drumClip = firstClip (session, firstOfType (session, TrackType::drums));
    for (int lane = 0; lane < 5; ++lane)
        for (int s = 0; s < 16; s += lane == 2 ? 1 : 4)
            session.setStep (drumClip, lane, s, true);
    const auto chords = session.createClip (synth, 0, 3840);
    for (int pitch : { 60, 64, 67, 71 })
        session.addNote (chords, { 0, pitch, 127, 0, 3840 });
    const auto line = session.createClip (bassTrack, 0, 3840);
    for (int b = 0; b < 4; ++b)
        session.addNote (line, { 0, 36 + b * 3, 127, b * 960, 900 });
    session.setLoop (true, 0, 3840);

    for (auto& poly : audio::presetsFor (audio::InstrumentType::polySynth))
        for (auto& bass : audio::presetsFor (audio::InstrumentType::monoBass))
        {
            session.applyPreset (synth, poly);
            session.applyPreset (bassTrack, bass);
            engine.stop();
            test::render (engine, 4800);
            engine.play();
            INFO (poly.name << " + " << bass.name);
            REQUIRE (peakIn (render (engine, 96000)) < 1.0f);
        }
}

TEST_CASE ("Clips paste onto tracks of the same kind with fresh ids", "[session][clips]")
{
    audio::AudioEngine engine;
    Session session (engine);
    const auto synth = firstOfType (session, TrackType::instrument);
    const auto other = session.addTrack (TrackType::instrument);
    const auto source = session.createClip (synth, 0, 3840);
    session.addNote (source, { 0, 60, 100, 0, 480 });
    const auto content = *session.findClip (source);

    const auto pasted = session.pasteClip (other, content, TrackType::instrument, 7680);
    REQUIRE (pasted != 0);
    REQUIRE (pasted != source);
    REQUIRE (session.findClip (pasted)->start == 7680);
    REQUIRE (session.findClip (pasted)->notes.size() == 1);
    REQUIRE (session.trackOfClip (pasted) == other);
    REQUIRE (session.undoLabel() == "Paste clip");
    REQUIRE (session.pasteClip (firstOfType (session, TrackType::drums), content, TrackType::instrument, 0) == 0);

    // New notes in the pasted clip get ids that do not clash.
    const auto id = session.addNote (pasted, { 0, 62, 100, 480, 480 });
    REQUIRE (id != session.findClip (pasted)->notes[0].id);
}
