#ifndef ASR_UTILS_AUDIO_BUFFER_TRAITS_H
#define ASR_UTILS_AUDIO_BUFFER_TRAITS_H

#include <chrono>
#include <concepts>
#include <cstdint>

namespace asr::utils {

    [[nodiscard]] constexpr std::uint32_t operator""_hz (unsigned long long value) noexcept { return static_cast<std::uint32_t>(value); }
    [[nodiscard]] constexpr std::uint8_t  operator""_ch (unsigned long long value) noexcept { return static_cast<std::uint8_t>(value);  }
    [[nodiscard]] constexpr std::uint8_t  operator""_bps(unsigned long long value) noexcept { return static_cast<std::uint8_t>(value);  }

    [[nodiscard]] constexpr std::uint32_t operator""_MB(unsigned long long value) noexcept {
        return static_cast<std::uint32_t>(value * 1024ULL * 1024ULL);
    }

    // -------------------------------------------------------------------------
    // Concepts
    // -------------------------------------------------------------------------
    template <typename T>
    concept SampleRate = std::same_as<T, std::uint32_t>;

    template <typename T>
    concept ChannelCount = std::same_as<T, std::uint8_t>;

    template <typename T>
    concept BytesPerSample = std::same_as<T, std::uint8_t>;

    template <typename T>
    concept AudioDuration = std::same_as<T, std::chrono::milliseconds>;

    // -------------------------------------------------------------------------
    // Valores de referencia — tabla de configuraciones comunes
    // -------------------------------------------------------------------------
    namespace sample_rate {
        static constexpr std::uint32_t hz_8k  =  8000_hz;
        static constexpr std::uint32_t hz_16k = 16000_hz;
        static constexpr std::uint32_t hz_48k = 48000_hz;
    }

    namespace duration {
        static constexpr std::chrono::milliseconds ms_10  {  10 };
        static constexpr std::chrono::milliseconds ms_20  {  20 };
        static constexpr std::chrono::milliseconds ms_30  {  30 };
        static constexpr std::chrono::milliseconds ms_40  {  40 };
        static constexpr std::chrono::milliseconds ms_100 { 100 };
    }

    namespace channels {
        static constexpr std::uint8_t mono   = 1_ch;
        static constexpr std::uint8_t stereo = 2_ch;
    }

    namespace bytes_per_sample {
        static constexpr std::uint8_t int16   = 2_bps;
        static constexpr std::uint8_t float32 = 4_bps;
    }

    // -------------------------------------------------------------------------
    // Funciones de cálculo — compile-time, sin estado global
    // -------------------------------------------------------------------------

    template <SampleRate SampleRateT, AudioDuration AudioDurationT>
    [[nodiscard]] constexpr std::uint32_t
    samples_per_buffer(SampleRateT sample_rate, AudioDurationT frame_duration) noexcept {
        return sample_rate * static_cast<std::uint32_t>(frame_duration.count()) / 1000U;
    }

    // FrameDurationMsV es uint32_t porque std::chrono::milliseconds no es structural
    // type en libstdc++13 — AudioDuration no aplica como NTTP constraint.
    template <SampleRate    auto SampleRateV,
              std::uint32_t      FrameDurationMsV,
              ChannelCount  auto ChannelCountV,
              BytesPerSample auto BytesPerSampleV>
    [[nodiscard]] constexpr std::uint32_t bytes_per_buffer() noexcept
    {
        static_assert(SampleRateV      > 0, "pre: sample_rate must be > 0");
        static_assert(FrameDurationMsV > 0, "pre: frame_duration_ms must be > 0");
        static_assert(ChannelCountV    > 0, "pre: channel_count must be >= 1");
        static_assert(BytesPerSampleV  > 0, "pre: bytes_per_sample must be >= 1");

        return samples_per_buffer(SampleRateV, std::chrono::milliseconds{ FrameDurationMsV })
               * ChannelCountV
               * BytesPerSampleV;
    }

    template <SampleRate    auto SampleRateV,
              std::uint32_t      FrameDurationMsV,
              ChannelCount  auto ChannelCountV,
              BytesPerSample auto BytesPerSampleV,
              SampleRate    auto NumChunksV>
    [[nodiscard]] constexpr std::uint32_t compute_audio_chunk_size_in_bytes() noexcept
    {
        static_assert(NumChunksV > 0, "pre: num_chunks must be >= 1");

        return bytes_per_buffer<SampleRateV, FrameDurationMsV, ChannelCountV, BytesPerSampleV>()
               * NumChunksV;
    }

} // namespace asr::utils

#endif // ASR_UTILS_AUDIO_BUFFER_TRAITS_H
