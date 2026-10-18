#include "beatforge/project/AudioExport.h"
#include "beatforge/audio/AudioEngine.h"
#include "beatforge/project/Session.h"
#include "beatforge/time/TempoMap.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace beatforge::project
{
    time::Tick songEnd (const Project& p) noexcept
    {
        time::Tick end = 0;
        for (auto& t : p.tracks)
            for (auto& c : t.clips)
                end = std::max (end, c.end());
        return end;
    }

    AudioExportResult renderAudio (const Project& project, const AudioExportOptions& options, const RenderSink& sink,
                                   AudioExportProgress* progress)
    {
        AudioExportResult result;
        result.sampleRate = options.sampleRate;

        const bool song = options.range == AudioExportOptions::Range::song;
        const auto start = song ? time::Tick (0) : project.loopStart;
        const auto end = song ? songEnd (project) : project.loopEnd;
        if (end <= start)
        {
            result.error = song ? "Nothing to export: the song has no clips yet. Create a clip first (double-click a track lane)."
                                : "The loop region is empty. Drag across the ruler's top strip to set one.";
            return result;
        }
        if (options.sampleRate < 8000.0 || options.sampleRate > 192000.0)
        {
            result.error = "Unsupported sample rate.";
            return result;
        }

        // A private engine and session play a copy: no metronome, no looping.
        audio::AudioEngine engine;
        engine.prepare (options.sampleRate);
        Session session (engine);
        auto copy = project;
        copy.loopEnabled = false;
        copy.metronomeEnabled = false;
        session.openProject (std::move (copy), std::nullopt);

        // Frames up to (not including) the first sample at or after `end`, as the transport counts them.
        const double samplesPerTick = time::TempoMap::secondsPerTick (project.tempo) * options.sampleRate;
        result.musicFrames = (std::int64_t) std::ceil ((double) (end - start) * samplesPerTick - 1e-7);
        const auto maxTailFrames = song ? (std::int64_t) (AudioExportOptions::maxTailSeconds * options.sampleRate) : 0;

        constexpr int blockSize = 1024;
        std::vector<float> left (blockSize), right (blockSize);
        float* stereo[] { left.data(), right.data() };

        engine.seek ((double) start);
        engine.play();

        auto renderBlock = [&] (int n) -> bool
        {
            engine.process (stereo, 2, n);
            for (int i = 0; i < n; ++i)
                result.peak = std::max ({ result.peak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]) });
            if (! sink (stereo, n, result.error))
                return false;
            result.frames += n;
            return true;
        };

        const auto total = (double) (result.musicFrames + maxTailFrames);
        auto cancelled = [&] { return progress != nullptr && progress->cancel.load (std::memory_order_relaxed); };
        auto report = [&] { if (progress != nullptr) progress->fraction.store ((float) ((double) result.frames / total), std::memory_order_relaxed); };

        // The music, ending exactly on the last frame.
        while (result.frames < result.musicFrames)
        {
            if (cancelled())
                return result.cancelled = true, result;
            if (! renderBlock ((int) std::min<std::int64_t> (blockSize, result.musicFrames - result.frames)))
                return result;
            report();
        }

        // The tails: notes are released at the end (as Stop does) and ring out until silent.
        engine.stop();
        for (std::int64_t tail = 0; tail < maxTailFrames;)
        {
            if (cancelled())
                return result.cancelled = true, result;
            const auto n = (int) std::min<std::int64_t> (blockSize, maxTailFrames - tail);
            engine.process (stereo, 2, n);
            float blockPeak = 0.0f;
            for (int i = 0; i < n; ++i)
                blockPeak = std::max ({ blockPeak, std::abs (left[(size_t) i]), std::abs (right[(size_t) i]) });
            if (blockPeak < AudioExportOptions::silence)
                break;
            result.peak = std::max (result.peak, blockPeak);
            if (! sink (stereo, n, result.error))
                return result;
            result.frames += n;
            tail += n;
            report();
        }

        if (progress != nullptr)
            progress->fraction.store (1.0f, std::memory_order_relaxed);
        return result;
    }

    AudioExportResult exportAudio (const Project& project, const std::filesystem::path& path,
                                   const AudioExportOptions& options, AudioExportProgress* progress)
    {
        audio::WavWriter writer;
        AudioExportResult result;
        if (! writer.open (path, options.sampleRate, 2, options.format, result.error))
            return result;

        result = renderAudio (project, options, [&] (const float* const* stereo, int n, std::string& error)
        {
            return writer.write (stereo, n, error);
        }, progress);

        if (! result.ok())
        {
            writer.abort();   // nothing half-written stays behind
            return result;
        }
        result.samplesOverFullScale = writer.getSamplesOverFullScale();
        if (! writer.commit (result.error))
            return result;

        // Validation: read the file back. A file that fails is removed, never left half-right.
        auto rejected = [&] (const std::string& why)
        {
            std::error_code ignored;
            std::filesystem::remove (path, ignored);
            result.error = why + " The file was removed; please export again (or try another folder or format).";
            return result;
        };
        audio::WavReader reader;
        std::string error;
        if (! reader.open (path, error))
            return rejected ("The exported file could not be read back (" + error + ").");

        const auto& info = reader.info();
        const bool isFloat = options.format == audio::SampleFormat::float32;
        if (info.formatTag != (isFloat ? 3 : 1) || info.numChannels != 2 || info.sampleRate != (int) std::lround (options.sampleRate)
            || info.bitsPerSample != audio::bitsOf (options.format) || info.numFrames != result.frames)
            return rejected ("The exported file did not match the chosen format or length.");

        // Peak read back = peak rendered (within the dither and rounding of integer formats).
        std::vector<float> block (2048 * 2);
        float peak = 0.0f;
        for (int n; (n = reader.read (block.data(), 2048)) > 0;)
            for (int i = 0; i < n * 2; ++i)
                peak = std::max (peak, std::abs (block[(size_t) i]));
        // Float keeps the exact values; integer formats clip at full scale and add dither/rounding (< 3 LSB).
        const float expected = isFloat ? result.peak : std::min (result.peak, 1.0f);
        const float tolerance = isFloat ? 0.0f : 3.0f / (options.format == audio::SampleFormat::int16 ? 32768.0f : 8388608.0f);
        if (std::abs (peak - expected) > tolerance)
            return rejected ("The exported audio did not match what was rendered.");

        return result;
    }
}
