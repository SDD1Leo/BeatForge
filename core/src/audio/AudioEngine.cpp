#include "beatforge/audio/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace beatforge::audio
{
    namespace
    {
        std::uint32_t pack (time::TimeSignature s) noexcept   { return (std::uint32_t) s.numerator << 8 | (std::uint32_t) s.denominator; }
        time::TimeSignature unpack (std::uint32_t v) noexcept  { return { (int) (v >> 8), (int) (v & 0xff) }; }

        float dbToGain (float db) noexcept { return db <= -60.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
    }

    struct AudioEngine::Slot
    {
        DrumMachine drums;
        InstrumentRack instruments;
        util::LockFreeExchange<TrackSchedule> exchange;
        const TrackSchedule* schedule = nullptr;   // audio thread
        bool scheduleChanged = false;

        std::atomic<int> type { (int) SlotType::none };
        std::atomic<float> volumeDb { 0.0f }, pan { 0.0f };
        std::atomic<bool> muted { false }, soloed { false };
        std::atomic<int> activeVoices { 0 };

        std::vector<float> left, right;            // render buffers (sized in prepare)
        LevelMeter meter;                          // post-fader
        float gainL = 1.0f, gainR = 1.0f;          // smoothed mix gains

        SlotType getType() const noexcept { return (SlotType) type.load (std::memory_order_acquire); }
    };

    AudioEngine::AudioEngine()
        : packedSignature (pack ({}))
    {
        static_assert (std::atomic<double>::is_always_lock_free);
        static_assert (std::atomic<time::Tick>::is_always_lock_free);
        for (auto& s : slots)
            s = std::make_unique<Slot>();
    }

    AudioEngine::~AudioEngine() = default;

    void AudioEngine::prepare (double newSampleRate)
    {
        transport.prepare (newSampleRate);
        metronome.prepare (newSampleRate);
        metronomeBuffer.assign ((size_t) maxChunk, 0.0f);

        for (auto& s : slots)
        {
            s->drums.prepare (newSampleRate);   // allocates the kit; never runs alongside process()
            s->instruments.prepare (newSampleRate);
            s->left.assign ((size_t) maxChunk, 0.0f);
            s->right.assign ((size_t) maxChunk, 0.0f);
        }

        transport.seekTo (playStart.load());
        renderedSamples = 0;
        clockValid = false;
        clockEarliness = 0.0;
        sampleRate.store (newSampleRate, std::memory_order_release);
        publishPosition();
    }

    void AudioEngine::reset() noexcept
    {
        playRequested.store (false, std::memory_order_release);
        transport.rewind();
        transport.seekTo (playStart.load());
        metronome.reset();
        for (auto& s : slots)
        {
            s->drums.reset();
            s->instruments.reset();
        }
        wasPlaying = false;
        liveHeld.fill (false);
        liveSlot = -1;
        recordingActive = false;
        numLiveEvents = 0;
        renderedSamples = 0;
        clockValid = false;
        clockEarliness = 0.0;
        publishPosition();
        state.store (TransportState::stopped, std::memory_order_release);
    }

    double AudioEngine::now() noexcept
    {
        return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void AudioEngine::setRecording (bool shouldRecord) noexcept
    {
        recording.store (shouldRecord, std::memory_order_release);
    }

    //==============================================================================
    void AudioEngine::seek (double tick) noexcept
    {
        playStart.store (std::max (0.0, tick), std::memory_order_release);
        seekPending.store (true, std::memory_order_release);
    }

    void AudioEngine::setTempo (double bpm) noexcept                  { tempo.store (time::clampTempo (bpm), std::memory_order_release); }
    void AudioEngine::setMetronomeEnabled (bool enabled) noexcept     { metronomeEnabled.store (enabled, std::memory_order_release); }
    time::TimeSignature AudioEngine::getTimeSignature() const noexcept { return unpack (packedSignature.load (std::memory_order_acquire)); }

    void AudioEngine::setTimeSignature (time::TimeSignature sig) noexcept
    {
        if (sig.isValid())
            packedSignature.store (pack (sig), std::memory_order_release);
    }

    void AudioEngine::setLoop (bool enabled, time::Tick start, time::Tick end) noexcept
    {
        // Disable first so the audio thread never pairs a new start with an old end.
        loopEnabled.store (false, std::memory_order_release);
        loopStart.store (start, std::memory_order_release);
        loopEnd.store (end, std::memory_order_release);
        loopEnabled.store (enabled, std::memory_order_release);
    }

    //==============================================================================
    void AudioEngine::setSlotType (int slot, SlotType t) noexcept
    {
        slots[(size_t) clampSlot (slot)]->type.store ((int) t, std::memory_order_release);
    }

    SlotType AudioEngine::getSlotType (int slot) const noexcept
    {
        return slots[(size_t) clampSlot (slot)]->getType();
    }

    void AudioEngine::setSlotMix (int slot, float volumeDb, float pan, bool muted, bool soloed) noexcept
    {
        auto& s = *slots[(size_t) clampSlot (slot)];
        s.volumeDb.store (volumeDb, std::memory_order_release);
        s.pan.store (std::clamp (pan, -1.0f, 1.0f), std::memory_order_release);
        s.muted.store (muted, std::memory_order_release);
        s.soloed.store (soloed, std::memory_order_release);
    }

    void AudioEngine::setSlotSchedule (int slot, std::unique_ptr<TrackSchedule> schedule)
    {
        slots[(size_t) clampSlot (slot)]->exchange.publish (schedule != nullptr ? std::move (schedule)
                                                                                 : std::make_unique<TrackSchedule>());
    }

    void AudioEngine::collectGarbage()
    {
        for (auto& s : slots)
            s->exchange.collectGarbage();
    }

    DrumMachine& AudioEngine::drums (int slot) noexcept          { return slots[(size_t) clampSlot (slot)]->drums; }
    InstrumentRack& AudioEngine::instruments (int slot) noexcept { return slots[(size_t) clampSlot (slot)]->instruments; }

    LevelMeter& AudioEngine::getSlotMeter (int slot) noexcept { return slots[(size_t) clampSlot (slot)]->meter; }

    int AudioEngine::getActiveVoices (int slot) const noexcept
    {
        return slots[(size_t) clampSlot (slot)]->activeVoices.load (std::memory_order_relaxed);
    }

    //==============================================================================
    void AudioEngine::applySettings() noexcept
    {
        transport.setTempo (tempo.load (std::memory_order_acquire));
        transport.setTimeSignature (getTimeSignature());
        transport.setLoop (loopEnabled.load (std::memory_order_acquire),
                           loopStart.load (std::memory_order_acquire), loopEnd.load (std::memory_order_acquire));
    }

    void AudioEngine::publishPosition() noexcept
    {
        positionTicks.store (transport.getPositionTicks(), std::memory_order_release);
        positionSamples.store (transport.getElapsedSamples(), std::memory_order_release);
    }

    void AudioEngine::renderVoices (int start, int num) noexcept
    {
        if (num <= 0)
            return;

        float* metro[] { metronomeBuffer.data() };
        metronome.render (metro, 1, start, num);

        for (auto& s : slots)
        {
            float* lr[] { s->left.data(), s->right.data() };
            switch (s->getType())
            {
                case SlotType::drums:      s->drums.render (lr, 2, start, num); break;
                case SlotType::instrument: s->instruments.render (lr, 2, start, num); break;
                case SlotType::none:       break;
            }
        }
    }

    void AudioEngine::releaseNotesNotCovering (Slot& s, double tick) noexcept
    {
        // Live notes belong to the player's hands, not the timeline.
        if (s.schedule == nullptr)
            return s.instruments.releaseUnless ([] (VoiceId v) { return isLiveVoice (v); });

        s.instruments.releaseUnless ([&] (VoiceId v) { return isLiveVoice (v) || s.schedule->covers (v, tick); });
    }

    //==============================================================================
    void AudioEngine::collectLiveEvents (int numSamples) noexcept
    {
        numLiveEvents = 0;
        const double now = clock();
        const double sr = sampleRate.load (std::memory_order_acquire);

        // Follow the audio clock. A callback can come early (a burst) but never
        // after its audio is due, so the latest "deadline line" seen is the
        // clock; it drifts down slowly to follow clock drift and to recover
        // from a one-off late callback.
        constexpr double drift = 5.0e-6;   // seconds per callback: well above real clock drift (~1e-6), well below a sample
        const double line = now - (double) renderedSamples / sr;
        if (! clockValid || line > clockOffset)
        {
            clockOffset = line;
            clockValid = true;
        }
        else
        {
            clockOffset -= drift;
        }
        clockEarliness = std::max (clockOffset - line, clockEarliness - drift);

        // Every event plays this long after it arrived: one block plus the largest burst seen.
        const double delay = (double) numSamples + clockEarliness * sr;

        auto drain = [&] (auto& queue)
        {
            while (numLiveEvents < maxLivePerBlock)
            {
                const auto* next = queue.peek();
                if (next == nullptr)
                    break;
                const double press = (next->time - clockOffset) * sr - (double) renderedSamples;
                const double at = press + delay;
                if (at >= (double) numSamples)   // due in a later block
                    break;
                liveEvents[(size_t) numLiveEvents++] = { std::clamp ((int) std::lround (at), 0, numSamples - 1), press, *next };
                LiveNote done;
                queue.pop (done);
            }
        };
        drain (deviceInput);
        drain (screenInput);

        for (int i = 1; i < numLiveEvents; ++i)   // stable, by offset
            for (int j = i; j > 0 && liveEvents[(size_t) j].offset < liveEvents[(size_t) j - 1].offset; --j)
                std::swap (liveEvents[(size_t) j], liveEvents[(size_t) j - 1]);
    }

    // Plays one live event; true if it changed what sounds (and so is worth recording).
    bool AudioEngine::applyLive (const LiveNote& n) noexcept
    {
        if (n.pitch < 0 || n.pitch > 127 || liveSlot < 0)
            return false;

        auto& rack = slots[(size_t) liveSlot]->instruments;
        const auto voice = liveVoiceBase | (VoiceId) n.pitch;
        if (n.velocity > 0)
        {
            if (liveHeld[(size_t) n.pitch])
                rack.noteOff (voice);
            rack.noteOn (voice, n.pitch, std::min (n.velocity, 127));
            liveHeld[(size_t) n.pitch] = true;
            return true;
        }

        if (! liveHeld[(size_t) n.pitch])
            return false;
        rack.noteOff (voice);
        liveHeld[(size_t) n.pitch] = false;
        return true;
    }

    void AudioEngine::releaseLive (double tick, double samplesPerTick) noexcept
    {
        for (int pitch = 0; pitch < 128; ++pitch)
            if (liveHeld[(size_t) pitch])
            {
                applyLive ({ 0.0, pitch, 0 });
                if (recordingActive)
                    recordEvent (tick, 0.0, samplesPerTick, pitch, 0);
            }
    }

    void AudioEngine::recordEvent (double tick, double delaySamples, double samplesPerTick, int pitch, int velocity) noexcept
    {
        const double heardDelay = delaySamples + (double) outputLatency.load (std::memory_order_acquire);
        double at = tick - heardDelay / std::max (1e-9, samplesPerTick);

        // Heard before a loop wrap: belongs at the end of the loop, not before its start -
        // but only once the take has wrapped; before that, a press just ahead of
        // the take counts as played on its first beat.
        const auto ls = (double) loopStart.load (std::memory_order_acquire), le = (double) loopEnd.load (std::memory_order_acquire);
        if (! wrappedInTake)
        {
            at = std::max (at, takeStartTick);
        }
        else if (loopEnabled.load (std::memory_order_acquire) && ls < le && tick >= ls && tick < le && at < ls)
        {
            at += le - ls;
            if (at >= le)   // rounding right at the wrap: the start of the next pass
                at = ls;
        }

        recorded.push ({ std::max (0.0, at), pitch, velocity });
    }

    //==============================================================================
    void AudioEngine::process (float* const* outputs, int numOutputs, int numSamples) noexcept
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                std::memset (outputs[ch], 0, sizeof (float) * (size_t) numSamples);

        // Sound goes to the first stereo pair; extra outputs stay silent.
        float* pair[2] {};
        int numPair = 0;
        for (int ch = 0; ch < numOutputs && numPair < 2; ++ch)
            if (outputs[ch] != nullptr)
                pair[numPair++] = outputs[ch];

        if (metronomeBuffer.empty())   // not prepared
            return;

        collectLiveEvents (numSamples);

        for (int done = 0; done < numSamples; done += maxChunk)
        {
            const int n = std::min (maxChunk, numSamples - done);
            float* chunk[2] { pair[0] != nullptr ? pair[0] + done : nullptr, pair[1] != nullptr ? pair[1] + done : nullptr };
            processChunk (chunk, numPair, n, done);
        }
        renderedSamples += numSamples;

        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }

    void AudioEngine::processChunk (float* const* pair, int numPair, int numSamples, int blockOffset) noexcept
    {
        applySettings();

        // Live events of this chunk (offsets relative to the chunk).
        auto liveInChunk = [&] (auto&& use)
        {
            for (int i = 0; i < numLiveEvents; ++i)
            {
                const auto& e = liveEvents[(size_t) i];
                if (e.offset >= blockOffset && e.offset < blockOffset + numSamples)
                    use (e.offset - blockOffset, e);
            }
        };
        std::fill_n (metronomeBuffer.data(), numSamples, 0.0f);

        for (auto& s : slots)
        {
            std::fill_n (s->left.data(), numSamples, 0.0f);
            std::fill_n (s->right.data(), numSamples, 0.0f);
            s->drums.beginBlock();
            s->drums.startPendingPreviews();
            s->instruments.beginBlock();
            s->schedule = s->exchange.acquire (s->scheduleChanged);
        }

        PreviewRequest preview;
        while (previews.pop (preview))
        {
            auto& rack = slots[(size_t) clampSlot (preview.slot)]->instruments;
            if (preview.velocity > 0) rack.noteOn (PolySynthInstrument::previewId, preview.pitch, preview.velocity);
            else                      rack.previewOff (preview.pitch);
        }

        const bool wantPlay = playRequested.load (std::memory_order_acquire);

        // Live target: an instrument slot. Notes held on the previous target end there.
        {
            const int t = liveTarget.load (std::memory_order_acquire);
            const int wanted = t >= 0 && t < maxTracks && slots[(size_t) t]->getType() == SlotType::instrument ? t : -1;
            if (wanted != liveSlot)
            {
                releaseLive (transport.getPositionTicks(), lastSamplesPerTick);
                liveSlot = wanted;
            }
        }

        if (seekPending.exchange (false, std::memory_order_acq_rel))
        {
            transport.seekTo (playStart.load (std::memory_order_acquire));
            for (auto& s : slots)
                releaseNotesNotCovering (*s, transport.getPositionTicks());
            publishPosition();
        }

        if (wantPlay)
        {
            // A clip edit while playing: notes that were deleted or moved away
            // from the playhead must not keep sounding.
            if (wasPlaying)
                for (auto& s : slots)
                    if (s->scheduleChanged)
                        releaseNotesNotCovering (*s, transport.getPositionTicks());

            // Punching out while playing ends the take where the playhead is.
            const bool recordNow = recording.load (std::memory_order_acquire);
            if (recordingActive && ! recordNow)
                recordEvent (transport.getPositionTicks(), 0.0, lastSamplesPerTick, RecordedEvent::endOfTake, 0);
            const bool takeStarts = recordNow && ! recordingActive;
            if (takeStarts)
            {
                takeStartTick = transport.getPositionTicks();
                wrappedInTake = false;
            }
            recordingActive = recordNow;

            // Everything that happens in this chunk, in sample order.
            enum Kind : std::uint8_t { liveOff, noteOff, jump, gridLine, drumHit, noteOn, liveOn };
            struct Event
            {
                int offset;
                Kind kind;
                int slot;
                time::Transport::GridLine line;
                const TrackSchedule::Event* event;
                double tick;
                LiveNote live;
                double delay;   // live: samples from the key press to this event
            };

            std::array<Event, maxEventsPerChunk> events;
            int numEvents = 0;
            auto add = [&] (const Event& e) { if (numEvents < maxEventsPerChunk) events[(size_t) numEvents++] = e; };

            // Segments of this chunk, to find the musical position of any sample.
            struct SegmentInfo { int offset; double startTick, samplesPerTick; };
            std::array<SegmentInfo, 32> segments {};
            int numSegments = 0;
            auto tickAt = [&] (int offset)
            {
                const SegmentInfo* seg = numSegments > 0 ? &segments[0] : nullptr;
                for (int i = 1; i < numSegments; ++i)
                    if (segments[(size_t) i].offset <= offset)
                        seg = &segments[(size_t) i];
                return seg == nullptr ? transport.getPositionTicks()
                                      : seg->startTick + (double) (offset - seg->offset) / seg->samplesPerTick;
            };

            transport.advance (numSamples,
                [&] (const time::Transport::GridLine& line)
                {
                    if (line.isBeat)
                        add ({ line.sampleOffset, gridLine, -1, line, nullptr, 0.0, {}, 0.0 });
                },
                [&] (const time::Transport::Segment& seg)
                {
                    if (numSegments < (int) segments.size())
                        segments[(size_t) numSegments++] = { seg.sampleOffset, seg.startTick, seg.samplesPerTick };
                    lastSamplesPerTick = seg.samplesPerTick;

                    if (seg.afterJump)
                        add ({ seg.sampleOffset, jump, -1, {}, nullptr, seg.startTick, {}, 0.0 });

                    for (int i = 0; i < maxTracks; ++i)
                    {
                        const auto& s = *slots[(size_t) i];
                        if (s.schedule == nullptr || s.getType() == SlotType::none)
                            continue;

                        const auto& ev = s.schedule->events;
                        auto it = std::upper_bound (ev.begin(), ev.end(), seg.eventsAfter,
                                                    [] (double t, const TrackSchedule::Event& e) { return t < (double) e.tick; });
                        for (; it != ev.end() && (double) it->tick <= seg.eventsUpTo; ++it)
                        {
                            const Kind k = it->kind == TrackSchedule::Kind::noteOff ? noteOff
                                         : it->kind == TrackSchedule::Kind::drumHit ? drumHit : noteOn;
                            add ({ seg.sampleFor ((double) it->tick), k, i, {}, &*it, 0.0, {}, 0.0 });
                        }
                    }
                });

            liveInChunk ([&] (int offset, const PlacedLive& p)
            {
                add ({ offset, p.note.velocity > 0 && p.note.pitch >= 0 ? liveOn : liveOff, -1, {}, nullptr, 0.0, p.note,
                       (double) p.offset - p.pressOffset });
            });

            // Stable insertion sort by (offset, kind): chunks hold few events.
            for (int i = 1; i < numEvents; ++i)
                for (int j = i; j > 0 && (events[(size_t) j].offset < events[(size_t) j - 1].offset
                                          || (events[(size_t) j].offset == events[(size_t) j - 1].offset
                                              && events[(size_t) j].kind < events[(size_t) j - 1].kind)); --j)
                    std::swap (events[(size_t) j], events[(size_t) j - 1]);

            const bool clicks = metronomeEnabled.load (std::memory_order_acquire);
            int rendered = 0;
            std::uint64_t beats = 0;

            for (int i = 0; i < numEvents; ++i)
            {
                const auto& e = events[(size_t) i];
                renderVoices (rendered, e.offset - rendered);
                rendered = e.offset;

                switch (e.kind)
                {
                    case noteOff:  slots[(size_t) e.slot]->instruments.noteOff (e.event->voice); break;
                    case noteOn:   slots[(size_t) e.slot]->instruments.noteOn (e.event->voice, e.event->pitch, e.event->velocity); break;
                    case drumHit:  slots[(size_t) e.slot]->drums.triggerLanes (e.event->laneMask); break;
                    case liveOn:
                    case liveOff:
                        if (e.live.pitch < 0)
                            releaseLive (tickAt (e.offset), lastSamplesPerTick);
                        else if (applyLive (e.live) && recordingActive)
                            recordEvent (tickAt (e.offset), e.delay, lastSamplesPerTick, e.live.pitch, e.live.velocity);
                        break;
                    case jump:
                        for (auto& s : slots)
                            releaseNotesNotCovering (*s, e.tick);
                        if (! (takeStarts && e.offset == 0))   // not the jump to the play start
                            wrappedInTake = true;
                        break;
                    case gridLine:
                        ++beats;
                        if (clicks)
                            metronome.trigger (e.line.isDownbeat());
                        break;
                }
            }

            renderVoices (rendered, numSamples - rendered);
            beatCount.fetch_add (beats, std::memory_order_relaxed);
            publishPosition();
            lastPlayingTick = transport.getPositionTicks();
            state.store (TransportState::playing, std::memory_order_release);
        }
        else
        {
            // Stopping releases every clip note (no stuck notes) and returns to
            // the play start; auditions and ringing tails keep sounding.
            if (wasPlaying)
            {
                // The take ends where playback stopped; keys still held are closed there.
                if (recordingActive)
                    recordEvent (lastPlayingTick, 0.0, lastSamplesPerTick, RecordedEvent::endOfTake, 0);
                recordingActive = false;

                for (auto& s : slots)
                    s->instruments.releaseUnless ([] (VoiceId v) { return isLiveVoice (v); });
                transport.rewind();
                transport.seekTo (playStart.load (std::memory_order_acquire));
                publishPosition();
                state.store (TransportState::stopped, std::memory_order_release);
            }

            int rendered = 0;
            liveInChunk ([&] (int offset, const PlacedLive& p)
            {
                renderVoices (rendered, offset - rendered);
                rendered = offset;
                if (p.note.pitch < 0) releaseLive (transport.getPositionTicks(), lastSamplesPerTick);
                else                  applyLive (p.note);
            });
            renderVoices (rendered, numSamples - rendered);
        }

        wasPlaying = wantPlay;
        mixSlots (pair, numPair, numSamples);
    }

    void AudioEngine::mixSlots (float* const* pair, int numPair, int numSamples) noexcept
    {
        bool anySolo = false;
        for (auto& s : slots)
            anySolo |= s->getType() != SlotType::none && s->soloed.load (std::memory_order_acquire);

        const float smoothing = 1.0f - std::exp (-1.0f / (0.01f * (float) std::max (1.0, sampleRate.load())));

        for (auto& s : slots)
        {
            if (s->getType() == SlotType::none)
                continue;

            // Balance pan: centre is unity on both sides; panning turns the other side down.
            const bool audible = ! s->muted.load (std::memory_order_acquire) && (! anySolo || s->soloed.load (std::memory_order_acquire));
            const float gain = audible ? dbToGain (s->volumeDb.load (std::memory_order_acquire)) : 0.0f;
            const float pan = s->pan.load (std::memory_order_acquire);
            const float targetL = gain * std::min (1.0f, 1.0f - pan), targetR = gain * std::min (1.0f, 1.0f + pan);

            for (int i = 0; i < numSamples; ++i)
            {
                s->gainL += smoothing * (targetL - s->gainL);
                s->gainR += smoothing * (targetR - s->gainR);
                // Land exactly on the target (a muted track is truly silent).
                if (std::abs (targetL - s->gainL) < 1e-5f) s->gainL = targetL;
                if (std::abs (targetR - s->gainR) < 1e-5f) s->gainR = targetR;
                s->left[(size_t) i] *= s->gainL;    // post-fader, for the track meter
                s->right[(size_t) i] *= s->gainR;
                if (numPair > 0) pair[0][i] += s->left[(size_t) i];
                if (numPair > 1) pair[1][i] += s->right[(size_t) i];
            }
            const float* lr[] { s->left.data(), s->right.data() };
            s->meter.process (lr, 2, numSamples);

            s->activeVoices.store (s->instruments.activeVoices(), std::memory_order_relaxed);
        }

        // Master volume on the tracks' sum (smoothed like the track gains; exactly 1 at 0 dB).
        const float masterTarget = dbToGain (masterVolumeDb.load (std::memory_order_acquire));
        if (masterGain != masterTarget || masterTarget != 1.0f)
            for (int i = 0; i < numSamples; ++i)
            {
                masterGain += smoothing * (masterTarget - masterGain);
                if (std::abs (masterTarget - masterGain) < 1e-5f) masterGain = masterTarget;
                for (int ch = 0; ch < numPair; ++ch)
                    pair[ch][i] *= masterGain;
            }

        // Metronome straight to the output (both channels), not affected by the master volume.
        for (int ch = 0; ch < numPair; ++ch)
            for (int i = 0; i < numSamples; ++i)
                pair[ch][i] += metronomeBuffer[(size_t) i];
    }
}
