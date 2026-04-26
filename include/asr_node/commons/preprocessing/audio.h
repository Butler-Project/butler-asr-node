#ifndef ASR_COMMONS_PREPROCESSING_AUDIO_H
#define ASR_COMMONS_PREPROCESSING_AUDIO_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace asr::preprocessing::audio {

struct AudioStats {
    float peak_raw{ 0.0F };
    float rms_raw{ 0.0F };
    float dc_offset{ 0.0F };
    float peak_processed{ 0.0F };
    float rms_processed{ 0.0F };
    float gain_applied{ 1.0F };
};

template <typename SampleT>
[[nodiscard]] AudioStats compute_raw_stats(const SampleT* samples,
                                           std::size_t count) noexcept
{
    AudioStats stats{};
    if (count == 0U) return stats;

    float sum    = 0.0F;
    float sum_sq = 0.0F;
    for (std::size_t i = 0U; i < count; ++i) {
        const float value = static_cast<float>(samples[i]);
        sum += value;
        sum_sq += value * value;
        stats.peak_raw = std::max(stats.peak_raw, std::abs(value));
    }

    stats.dc_offset = sum / static_cast<float>(count);
    stats.rms_raw = std::sqrt(sum_sq / static_cast<float>(count));
    return stats;
}

[[nodiscard]] inline float target_peak_gain(float abs_peak,
                                            float target_peak_amplitude) noexcept
{
    if (target_peak_amplitude <= 0.0F) return 1.0F;
    if (abs_peak <= 1e-6F) return 1.0F;
    return target_peak_amplitude / abs_peak;
}

inline void apply_gain(std::vector<float>& samples, float gain) {
    if (gain == 1.0F) return;
    for (auto& sample : samples) sample *= gain;
}

inline void update_processed_stats(std::vector<float>& samples,
                                   AudioStats& stats) noexcept
{
    float sum_sq = 0.0F;
    float peak   = 0.0F;
    for (const float sample : samples) {
        sum_sq += sample * sample;
        peak = std::max(peak, std::abs(sample));
    }

    stats.rms_processed = samples.empty()
        ? 0.0F
        : std::sqrt(sum_sq / static_cast<float>(samples.size()));
    stats.peak_processed = peak;
}

template <typename ConfigT, typename SampleT>
[[nodiscard]] std::vector<float> preprocess(const SampleT* samples,
                                            std::size_t count,
                                            AudioStats& stats)
{
    std::vector<float> output(count);

    const float dc_offset = ConfigT::kRemoveDcOffset ? stats.dc_offset : 0.0F;
    for (std::size_t i = 0U; i < count; ++i) {
        output[i] = static_cast<float>(samples[i]) - dc_offset;
    }

    if constexpr (ConfigT::kTargetPeakAmplitude > 0.0F) {
        float abs_peak = 0.0F;
        for (const float sample : output) {
            abs_peak = std::max(abs_peak, std::abs(sample));
        }

        stats.gain_applied = target_peak_gain(abs_peak, ConfigT::kTargetPeakAmplitude);
        apply_gain(output, stats.gain_applied);
    } else {
        stats.gain_applied = 1.0F;
    }

    update_processed_stats(output, stats);
    return output;
}

// Variant that applies a caller-managed gain (e.g. EMA-smoothed) instead of
// computing it per-chunk from the peak.  DC offset removal still uses ConfigT.
template <typename ConfigT, typename SampleT>
[[nodiscard]] std::vector<float> preprocess_with_gain(const SampleT* samples,
                                                       std::size_t count,
                                                       AudioStats& stats,
                                                       float gain)
{
    std::vector<float> output(count);

    const float dc_offset = ConfigT::kRemoveDcOffset ? stats.dc_offset : 0.0F;
    for (std::size_t i = 0U; i < count; ++i) {
        output[i] = static_cast<float>(samples[i]) - dc_offset;
    }

    stats.gain_applied = gain;
    apply_gain(output, gain);
    update_processed_stats(output, stats);
    return output;
}

} // namespace asr::preprocessing::audio

#endif // ASR_COMMONS_PREPROCESSING_AUDIO_H
