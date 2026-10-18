#include "beatforge/audio/AudioEngine.h"

#include <algorithm>
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
        publishPosition();
        state.store (TransportState::stopped, std::memory_order_release);
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
        if (s.schedule == nullptr)
            return s.instruments.releaseUnless ([] (VoiceId) { return false; });

        s.instruments.releaseUnless ([&] (VoiceId v) { return s.schedule->covers (v, tick); });
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

        for (int done = 0; done < numSamples; done += maxChunk)
        {
            const int n = std::min (maxChunk, numSamples - done);
            float* chunk[2] { pair[0] != nullptr ? pair[0] + done : nullptr, pair[1] != nullptr ? pair[1] + done : nullptr };
            processChunk (chunk, numPair, n);
        }

        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }

    void AudioEngine::processChunk (float* const* pair, int numPair, int numSamples) noexcept
    {
        applySettings();
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

        PreviewQueue::Request preview;
        while (previews.pop (preview))
        {
            auto& rack = slots[(size_t) clampSlot (preview.slot)]->instruments;
            if (preview.velocity > 0) rack.noteOn (PolySynthInstrument::previewId, preview.pitch, preview.velocity);
            else                      rack.previewOff (preview.pitch);
        }

        const bool wantPlay = playRequested.load (std::memory_order_acquire);

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

            // Everything that happens in this chunk, in sample order.
            enum Kind : std::uint8_t { noteOff, jump, gridLine, drumHit, noteOn };
            struct Event
            {
                int offset;
                Kind kind;
                int slot;
                time::Transport::GridLine line;
                const TrackSchedule::Event* event;
                double tick;
            };

            std::array<Event, maxEventsPerChunk> events;
            int numEvents = 0;
            auto add = [&] (const Event& e) { if (numEvents < maxEventsPerChunk) events[(size_t) numEvents++] = e; };

            transport.advance (numSamples,
                [&] (const time::Transport::GridLine& line)
                {
                    if (line.isBeat)
                        add ({ line.sampleOffset, gridLine, -1, line, nullptr, 0.0 });
                },
                [&] (const time::Transport::Segment& seg)
                {
                    if (seg.afterJump)
                        add ({ seg.sampleOffset, jump, -1, {}, nullptr, seg.startTick });

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
                            add ({ seg.sampleFor ((double) it->tick), k, i, {}, &*it, 0.0 });
                        }
                    }
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
                    case jump:
                        for (auto& s : slots)
                            releaseNotesNotCovering (*s, e.tick);
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
            state.store (TransportState::playing, std::memory_order_release);
        }
        else
        {
            // Stopping releases every clip note (no stuck notes) and returns to
            // the play start; auditions and ringing tails keep sounding.
            if (wasPlaying)
            {
                for (auto& s : slots)
                    s->instruments.releaseUnless ([] (VoiceId) { return false; });
                transport.rewind();
                transport.seekTo (playStart.load (std::memory_order_acquire));
                publishPosition();
                state.store (TransportState::stopped, std::memory_order_release);
            }

            renderVoices (0, numSamples);
        }

        wasPlaying = wantPlay;
        mixSlots (pair, numPair, numSamples);
    }

    void AudioEngine::mixSlots (float* const* pair, int numPair, int numSamples) noexcept
    {
        // Metronome straight to the output (both channels).
        for (int ch = 0; ch < numPair; ++ch)
            for (int i = 0; i < numSamples; ++i)
                pair[ch][i] += metronomeBuffer[(size_t) i];

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
                if (numPair > 0) pair[0][i] += s->left[(size_t) i] * s->gainL;
                if (numPair > 1) pair[1][i] += s->right[(size_t) i] * s->gainR;
            }

            s->activeVoices.store (s->instruments.activeVoices(), std::memory_order_relaxed);
        }
    }
}
