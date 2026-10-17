#include "beatforge/audio/AudioEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace beatforge::audio
{
    namespace
    {
        std::uint32_t pack (time::TimeSignature s) noexcept   { return (std::uint32_t) s.numerator << 8 | (std::uint32_t) s.denominator; }
        time::TimeSignature unpack (std::uint32_t v) noexcept  { return { (int) (v >> 8), (int) (v & 0xff) }; }
    }

    AudioEngine::AudioEngine()
        : packedSignature (pack ({}))
    {
        static_assert (std::atomic<double>::is_always_lock_free);
        static_assert (std::atomic<time::Tick>::is_always_lock_free);
    }

    void AudioEngine::prepare (double newSampleRate) noexcept
    {
        transport.prepare (newSampleRate);
        metronome.prepare (newSampleRate);
        drums.prepare (newSampleRate);   // allocates the kit; never runs alongside process()
        instruments.prepare (newSampleRate);
        sampleRate.store (newSampleRate, std::memory_order_release);
    }

    void AudioEngine::reset() noexcept
    {
        playRequested.store (false, std::memory_order_release);
        transport.rewind();
        metronome.reset();
        drums.reset();
        instruments.reset();
        publishPosition();
        state.store (TransportState::stopped, std::memory_order_release);
    }

    void AudioEngine::setTempo (double bpm) noexcept
    {
        tempo.store (time::clampTempo (bpm), std::memory_order_release);
    }

    void AudioEngine::setTimeSignature (time::TimeSignature sig) noexcept
    {
        if (sig.isValid())
            packedSignature.store (pack (sig), std::memory_order_release);
    }

    time::TimeSignature AudioEngine::getTimeSignature() const noexcept
    {
        return unpack (packedSignature.load (std::memory_order_acquire));
    }

    void AudioEngine::setMetronomeEnabled (bool enabled) noexcept
    {
        metronomeEnabled.store (enabled, std::memory_order_release);
    }

    void AudioEngine::setLoop (bool enabled, time::Tick start, time::Tick end) noexcept
    {
        // Disable first so the audio thread never pairs a new start with an old end.
        loopEnabled.store (false, std::memory_order_release);
        loopStart.store (start, std::memory_order_release);
        loopEnd.store (end, std::memory_order_release);
        loopEnabled.store (enabled, std::memory_order_release);
    }

    void AudioEngine::applySettings() noexcept
    {
        transport.setTempo (tempo.load (std::memory_order_acquire));
        transport.setTimeSignature (getTimeSignature());

        const bool loop = loopEnabled.load (std::memory_order_acquire);
        transport.setLoop (loop, loopStart.load (std::memory_order_acquire), loopEnd.load (std::memory_order_acquire));
    }

    void AudioEngine::publishPosition() noexcept
    {
        positionTicks.store (transport.getPositionTicks(), std::memory_order_release);
        positionSamples.store (transport.getElapsedSamples(), std::memory_order_release);
    }

    void AudioEngine::setMidiClip (const midi::Clip* clip)
    {
        schedules.publish (clip != nullptr ? midi::NoteSchedule::build (*clip)
                                           : std::make_unique<midi::NoteSchedule>());
    }

    void AudioEngine::collectGarbage()
    {
        schedules.collectGarbage();
    }

    void AudioEngine::renderVoices (float* const* outputs, int numOutputs, int start, int num) noexcept
    {
        if (num <= 0)
            return;

        metronome.render (outputs, numOutputs, start, num);
        drums.render (outputs, numOutputs, start, num);
        instruments.render (outputs, numOutputs, start, num);
    }

    void AudioEngine::releaseNotesNotCovering (double absoluteTick) noexcept
    {
        if (schedule == nullptr)
            return instruments.releaseAll();

        const auto local = std::fmod (absoluteTick, (double) schedule->clipLength);
        instruments.releaseUnless ([&] (midi::NoteId id) { return schedule->covers (id, local); });
    }

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

        applySettings();
        drums.beginBlock();
        drums.startPendingPreviews();

        instruments.beginBlock();

        PreviewQueue::Request preview;
        while (previews.pop (preview))
        {
            if (preview.velocity > 0) instruments.noteOn (PolySynthInstrument::previewId, preview.pitch, preview.velocity);
            else                      instruments.previewOff (preview.pitch);
        }

        bool scheduleChanged = false;
        schedule = schedules.acquire (scheduleChanged);

        const bool wantPlay = playRequested.load (std::memory_order_acquire);
        const bool wasPlaying = state.load (std::memory_order_relaxed) == TransportState::playing;

        if (wantPlay)
        {
            // A clip edit while playing: notes that were deleted or moved away
            // from the playhead must not keep sounding.
            if (scheduleChanged && wasPlaying)
                releaseNotesNotCovering (transport.getPositionTicks());

            // Everything that happens in this block, in sample order: grid lines
            // (metronome, drum steps), note on/off, loop jumps.
            enum Kind : std::uint8_t { noteOff, jump, gridLine, noteOn };
            struct Event
            {
                int offset;
                Kind kind;
                time::Transport::GridLine line;
                const midi::NoteEvent* note;
                double tick;
            };

            std::array<Event, maxEventsPerBlock> events;
            int numEvents = 0;
            auto add = [&] (const Event& e) { if (numEvents < maxEventsPerBlock) events[(size_t) numEvents++] = e; };

            transport.advance (numSamples,
                [&] (const time::Transport::GridLine& line)
                {
                    add ({ line.sampleOffset, gridLine, line, nullptr, 0.0 });
                },
                [&] (const time::Transport::Segment& seg)
                {
                    if (seg.afterJump)
                        add ({ seg.sampleOffset, jump, {}, nullptr, seg.startTick });

                    if (schedule == nullptr || schedule->events.empty())
                        return;

                    // Events with absolute tick in (eventsAfter, eventsUpTo], walking
                    // each repetition of the looping clip that overlaps that range.
                    const auto clipLength = (double) schedule->clipLength;
                    for (double cycle = std::floor (seg.eventsAfter / clipLength) * clipLength;
                         cycle <= seg.eventsUpTo; cycle += clipLength)
                    {
                        const double from = seg.eventsAfter - cycle, upTo = seg.eventsUpTo - cycle;
                        auto it = std::upper_bound (schedule->events.begin(), schedule->events.end(), from,
                                                    [] (double t, const midi::NoteEvent& e) { return t < (double) e.tick; });

                        for (; it != schedule->events.end() && (double) it->tick <= upTo; ++it)
                            add ({ seg.sampleFor (cycle + (double) it->tick), it->on ? noteOn : noteOff, {}, &*it, 0.0 });
                    }
                });

            // Stable insertion sort by (offset, kind): blocks hold few events.
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
                renderVoices (pair, numPair, rendered, e.offset - rendered);
                rendered = e.offset;

                switch (e.kind)
                {
                    case noteOff:  instruments.noteOff (e.note->id); break;
                    case noteOn:   instruments.noteOn (e.note->id, e.note->pitch, e.note->velocity); break;
                    case jump:     releaseNotesNotCovering (e.tick); break;
                    case gridLine:
                        if (e.line.isBeat)
                        {
                            ++beats;
                            if (clicks)
                                metronome.trigger (e.line.isDownbeat());
                        }

                        if (e.line.isStep)
                            drums.triggerStep ((int) ((e.line.tick / time::Transport::stepTicks) % DrumMachine::numSteps));
                        break;
                }
            }

            renderVoices (pair, numPair, rendered, numSamples - rendered);
            beatCount.fetch_add (beats, std::memory_order_relaxed);
            publishPosition();
            state.store (TransportState::playing, std::memory_order_release);
        }
        else
        {
            // Stopping releases every clip note (no stuck notes); auditions and
            // ringing tails keep sounding while stopped.
            if (wasPlaying)
                instruments.releaseUnless ([] (midi::NoteId) { return false; });

            renderVoices (pair, numPair, 0, numSamples);

            if (state.load (std::memory_order_relaxed) != TransportState::stopped)
            {
                transport.rewind();
                publishPosition();
                state.store (TransportState::stopped, std::memory_order_release);
            }
        }

        activeSynthVoices.store (instruments.activeVoices(), std::memory_order_relaxed);
        outputMeter.process (outputs, std::min (numOutputs, LevelMeter::maxChannels), numSamples);
    }
}
