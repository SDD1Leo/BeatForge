#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace beatforge::audio
{
    enum class SampleFormat { int16, int24, float32 };

    constexpr int bitsOf (SampleFormat f) noexcept { return f == SampleFormat::int16 ? 16 : f == SampleFormat::int24 ? 24 : 32; }

    // Streams a WAV file: PCM 16/24-bit (with TPDF dither) or 32-bit float.
    // Writes to "<file>.tmp" and renames on commit(), so a failed or cancelled
    // export never leaves a half-written file (or damages an existing one).
    class WavWriter
    {
    public:
        WavWriter() = default;
        ~WavWriter() { abort(); }

        bool open (const std::filesystem::path&, double sampleRate, int numChannels, SampleFormat, std::string& error);
        // Converts and writes `numFrames` frames of `numChannels` channels.
        bool write (const float* const* channels, int numFrames, std::string& error);
        bool commit (std::string& error);   // finishes the header, renames into place
        void abort();                       // closes and deletes the temporary file

        std::int64_t getFramesWritten() const noexcept { return frames; }
        // Samples beyond full scale (clipped in integer formats; kept in float).
        std::int64_t getSamplesOverFullScale() const noexcept { return overFullScale; }

        static constexpr std::uint64_t maxDataBytes = 0xffffffffull - 64;   // RIFF size limit

    private:
        float dither() noexcept;

        std::filesystem::path target, temp;
        std::ofstream out;
        SampleFormat format = SampleFormat::int24;
        int channels = 2;
        std::int64_t frames = 0, overFullScale = 0;
        std::uint64_t dataBytes = 0;
        std::uint32_t rng = 0x9e3779b9u;   // fixed seed: exports are reproducible
        std::vector<std::uint8_t> buffer;
        bool isOpen = false;
    };

    struct WavInfo
    {
        int formatTag = 0;          // 1 = PCM, 3 = IEEE float
        int numChannels = 0;
        int sampleRate = 0;
        int bitsPerSample = 0;
        std::int64_t numFrames = 0;
    };

    // Reads WAV files written by WavWriter (and plain PCM/float WAVs in general).
    class WavReader
    {
    public:
        bool open (const std::filesystem::path&, std::string& error);
        const WavInfo& info() const noexcept { return wav; }
        // Reads up to `maxFrames` interleaved frames as float; returns frames read.
        int read (float* interleaved, int maxFrames);

    private:
        std::ifstream in;
        WavInfo wav;
        std::int64_t framesLeft = 0;
        std::vector<std::uint8_t> buffer;
    };
}
