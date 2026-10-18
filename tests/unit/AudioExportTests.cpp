#include "EngineHelpers.h"

#include <beatforge/audio/WavFile.h>
#include <beatforge/project/AudioExport.h>
#include <beatforge/project/Session.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using namespace beatforge;
using project::AudioExportOptions;
using audio::SampleFormat;
using Catch::Approx;

namespace
{
    constexpr time::Tick bar = 3840;

    // 4 bars: kick + hats all along, a bass line, a chord ringing to the end.
    project::Project song()
    {
        auto p = project::Project::makeDefault();
        auto& drums = p.tracks[0];
        drums.clips[0].length = 4 * bar;
        drums.clips[0].pattern = {};
        for (int s : { 0, 4, 8, 12 }) drums.clips[0].pattern[0][(size_t) s] = true;
        for (int s : { 2, 6, 10, 14 }) drums.clips[0].pattern[2][(size_t) s] = true;

        project::Clip c;
        c.id = 9;
        c.start = 0;
        c.length = 4 * bar;
        midi::NoteId id = 1;
        for (int b = 0; b < 4; ++b)
            c.notes.push_back ({ id++, 45 + b * 2, 100, b * bar, 1920 });
        for (int pitch : { 60, 64, 67 })
            c.notes.push_back ({ id++, pitch, 90, 3 * bar, bar });   // ends exactly at the song end
        p.tracks[1].clips = { c };
        p.loopStart = bar;
        p.loopEnd = 3 * bar;
        p.loopEnabled = true;            // ignored by the song export
        p.metronomeEnabled = true;       // never exported
        return p;
    }

    std::vector<float> renderLeft (const project::Project& p, const AudioExportOptions& o, project::AudioExportResult& result)
    {
        std::vector<float> out;
        result = project::renderAudio (p, o, [&] (const float* const* stereo, int n, std::string&)
        {
            out.insert (out.end(), stereo[0], stereo[0] + n);
            return true;
        });
        return out;
    }

    std::filesystem::path tempDir()
    {
        auto d = std::filesystem::temp_directory_path() / "beatforge-export-test";
        std::filesystem::create_directories (d);
        return d;
    }
}

TEST_CASE ("The export is what live playback plays, sample for sample", "[export]")
{
    const auto p = song();
    AudioExportOptions o;
    project::AudioExportResult r;
    const auto offline = renderLeft (p, o, r);
    REQUIRE (r.ok());

    // Live: the app's engine and session, playing from bar 1 without loop or metronome.
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    project::Session session (engine);
    auto copy = p;
    copy.loopEnabled = false;
    copy.metronomeEnabled = false;
    session.openProject (copy, std::nullopt);
    engine.play();
    const auto live = test::render (engine, (int) r.musicFrames, 512);

    REQUIRE (offline.size() >= live.size());
    size_t differing = 0;
    for (size_t i = 0; i < live.size(); ++i)
        differing += offline[i] != live[i] ? 1 : 0;
    REQUIRE (differing == 0);
    REQUIRE (test::peakIn (live) > 0.1f);
}

TEST_CASE ("Song length is exact at every sample rate and tempo; tails ring out, then stop", "[export]")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto p = song();
        AudioExportOptions o;
        o.sampleRate = rate;
        project::AudioExportResult r;
        const auto out = renderLeft (p, o, r);
        INFO ("rate " << rate);
        REQUIRE (r.musicFrames == (std::int64_t) (8.0 * rate));          // 4 bars at 120 BPM = 8 s
        REQUIRE (r.frames > r.musicFrames);                               // the chord's release
        REQUIRE (r.frames - r.musicFrames < (std::int64_t) (1.0 * rate)); // ...ends well before the 4 s limit
        REQUIRE (test::peakIn (out, (size_t) r.musicFrames, (size_t) r.musicFrames + 100) > 0.001f);
        REQUIRE (std::abs (out.back()) < 0.01f);
    }

    auto p = song();
    p.tempo = 133.0;
    project::AudioExportResult r;
    renderLeft (p, {}, r);
    REQUIRE (r.musicFrames == (std::int64_t) std::ceil (4.0 * bar * 60.0 / (133.0 * 960.0) * 48000.0 - 1e-7));
}

TEST_CASE ("The loop region exports exactly, without a tail", "[export]")
{
    auto p = song();
    AudioExportOptions o;
    o.range = AudioExportOptions::Range::loop;
    project::AudioExportResult r;
    const auto out = renderLeft (p, o, r);
    REQUIRE (r.ok());
    REQUIRE (r.frames == 4 * 48000);    // bars 2-3
    REQUIRE (r.musicFrames == r.frames);

    // The same as live playback started at bar 2 (notes from bar 1 are not carried in).
    audio::AudioEngine engine;
    engine.prepare (48000.0);
    project::Session session (engine);
    auto copy = p;
    copy.loopEnabled = false;
    copy.metronomeEnabled = false;
    session.openProject (copy, std::nullopt);
    engine.seek ((double) bar);
    engine.play();
    const auto live = test::render (engine, (int) r.frames, 512);
    REQUIRE (out == live);
}

TEST_CASE ("WAV files: every format is written, read back and validated", "[export][wav]")
{
    const auto p = song();
    for (auto format : { SampleFormat::int16, SampleFormat::int24, SampleFormat::float32 })
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            AudioExportOptions o;
            o.format = format;
            o.sampleRate = rate;
            const auto path = tempDir() / "song.wav";
            const auto r = project::exportAudio (p, path, o);
            INFO ("bits " << audio::bitsOf (format) << " rate " << rate << ": " << r.error);
            REQUIRE (r.ok());
            REQUIRE_FALSE (std::filesystem::exists (tempDir() / "song.wav.tmp"));

            audio::WavReader reader;
            std::string error;
            REQUIRE (reader.open (path, error));
            REQUIRE (reader.info().numChannels == 2);
            REQUIRE (reader.info().sampleRate == (int) rate);
            REQUIRE (reader.info().bitsPerSample == audio::bitsOf (format));
            REQUIRE (reader.info().formatTag == (format == SampleFormat::float32 ? 3 : 1));
            REQUIRE (reader.info().numFrames == r.frames);
            REQUIRE (std::filesystem::file_size (path)
                     == (std::uintmax_t) (r.frames * 2 * audio::bitsOf (format) / 8 + (format == SampleFormat::float32 ? 58 : 44)));
        }

    // Float keeps every sample exactly; 24-bit is within dither + rounding.
    project::AudioExportResult r;
    const auto rendered = renderLeft (p, {}, r);
    for (auto format : { SampleFormat::float32, SampleFormat::int24 })
    {
        AudioExportOptions o;
        o.format = format;
        const auto path = tempDir() / "check.wav";
        REQUIRE (project::exportAudio (p, path, o).ok());
        audio::WavReader reader;
        std::string error;
        REQUIRE (reader.open (path, error));
        std::vector<float> data ((size_t) reader.info().numFrames * 2);
        REQUIRE (reader.read (data.data(), (int) reader.info().numFrames) == (int) reader.info().numFrames);
        float worst = 0.0f;
        for (size_t i = 0; i < rendered.size(); ++i)
            worst = std::max (worst, std::abs (data[i * 2] - rendered[i]));
        REQUIRE (worst <= (format == SampleFormat::float32 ? 0.0f : 2.0f / 8388608.0f));
    }
    std::filesystem::remove_all (tempDir());
}

TEST_CASE ("Cancel and errors leave no file behind", "[export]")
{
    const auto p = song();
    const auto path = tempDir() / "cancel.wav";

    SECTION ("cancelled halfway")
    {
        project::AudioExportProgress progress;
        audio::WavWriter writer;
        std::string error;
        REQUIRE (writer.open (path, 48000.0, 2, SampleFormat::int24, error));
        int blocks = 0;
        const auto r = project::renderAudio (p, {}, [&] (const float* const* s, int n, std::string& e)
        {
            if (++blocks == 200) progress.cancel = true;   // ~0.36 of the expected total (music + 4 s tail)
            return writer.write (s, n, e);
        }, &progress);
        REQUIRE (r.cancelled);
        REQUIRE (progress.fraction.load() > 0.1f);
        REQUIRE (progress.fraction.load() < 0.9f);
        writer.abort();   // this test's own writer; Windows cannot delete a file still open

        // exportAudio aborts the writer on cancel:
        project::AudioExportProgress cancelNow;
        cancelNow.cancel = true;
        const auto r2 = project::exportAudio (p, path, {}, &cancelNow);
        REQUIRE (r2.cancelled);
        REQUIRE_FALSE (std::filesystem::exists (path));
        REQUIRE_FALSE (std::filesystem::exists (tempDir() / "cancel.wav.tmp"));
    }
    SECTION ("an existing file survives a failed export")
    {
        REQUIRE (project::exportAudio (p, path, {}).ok());
        const auto size = std::filesystem::file_size (path);
        project::AudioExportProgress cancelNow;
        cancelNow.cancel = true;
        REQUIRE (project::exportAudio (p, path, {}, &cancelNow).cancelled);
        REQUIRE (std::filesystem::file_size (path) == size);
    }
    SECTION ("nothing to export / unwritable folder")
    {
        auto empty = song();
        for (auto& t : empty.tracks) t.clips.clear();
        REQUIRE (project::exportAudio (empty, path, {}).error.find ("no clips") != std::string::npos);
        REQUIRE_FALSE (project::exportAudio (p, tempDir() / "missing" / "x.wav", {}).error.empty());
    }
    std::filesystem::remove_all (tempDir());
}

TEST_CASE ("Integer formats clip at full scale and count it; float keeps overs", "[export][wav]")
{
    const auto path = tempDir() / "over.wav";
    std::vector<float> l { 0.5f, 1.5f, -2.0f, 0.0f }, r (4, 0.0f);
    const float* ch[] { l.data(), r.data() };

    for (auto format : { SampleFormat::int16, SampleFormat::float32 })
    {
        audio::WavWriter w;
        std::string error;
        REQUIRE (w.open (path, 48000.0, 2, format, error));
        REQUIRE (w.write (ch, 4, error));
        REQUIRE (w.getSamplesOverFullScale() == 2);
        REQUIRE (w.commit (error));

        audio::WavReader reader;
        REQUIRE (reader.open (path, error));
        std::vector<float> data (8);
        REQUIRE (reader.read (data.data(), 4) == 4);
        if (format == SampleFormat::int16)
        {
            REQUIRE (data[2] == Approx (32767.0f / 32768.0f));
            REQUIRE (data[4] == -1.0f);
            REQUIRE (data[0] == Approx (0.5f).margin (2.0f / 32768.0f));
            REQUIRE (std::abs (data[6]) <= 1.0f / 32768.0f);   // silence + dither: at most 1 LSB
        }
        else
        {
            REQUIRE (data[2] == 1.5f);
            REQUIRE (data[4] == -2.0f);
        }
    }
    std::filesystem::remove_all (tempDir());
}
