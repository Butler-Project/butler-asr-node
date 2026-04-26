#ifndef ASR_RAW_AUDIO_CAPTURE_CONFIG_H
#define ASR_RAW_AUDIO_CAPTURE_CONFIG_H

#include <chrono>
#include <cstddef>
#include <cstdint>

#include <RtAudio.h>

#include "commons/audio_buffer_traits.h"

namespace asr {

struct DefaultRawAudioCaptureConfig {
    using InputSampleType = float;

    static constexpr std::uint32_t kSampleRate = asr::utils::sample_rate::hz_16k;
    static constexpr std::uint32_t kFrameDurationMs = 20U;
    static constexpr std::uint8_t kChannels = asr::utils::channels::mono;
    static constexpr std::uint8_t kBytesPerInputSample =
        static_cast<std::uint8_t>(sizeof(InputSampleType));
    static constexpr std::uint32_t kFramesPerBuffer =
        asr::utils::samples_per_buffer(kSampleRate, std::chrono::milliseconds{ kFrameDurationMs });
    static constexpr std::uint32_t kInternalBuffers = 4U;
    static constexpr std::uint32_t kBufferedAudioSeconds = 10U;
    static constexpr std::uint32_t kBufferedChunkCount =
        kBufferedAudioSeconds * 1000U / kFrameDurationMs;
    static constexpr std::uint32_t kBufferedInputAudioBytes =
        asr::utils::compute_audio_chunk_size_in_bytes<kSampleRate,
                                                      kFrameDurationMs,
                                                      kChannels,
                                                      kBytesPerInputSample,
                                                      kBufferedChunkCount>();
    static constexpr std::size_t kStoredSampleCapacity =
        kBufferedInputAudioBytes / kBytesPerInputSample;
    // Log frequency controls (0 = disable that log category at compile-time).
    static constexpr std::uint32_t kDebugCaptureLogEveryCallbacks    = 50U;
    static constexpr std::uint32_t kMaxDebugOverflowLogs             = 8U;
    static constexpr std::uint32_t kDebugPreprocessLogEveryCallbacks = 50U;

    // Preprocessing: remove DC offset before storing samples.
    static constexpr bool  kRemoveDcOffset       = true;
    // Normalize amplitude to this target peak level (0 = disabled).
    static constexpr float kTargetPeakAmplitude  = 0.3F;

    // Smoothed gain (EMA): alpha controls response speed.
    // Higher = faster response to volume changes; lower = smoother.
    // 0.08 ≈ time constant of ~12 chunks (240 ms at 20 ms/chunk).
    static constexpr float kGainSmoothingAlpha   = 0.08F;
    // Hard ceiling on gain to prevent extreme noise amplification in silence.
    static constexpr float kMaxGain              = 40.0F;
    // Noise gate: skip gain application if raw peak is below this threshold.
    // Keeps silence truly silent instead of amplifying the noise floor.
    static constexpr float kNoiseGateThreshold   = 0.005F;

    static constexpr RtAudioFormat kRtAudioFormat = RTAUDIO_FLOAT32;

    template <typename StoredSampleT>
    [[nodiscard]] static constexpr std::size_t memory_pool_size_bytes(
        double multiplier = 1.0) noexcept
    {
        const auto base_memory_pool_size = kStoredSampleCapacity * sizeof(StoredSampleT);
        return static_cast<std::size_t>(
            static_cast<double>(base_memory_pool_size) * multiplier);
    }
};

} // namespace asr

#endif // ASR_RAW_AUDIO_CAPTURE_CONFIG_H
