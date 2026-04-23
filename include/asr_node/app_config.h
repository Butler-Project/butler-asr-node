#ifndef ASR_APP_CONFIG_H
#define ASR_APP_CONFIG_H

#include <cstddef>
#include <cstdint>

#include "RawInputAudioProcessor/raw_audio_capture_config.h"

namespace asr::app::config {

using AppRawAudioCaptureBaseConfig = asr::DefaultRawAudioCaptureConfig;

struct AppRawAudioCaptureConfig : AppRawAudioCaptureBaseConfig {
    static constexpr std::uint32_t kBufferedAudioSeconds = 20U;
    static_assert(kBufferedAudioSeconds > 0U, "buffered audio seconds must be greater than 0");

    static constexpr std::uint32_t kBufferedChunkCount =
        kBufferedAudioSeconds * 1000U / AppRawAudioCaptureBaseConfig::kFrameDurationMs;
    static constexpr std::uint32_t kBufferedInputAudioBytes =
        asr::utils::compute_audio_chunk_size_in_bytes<
            AppRawAudioCaptureBaseConfig::kSampleRate,
            AppRawAudioCaptureBaseConfig::kFrameDurationMs,
            AppRawAudioCaptureBaseConfig::kChannels,
            AppRawAudioCaptureBaseConfig::kBytesPerInputSample,
            kBufferedChunkCount>();
    static constexpr std::size_t kStoredSampleCapacity =
        kBufferedInputAudioBytes / AppRawAudioCaptureBaseConfig::kBytesPerInputSample;

    template <typename StoredSampleT>
    [[nodiscard]] static constexpr std::size_t memory_pool_size_bytes(
        double multiplier = 1.0) noexcept
    {
        const auto base_memory_pool_size = kStoredSampleCapacity * sizeof(StoredSampleT);
        return static_cast<std::size_t>(
            static_cast<double>(base_memory_pool_size) * multiplier);
    }
};

} // namespace asr::app::config

#endif // ASR_APP_CONFIG_H
