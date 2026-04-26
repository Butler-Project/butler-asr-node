#ifndef TEST_WAV_LOADER_H
#define TEST_WAV_LOADER_H

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test {

struct WavData {
    std::uint32_t sample_rate{ 0 };
    std::uint16_t channels{ 0 };
    std::vector<float> samples; // normalized to [-1, 1]
};

// Loads a PCM-16 WAV file (mono or stereo, any sample rate).
// Converts int16 samples to float32 normalized to [-1, 1].
// Throws std::runtime_error on format or I/O errors.
[[nodiscard]] inline WavData load_wav_pcm16(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error{ "cannot open WAV: " + path };

    auto read_u32 = [&]() -> std::uint32_t {
        std::uint32_t v{};
        f.read(reinterpret_cast<char*>(&v), 4);
        return v;
    };
    auto read_u16 = [&]() -> std::uint16_t {
        std::uint16_t v{};
        f.read(reinterpret_cast<char*>(&v), 2);
        return v;
    };

    // RIFF header
    char magic[4]{};
    f.read(magic, 4);
    if (std::string_view{ magic, 4 } != "RIFF")
        throw std::runtime_error{ "not a RIFF file: " + path };
    read_u32(); // chunk size, ignored
    char wave[4]{};
    f.read(wave, 4);
    if (std::string_view{ wave, 4 } != "WAVE")
        throw std::runtime_error{ "not a WAVE file: " + path };

    WavData result{};
    std::vector<std::int16_t> raw{};
    std::uint16_t bits_per_sample{ 0 };

    // Iterate over chunks until data is found
    while (f) {
        char chunk_id[4]{};
        if (f.read(chunk_id, 4).gcount() < 4) break;
        const auto chunk_size = read_u32();

        if (std::string_view{ chunk_id, 4 } == "fmt ") {
            const auto audio_format = read_u16();
            if (audio_format != 1)
                throw std::runtime_error{ "only PCM (format=1) WAVs are supported: " + path };
            result.channels        = read_u16();
            result.sample_rate     = read_u32();
            read_u32(); // byte rate
            read_u16(); // block align
            bits_per_sample        = read_u16();
            if (bits_per_sample != 16)
                throw std::runtime_error{ "only 16-bit PCM WAVs are supported: " + path };
            if (chunk_size > 16)
                f.seekg(static_cast<std::streamoff>(chunk_size - 16U), std::ios::cur);
        } else if (std::string_view{ chunk_id, 4 } == "data") {
            const auto n_samples = chunk_size / sizeof(std::int16_t);
            raw.resize(n_samples);
            f.read(reinterpret_cast<char*>(raw.data()),
                   static_cast<std::streamsize>(chunk_size));
            break; // data chunk is last; stop here
        } else {
            // Skip unknown chunk (WAV chunks are word-aligned)
            const auto skip = (chunk_size + 1U) & ~1U;
            f.seekg(static_cast<std::streamoff>(skip), std::ios::cur);
        }
    }

    if (result.sample_rate == 0)
        throw std::runtime_error{ "no fmt chunk found: " + path };
    if (raw.empty())
        throw std::runtime_error{ "no data chunk found: " + path };

    result.samples.resize(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i)
        result.samples[i] = static_cast<float>(raw[i]) / 32768.0F;

    return result;
}

} // namespace test

#endif // TEST_WAV_LOADER_H
