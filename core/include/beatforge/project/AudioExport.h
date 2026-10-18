#pragma once

#include "beatforge/audio/WavFile.h"
#include "beatforge/project/Project.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace beatforge::project
{
    // Offline rendering: a private engine plays a copy of the project, faster
    // than real time, block by block - the same engine code as live playback,
    // so the result is what you hear (without metronome or looping). Live
    // playback and editing are not affected. Any thread.
    //
    //  - song:  bar 1 to the end of the last clip, then the release tails until
    //           they fall silent (at most `maxTailSeconds`).
    //  - loop:  exactly the loop region (no tail), so the file loops seamlessly.
    struct AudioExportOptions
    {
        enum class Range { song, loop };

        Range range = Range::song;
        double sampleRate = 48000.0;
        audio::SampleFormat format = audio::SampleFormat::int24;

        static constexpr double maxTailSeconds = 4.0;
        static constexpr float silence = 1.0e-5f;   // -100 dBFS: the tail has ended
    };

    // Shared with the thread that runs the export (UI: progress bar, Cancel).
    struct AudioExportProgress
    {
        std::atomic<float> fraction { 0.0f };
        std::atomic<bool> cancel { false };
    };

    struct AudioExportResult
    {
        std::string error;
        bool cancelled = false;
        std::int64_t frames = 0;           // total, music + tail
        std::int64_t musicFrames = 0;
        double sampleRate = 0.0;
        float peak = 0.0f;                 // linear, of the rendered audio
        std::int64_t samplesOverFullScale = 0;

        bool ok() const noexcept { return error.empty() && ! cancelled; }
        double seconds() const noexcept { return sampleRate > 0.0 ? (double) frames / sampleRate : 0.0; }
        double musicSeconds() const noexcept { return sampleRate > 0.0 ? (double) musicFrames / sampleRate : 0.0; }
    };

    // Renders and hands each block (stereo, `numFrames` long) to `sink`;
    // `sink` returns false to stop with an error it reports in `error`.
    using RenderSink = std::function<bool (const float* const* stereo, int numFrames, std::string& error)>;
    AudioExportResult renderAudio (const Project&, const AudioExportOptions&, const RenderSink&, AudioExportProgress* = nullptr);

    // Renders to a WAV file (written atomically; nothing is left on cancel or
    // error), then reads the file back and checks format, length and peak.
    AudioExportResult exportAudio (const Project&, const std::filesystem::path&, const AudioExportOptions&,
                                   AudioExportProgress* = nullptr);

    // End of the song: the end of the last clip (0 when there are none).
    time::Tick songEnd (const Project&) noexcept;
}
