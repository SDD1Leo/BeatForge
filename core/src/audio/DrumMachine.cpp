#include "beatforge/audio/DrumMachine.h"

#include <algorithm>
#include <cmath>

namespace beatforge::audio
{
    namespace
    {
        bool validLane (int lane) noexcept           { return lane >= 0 && lane < DrumMachine::numLanes; }
        bool validStep (int step) noexcept           { return step >= 0 && step < DrumMachine::numSteps; }

        float dbToGain (float db) noexcept
        {
            return db <= DrumMachine::minGainDb ? 0.0f : std::pow (10.0f, db / 20.0f);
        }
    }

    DrumMachine::DrumMachine()
    {
        for (auto& s : steps) s.store (false);
        for (int i = 0; i < numLanes; ++i)
        {
            gainDb[(size_t) i].store (0.0f);
            muted[(size_t) i].store (false);
            soloed[(size_t) i].store (false);
            previewPending[(size_t) i].store (false);
            triggerCounts[(size_t) i].store (0);
        }
    }

    void DrumMachine::prepare (double sampleRate)
    {
        kit = DrumKit::generate (sampleRate);
        gainRampSamples = std::max (1, (int) (gainRampSeconds * sampleRate));
        retriggerFade = std::max (1, (int) (0.0015 * sampleRate));   // 1.5 ms
        chokeFade = std::max (1, (int) (0.005 * sampleRate));        // 5 ms
        reset();
    }

    void DrumMachine::reset() noexcept
    {
        const bool anySolo = std::any_of (soloed.begin(), soloed.end(), [] (auto& s) { return s.load(); });

        for (int i = 0; i < numLanes; ++i)
        {
            auto& lane = lanes[(size_t) i];
            for (auto& v : lane.voices)
                v = {};
            lane.gain = lane.targetGain = laneTargetGain (i, anySolo);
            lane.gainStep = 0.0f;
        }
    }

    void DrumMachine::setStep (int lane, int step, bool on) noexcept
    {
        if (validLane (lane) && validStep (step))
            steps[(size_t) (lane * numSteps + step)].store (on, std::memory_order_release);
    }

    bool DrumMachine::getStep (int lane, int step) const noexcept
    {
        return validLane (lane) && validStep (step)
            && steps[(size_t) (lane * numSteps + step)].load (std::memory_order_acquire);
    }

    void DrumMachine::clearPattern() noexcept
    {
        for (auto& s : steps)
            s.store (false, std::memory_order_release);
    }

    void DrumMachine::setLaneGainDb (int lane, float db) noexcept
    {
        if (validLane (lane))
            gainDb[(size_t) lane].store (std::clamp (db, minGainDb, maxGainDb), std::memory_order_release);
    }

    float DrumMachine::getLaneGainDb (int lane) const noexcept
    {
        return validLane (lane) ? gainDb[(size_t) lane].load (std::memory_order_acquire) : minGainDb;
    }

    void DrumMachine::setLaneMuted (int lane, bool m) noexcept   { if (validLane (lane)) muted[(size_t) lane].store (m); }
    bool DrumMachine::isLaneMuted (int lane) const noexcept      { return validLane (lane) && muted[(size_t) lane].load(); }
    void DrumMachine::setLaneSoloed (int lane, bool s) noexcept  { if (validLane (lane)) soloed[(size_t) lane].store (s); }
    bool DrumMachine::isLaneSoloed (int lane) const noexcept     { return validLane (lane) && soloed[(size_t) lane].load(); }

    void DrumMachine::preview (int lane) noexcept
    {
        if (validLane (lane))
            previewPending[(size_t) lane].store (true, std::memory_order_release);
    }

    std::uint32_t DrumMachine::getTriggerCount (int lane) const noexcept
    {
        return validLane (lane) ? triggerCounts[(size_t) lane].load (std::memory_order_relaxed) : 0;
    }

    float DrumMachine::laneTargetGain (int lane, bool anySolo) const noexcept
    {
        const bool audible = ! muted[(size_t) lane].load (std::memory_order_acquire)
                          && (! anySolo || soloed[(size_t) lane].load (std::memory_order_acquire));
        return audible ? dbToGain (gainDb[(size_t) lane].load (std::memory_order_acquire)) : 0.0f;
    }

    void DrumMachine::beginBlock() noexcept
    {
        const bool anySolo = std::any_of (soloed.begin(), soloed.end(),
                                          [] (auto& s) { return s.load (std::memory_order_acquire); });

        // A new target starts a fixed-length linear ramp (no zipper noise, and a
        // mute never leaks for longer than the ramp, whatever the buffer size).
        for (int i = 0; i < numLanes; ++i)
        {
            auto& lane = lanes[(size_t) i];
            const auto target = laneTargetGain (i, anySolo);

            if (target != lane.targetGain)
            {
                lane.targetGain = target;
                lane.gainStep = (target - lane.gain) / (float) gainRampSamples;
            }
        }
    }

    float DrumMachine::rampTowards (float gain, float target, float step, int samples) noexcept
    {
        const float next = gain + step * (float) samples;
        return step > 0.0f ? std::min (next, target) : (step < 0.0f ? std::max (next, target) : target);
    }

    void DrumMachine::fadeOut (Voice& v, int samples) noexcept
    {
        if (v.active && (v.fadeRemaining == 0 || v.fadeRemaining > samples))
        {
            v.fadeRemaining = samples;
            v.fadeLength = samples;
        }
    }

    void DrumMachine::trigger (int lane) noexcept
    {
        if (! validLane (lane))
            return;

        auto& l = lanes[(size_t) lane];
        const auto& sample = kit.samples[(size_t) lane];

        if (sample.empty())
            return;

        fadeOut (l.voices[(size_t) l.current], retriggerFade);
        l.current ^= 1;
        l.voices[(size_t) l.current] = { sample.data(), (int) sample.size(), 0, 0, 1, true };

        // Closed hat chokes a ringing open hat.
        if (lane == (int) DrumSound::closedHat)
            for (auto& v : lanes[(size_t) DrumSound::openHat].voices)
                fadeOut (v, chokeFade);

        triggerCounts[(size_t) lane].fetch_add (1, std::memory_order_relaxed);
    }

    void DrumMachine::triggerStep (int step) noexcept
    {
        if (! validStep (step))
            return;

        for (int lane = 0; lane < numLanes; ++lane)
            if (steps[(size_t) (lane * numSteps + step)].load (std::memory_order_acquire))
                trigger (lane);
    }

    void DrumMachine::startPendingPreviews() noexcept
    {
        for (int lane = 0; lane < numLanes; ++lane)
            if (previewPending[(size_t) lane].exchange (false, std::memory_order_acq_rel))
                trigger (lane);
    }

    void DrumMachine::render (float* const* channels, int numChannels, int start, int num) noexcept
    {
        for (auto& lane : lanes)
        {
            float gain = lane.gain;

            for (auto& v : lane.voices)
            {
                if (! v.active)
                    continue;

                gain = lane.gain;

                for (int i = start; i < start + num; ++i)
                {
                    float s = v.data[v.position] * gain;

                    if (v.fadeRemaining > 0)
                        s *= (float) v.fadeRemaining / (float) v.fadeLength;

                    for (int ch = 0; ch < numChannels; ++ch)
                        channels[ch][i] += s;

                    if (gain != lane.targetGain)
                        gain = rampTowards (gain, lane.targetGain, lane.gainStep, 1);

                    if (++v.position >= v.length || (v.fadeRemaining > 0 && --v.fadeRemaining == 0))
                    {
                        v.active = false;
                        break;
                    }
                }
            }

            // Advance the lane's ramp by the segment length, whether or not anything played.
            if (lane.gain != lane.targetGain)
                lane.gain = rampTowards (lane.gain, lane.targetGain, lane.gainStep, num);
        }
    }

    bool DrumMachine::isSounding() const noexcept
    {
        for (auto& lane : lanes)
            for (auto& v : lane.voices)
                if (v.active)
                    return true;
        return false;
    }
}
