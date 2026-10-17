#include "beatforge/project/Session.h"

#include <algorithm>
#include <utility>

namespace beatforge::project
{
    Session::Session (audio::AudioEngine& e) : engine (e)
    {
        applyAllToEngine();
    }

    bool Session::validLane (int lane) noexcept
    {
        return lane >= 0 && lane < audio::DrumMachine::numLanes;
    }

    //==============================================================================
    void Session::setTempo (double bpm)
    {
        bpm = time::clampTempo (bpm);
        if (bpm == project.tempo)
            return;

        project.tempo = bpm;
        engine.setTempo (bpm);
        changed (Change::transport);
    }

    void Session::setTimeSignature (time::TimeSignature sig)
    {
        if (! sig.isValid() || sig == project.timeSignature)
            return;

        project.timeSignature = sig;
        engine.setTimeSignature (sig);
        applyLoop();   // loop length is in bars
        changed (Change::transport);
    }

    void Session::setMetronomeEnabled (bool enabled)
    {
        if (enabled == project.metronomeEnabled)
            return;

        project.metronomeEnabled = enabled;
        engine.setMetronomeEnabled (enabled);
        changed (Change::transport);
    }

    void Session::setLoop (bool enabled, int bars)
    {
        if (! Project::isValidLoopBars (bars))
            bars = project.loopBars;

        if (enabled == project.loopEnabled && bars == project.loopBars)
            return;

        project.loopEnabled = enabled;
        project.loopBars = bars;
        applyLoop();
        changed (Change::transport);
    }

    void Session::applyLoop()
    {
        engine.setLoop (project.loopEnabled, 0, project.timeSignature.ticksPerBar() * project.loopBars);
    }

    //==============================================================================
    void Session::setStep (int lane, int step, bool on)
    {
        if (! validLane (lane) || step < 0 || step >= audio::DrumMachine::numSteps)
            return;

        auto& s = project.lanes[(size_t) lane].steps[(size_t) step];
        if (s == on)
            return;

        s = on;
        engine.getDrumMachine().setStep (lane, step, on);
        changed (Change::pattern);
    }

    void Session::clearPattern()
    {
        bool any = false;
        for (auto& lane : project.lanes)
            for (auto& s : lane.steps)
                any |= std::exchange (s, false);

        if (! any)
            return;

        engine.getDrumMachine().clearPattern();
        changed (Change::pattern);
    }

    void Session::setLaneGainDb (int lane, float db)
    {
        if (! validLane (lane))
            return;

        db = std::clamp (db, audio::DrumMachine::minGainDb, audio::DrumMachine::maxGainDb);
        auto& stored = project.lanes[(size_t) lane].gainDb;
        if (db == stored)
            return;

        stored = db;
        engine.getDrumMachine().setLaneGainDb (lane, db);
        changed (Change::lanes);
    }

    void Session::setLaneMuted (int lane, bool muted)
    {
        if (! validLane (lane) || project.lanes[(size_t) lane].muted == muted)
            return;

        project.lanes[(size_t) lane].muted = muted;
        engine.getDrumMachine().setLaneMuted (lane, muted);
        changed (Change::lanes);
    }

    void Session::setLaneSoloed (int lane, bool soloed)
    {
        if (! validLane (lane) || project.lanes[(size_t) lane].soloed == soloed)
            return;

        project.lanes[(size_t) lane].soloed = soloed;
        engine.getDrumMachine().setLaneSoloed (lane, soloed);
        changed (Change::lanes);
    }

    bool Session::renameLane (int lane, const std::string& name)
    {
        if (! validLane (lane) || name.empty() || name.size() > (size_t) Project::maxLaneNameLength)
            return false;

        if (project.lanes[(size_t) lane].name != name)
        {
            project.lanes[(size_t) lane].name = name;
            changed (Change::lanes);
        }
        return true;
    }

    bool Session::renameProject (const std::string& name)
    {
        if (name.empty() || name.size() > (size_t) Project::maxNameLength)
            return false;

        if (project.name != name)
        {
            project.name = name;
            changed (Change::metadata);
        }
        return true;
    }

    //==============================================================================
    void Session::newProject()
    {
        openProject (Project {}, std::nullopt);
    }

    void Session::openProject (Project p, std::optional<std::filesystem::path> f)
    {
        project = std::move (p);
        file = std::move (f);
        dirty = false;
        applyAllToEngine();
        changed (Change::replaced, false);
    }

    void Session::markSaved (const std::filesystem::path& f)
    {
        file = f;
        dirty = false;
        changed (Change::metadata, false);
    }

    void Session::applyAllToEngine()
    {
        engine.setTempo (project.tempo);
        engine.setTimeSignature (project.timeSignature);
        engine.setMetronomeEnabled (project.metronomeEnabled);
        applyLoop();

        auto& drums = engine.getDrumMachine();
        for (int lane = 0; lane < audio::DrumMachine::numLanes; ++lane)
        {
            const auto& l = project.lanes[(size_t) lane];
            drums.setLaneGainDb (lane, l.gainDb);
            drums.setLaneMuted (lane, l.muted);
            drums.setLaneSoloed (lane, l.soloed);
            for (int step = 0; step < audio::DrumMachine::numSteps; ++step)
                drums.setStep (lane, step, l.steps[(size_t) step]);
        }
    }

    //==============================================================================
    int Session::addListener (Listener listener)
    {
        listeners.emplace_back (nextListenerId, std::move (listener));
        return nextListenerId++;
    }

    void Session::removeListener (int id)
    {
        std::erase_if (listeners, [id] (auto& l) { return l.first == id; });
    }

    void Session::changed (Change change, bool makesDirty)
    {
        const bool becameDirty = makesDirty && ! dirty;
        dirty = dirty || makesDirty;

        // Copy: a listener may add/remove listeners.
        auto current = listeners;
        for (auto& [id, listener] : current)
        {
            listener (change);
            if (becameDirty && change != Change::metadata)
                listener (Change::metadata);
        }
    }
}
