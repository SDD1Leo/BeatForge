#include "beatforge/audio/WavFile.h"
#include "beatforge/util/Paths.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace beatforge::audio
{
    namespace
    {
        void put16 (std::vector<std::uint8_t>& b, std::uint32_t v) { b.push_back ((std::uint8_t) v); b.push_back ((std::uint8_t) (v >> 8)); }
        void put32 (std::vector<std::uint8_t>& b, std::uint32_t v) { put16 (b, v & 0xffff); put16 (b, v >> 16); }
        void tag (std::vector<std::uint8_t>& b, const char* t) { b.insert (b.end(), t, t + 4); }

        std::uint32_t get16 (const std::uint8_t* p) { return (std::uint32_t) p[0] | (std::uint32_t) p[1] << 8; }
        std::uint32_t get32 (const std::uint8_t* p) { return get16 (p) | get16 (p + 2) << 16; }

        // Header with sizes; called again on commit with the final sizes.
        std::vector<std::uint8_t> header (SampleFormat f, int channels, double sampleRate, std::uint64_t dataBytes, std::int64_t frames)
        {
            const bool isFloat = f == SampleFormat::float32;
            const auto bytesPerSample = (std::uint32_t) bitsOf (f) / 8;
            const auto blockAlign = bytesPerSample * (std::uint32_t) channels;
            const auto rate = (std::uint32_t) std::lround (sampleRate);

            std::vector<std::uint8_t> h;
            tag (h, "RIFF");
            put32 (h, (std::uint32_t) ((isFloat ? 50 : 36) + dataBytes));
            tag (h, "WAVE");
            tag (h, "fmt ");
            put32 (h, isFloat ? 18 : 16);
            put16 (h, isFloat ? 3 : 1);
            put16 (h, (std::uint32_t) channels);
            put32 (h, rate);
            put32 (h, rate * blockAlign);
            put16 (h, blockAlign);
            put16 (h, (std::uint32_t) bitsOf (f));
            if (isFloat)
            {
                put16 (h, 0);   // cbSize
                tag (h, "fact");
                put32 (h, 4);
                put32 (h, (std::uint32_t) frames);
            }
            tag (h, "data");
            put32 (h, (std::uint32_t) dataBytes);
            return h;
        }
    }

    //==============================================================================
    bool WavWriter::open (const std::filesystem::path& path, double sampleRate, int numChannels, SampleFormat f, std::string& error)
    {
        abort();
        target = path;
        temp = path;
        temp += ".tmp";
        format = f;
        channels = numChannels;
        frames = overFullScale = 0;
        dataBytes = 0;
        rng = 0x9e3779b9u;

        errno = 0;
        out.open (temp, std::ios::binary | std::ios::trunc);
        if (! out)
        {
            error = "Could not write to " + pathToUtf8 (path.parent_path()) + ": " + std::strerror (errno)
                    + ". Check that the folder exists and is writable.";
            return false;
        }

        const auto h = header (format, channels, sampleRate, 0, 0);
        out.write (reinterpret_cast<const char*> (h.data()), (std::streamsize) h.size());
        isOpen = true;
        return true;
    }

    float WavWriter::dither() noexcept
    {
        // TPDF: the sum of two uniform values in [-0.5, 0.5) LSB.
        auto next = [this]
        {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            return (float) (rng >> 8) / 16777216.0f - 0.5f;
        };
        return next() + next();
    }

    bool WavWriter::write (const float* const* data, int numFrames, std::string& error)
    {
        if (! isOpen)
            return error = "The export file is not open.", false;

        const auto bytesPerSample = (size_t) bitsOf (format) / 8;
        const auto bytes = (size_t) numFrames * (size_t) channels * bytesPerSample;
        if (dataBytes + bytes > maxDataBytes)
            return error = "The export is larger than a WAV file can hold (4 GB). Choose a lower sample rate or bit depth.", false;

        buffer.resize (bytes);
        auto* p = buffer.data();
        const float scale = format == SampleFormat::int16 ? 32767.0f : 8388607.0f;
        const float lo = format == SampleFormat::int16 ? -32768.0f : -8388608.0f;

        for (int i = 0; i < numFrames; ++i)
            for (int ch = 0; ch < channels; ++ch)
            {
                const float x = data[ch][i];
                if (std::abs (x) > 1.0f)
                    ++overFullScale;

                if (format == SampleFormat::float32)
                {
                    std::uint32_t bits;
                    std::memcpy (&bits, &x, 4);
                    for (int b = 0; b < 4; ++b) *p++ = (std::uint8_t) (bits >> (8 * b));
                    continue;
                }

                const auto v = (std::int32_t) std::clamp (std::round (x * scale + dither()), lo, scale);
                const auto u = (std::uint32_t) v;
                for (size_t b = 0; b < bytesPerSample; ++b) *p++ = (std::uint8_t) (u >> (8 * b));
            }

        out.write (reinterpret_cast<const char*> (buffer.data()), (std::streamsize) bytes);
        if (! out)
            return error = "Writing the WAV file failed (the disk may be full).", false;

        dataBytes += bytes;
        frames += numFrames;
        return true;
    }

    bool WavWriter::commit (std::string& error)
    {
        if (! isOpen)
            return error = "The export file is not open.", false;

        // Odd-sized data is padded (24-bit mono with an odd frame count); the
        // padding counts in the RIFF size but not in the data size.
        const auto pad = dataBytes % 2;
        if (pad != 0)
            out.put (0);

        const auto h = header (format, channels, 0.0, dataBytes, frames);
        const auto riff = header (format, channels, 0.0, dataBytes + pad, frames);
        out.seekp (4);
        out.write (reinterpret_cast<const char*> (riff.data() + 4), 4);  // RIFF size
        out.seekp ((std::streamoff) h.size() - 4);
        out.write (reinterpret_cast<const char*> (h.data() + h.size() - 4), 4);   // data size
        if (format == SampleFormat::float32)
        {
            out.seekp ((std::streamoff) h.size() - 12);
            out.write (reinterpret_cast<const char*> (h.data() + h.size() - 12), 4);   // fact: frames
        }
        out.flush();
        out.close();
        isOpen = false;
        if (! out)
        {
            std::error_code ignored;
            std::filesystem::remove (temp, ignored);
            return error = "Finishing the WAV file failed (the disk may be full).", false;
        }

        std::error_code ec;
        std::filesystem::rename (temp, target, ec);
        if (ec)
        {
            std::error_code ignored;
            std::filesystem::remove (temp, ignored);
            return error = "Could not replace " + pathToUtf8 (target.filename()) + ": " + ec.message() + ".", false;
        }
        return true;
    }

    void WavWriter::abort()
    {
        if (! isOpen)
            return;
        out.close();
        isOpen = false;
        std::error_code ignored;
        std::filesystem::remove (temp, ignored);
    }

    //==============================================================================
    bool WavReader::open (const std::filesystem::path& path, std::string& error)
    {
        in.open (path, std::ios::binary);
        if (! in)
            return error = "Could not open " + pathToUtf8 (path) + ".", false;

        std::uint8_t h[12];
        if (! in.read (reinterpret_cast<char*> (h), 12) || std::memcmp (h, "RIFF", 4) != 0 || std::memcmp (h + 8, "WAVE", 4) != 0)
            return error = "Not a WAV file.", false;

        bool haveFormat = false;
        for (;;)
        {
            std::uint8_t chunk[8];
            if (! in.read (reinterpret_cast<char*> (chunk), 8))
                return error = "The WAV file has no audio data.", false;
            const auto size = get32 (chunk + 4);

            if (std::memcmp (chunk, "fmt ", 4) == 0)
            {
                std::vector<std::uint8_t> fmt (size);
                if (size < 16 || ! in.read (reinterpret_cast<char*> (fmt.data()), size))
                    return error = "The WAV format chunk is damaged.", false;
                wav.formatTag = (int) get16 (&fmt[0]);
                wav.numChannels = (int) get16 (&fmt[2]);
                wav.sampleRate = (int) get32 (&fmt[4]);
                wav.bitsPerSample = (int) get16 (&fmt[14]);
                haveFormat = true;
            }
            else if (std::memcmp (chunk, "data", 4) == 0)
            {
                if (! haveFormat || wav.numChannels < 1 || wav.bitsPerSample < 8)
                    return error = "The WAV format is missing or unsupported.", false;
                wav.numFrames = (std::int64_t) size / (wav.numChannels * (wav.bitsPerSample / 8));
                framesLeft = wav.numFrames;
                return true;
            }
            else
            {
                in.seekg (size + (size & 1), std::ios::cur);
            }
        }
    }

    int WavReader::read (float* interleaved, int maxFrames)
    {
        const auto n = (int) std::min<std::int64_t> (maxFrames, framesLeft);
        const auto bytesPerSample = wav.bitsPerSample / 8;
        const auto samples = (size_t) n * (size_t) wav.numChannels;
        buffer.resize (samples * (size_t) bytesPerSample);
        if (n <= 0 || ! in.read (reinterpret_cast<char*> (buffer.data()), (std::streamsize) buffer.size()))
            return 0;

        const auto* p = buffer.data();
        for (size_t i = 0; i < samples; ++i, p += bytesPerSample)
        {
            if (wav.formatTag == 3 && bytesPerSample == 4)
            {
                std::memcpy (&interleaved[i], p, 4);   // little-endian host
                continue;
            }
            std::int32_t v = 0;
            for (int b = 0; b < bytesPerSample; ++b)
                v |= (std::int32_t) p[b] << (8 * b);
            const int shift = 32 - 8 * bytesPerSample;
            v = (std::int32_t) ((std::uint32_t) v << shift) >> shift;   // sign-extend
            interleaved[i] = (float) v / (float) (1u << (8 * bytesPerSample - 1));
        }
        framesLeft -= n;
        return n;
    }
}
