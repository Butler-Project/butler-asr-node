#ifndef ASR_RAW_AUDIO_CAPTURE_H
#define ASR_RAW_AUDIO_CAPTURE_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

#include <RtAudio.h>

#include "MemoryManager/memory_manager.h"
#include "RawInputAudioProcessor/raw_audio_capture_config.h"
#include "commons/human_readable_bytes.h"
#include "commons/logging.h"
#include "commons/preprocessing/audio.h"
#include "commons/utils.h"

namespace asr {

template <typename MemMgrT, typename ConfigT = DefaultRawAudioCaptureConfig>
struct RawAudioCapture {
    using input_sample_type = typename ConfigT::InputSampleType;

    static_assert(std::is_same_v<input_sample_type, float>,
                  "Default RtAudio capture is configured as RTAUDIO_FLOAT32.");
    static_assert(ConfigT::kRtAudioFormat == RTAUDIO_FLOAT32,
                  "InputSampleType and kRtAudioFormat must describe the same sample format.");

    using AudioStats = asr::preprocessing::audio::AudioStats;

    explicit RawAudioCapture(MemMgrT& memory_manager)
        : memory_manager_{ memory_manager }
    {
        memory_manager_.reserve_audio_samples(ConfigT::kStoredSampleCapacity);

        asr::logging::log(asr::logging::tags::kRawAudioCapture)
            << "reserved audio samples: " << memory_manager_.audio_sample_capacity()
            << " memory_used="     << asr::utils::human_readable_bytes(memory_manager_.audio_memory_used_bytes())
            << " memory_capacity=" << asr::utils::human_readable_bytes(memory_manager_.audio_memory_capacity_bytes())
            << " memory_pool="     << asr::utils::human_readable_bytes(memory_manager_.memory_pool_size_bytes());
    }

    ~RawAudioCapture() { stop(); }

    RawAudioCapture(const RawAudioCapture&)            = delete;
    RawAudioCapture& operator=(const RawAudioCapture&) = delete;
    RawAudioCapture(RawAudioCapture&&)                 = delete;
    RawAudioCapture& operator=(RawAudioCapture&&)      = delete;

    void start() {
        if (rt_audio_.isStreamRunning()) return;
        if (rt_audio_.isStreamOpen()) {
            rt_audio_.startStream();
            return;
        }

        if (rt_audio_.getDeviceCount() == 0) {
            throw std::runtime_error{ "No audio input devices available." };
        }

        const auto device_id  = rt_audio_.getDefaultInputDevice();
        auto device_info      = rt_audio_.getDeviceInfo(device_id);

        if (!device_info.probed) {
            throw std::runtime_error{ "Default audio input device could not be probed." };
        }
        if (device_info.inputChannels < ConfigT::kChannels) {
            throw std::runtime_error{
                "Default audio input device does not expose enough input channels." };
        }

        asr::logging::log(asr::logging::tags::kRawAudioCapture)
            << "input device id=" << device_id
            << " name=\""         << device_info.name << "\""
            << " input_channels=" << device_info.inputChannels
            << " requested_channels=" << static_cast<std::uint32_t>(ConfigT::kChannels)
            << " sample_rate="    << ConfigT::kSampleRate
            << " frames_per_buffer=" << ConfigT::kFramesPerBuffer;

        memory_manager_.clear_audio_samples();

        RtAudio::StreamParameters input_params{};
        input_params.deviceId    = device_id;
        input_params.nChannels   = ConfigT::kChannels;
        input_params.firstChannel = 0;

        RtAudio::StreamOptions options{};
        options.numberOfBuffers = ConfigT::kInternalBuffers;

        auto buffer_frames = ConfigT::kFramesPerBuffer;

        rt_audio_.openStream(nullptr, &input_params, ConfigT::kRtAudioFormat,
                              ConfigT::kSampleRate, &buffer_frames,
                              &RawAudioCapture::audio_callback, this, &options);

        actual_frames_per_buffer_ = buffer_frames;
        captured_samples_.store(0, std::memory_order_relaxed);
        capture_callback_count_.store(0, std::memory_order_relaxed);
        dropped_samples_.store(0, std::memory_order_relaxed);
        dropped_log_events_.store(0, std::memory_order_relaxed);

        asr::logging::log(asr::logging::tags::kRawAudioCapture)
            << "stream opened"
            << " actual_frames_per_buffer=" << actual_frames_per_buffer_
            << " stored_sample_capacity="   << memory_manager_.audio_sample_capacity()
            << " memory_used="     << asr::utils::human_readable_bytes(memory_manager_.audio_memory_used_bytes())
            << " memory_capacity=" << asr::utils::human_readable_bytes(memory_manager_.audio_memory_capacity_bytes())
            << " memory_available=" << asr::utils::human_readable_bytes(memory_manager_.audio_memory_available_bytes());

        rt_audio_.startStream();

        asr::logging::log(asr::logging::tags::kRawAudioCapture) << "stream started";
    }

    void stop() noexcept {
        try {
            if (rt_audio_.isStreamRunning()) rt_audio_.stopStream();
            if (rt_audio_.isStreamOpen())    rt_audio_.closeStream();
        } catch (const RtAudioError&) {}
    }

    [[nodiscard]] std::uint64_t dropped_samples()  const noexcept {
        return dropped_samples_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t captured_samples() const noexcept {
        return captured_samples_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint32_t actual_frames_per_buffer() const noexcept {
        return actual_frames_per_buffer_;
    }

private:
    // ---- Audio callback ----
    static int audio_callback(void*               /*output_buffer*/,
                               void*               input_buffer,
                               std::uint32_t       frame_count,
                               double              /*stream_time*/,
                               RtAudioStreamStatus status,
                               void*               user_data)
    {
        if (input_buffer == nullptr || user_data == nullptr) return 0;

        auto& self       = *static_cast<RawAudioCapture*>(user_data);
        auto* samples    = static_cast<const input_sample_type*>(input_buffer);
        auto  sample_count = static_cast<std::size_t>(frame_count) * ConfigT::kChannels;

        if (status != 0) {
            self.record_dropped_samples(sample_count, "RtAudio status");
            return 0;
        }

        auto stats = asr::preprocessing::audio::compute_raw_stats(samples, sample_count);

        // Smoothed gain (EMA) with noise gate and max clamp.
        // During silence (below kNoiseGateThreshold): apply no gain and hold
        // the smoothed_gain_ state so it's ready when speech resumes.
        float gain_to_apply = 1.0F;
        if constexpr (ConfigT::kTargetPeakAmplitude > 0.0F) {
            if (stats.peak_raw >= ConfigT::kNoiseGateThreshold) {
                const float instant_gain = asr::preprocessing::audio::target_peak_gain(
                    stats.peak_raw, ConfigT::kTargetPeakAmplitude);
                self.smoothed_gain_ =
                    ConfigT::kGainSmoothingAlpha * instant_gain +
                    (1.0F - ConfigT::kGainSmoothingAlpha) * self.smoothed_gain_;
                self.smoothed_gain_ = std::min(self.smoothed_gain_, ConfigT::kMaxGain);
                gain_to_apply = self.smoothed_gain_;
            }
        }

        auto processed = asr::preprocessing::audio::preprocess_with_gain<ConfigT>(
            samples, sample_count, stats, gain_to_apply);

        const auto samples_stored =
            self.memory_manager_.push_raw_audio_samples(processed.begin(), processed.end());

        self.record_captured_samples(samples_stored, sample_count, stats);

        if (samples_stored < sample_count) {
            self.record_dropped_samples(sample_count - samples_stored, "memory buffer full");
        }
        return 0;
    }

    // ---- Logging helpers ----
    void record_captured_samples(std::size_t stored_count,
                                  std::size_t received_count,
                                  const AudioStats& stats)
    {
        const auto total_captured =
            captured_samples_.fetch_add(stored_count, std::memory_order_relaxed) + stored_count;
        const auto callback_index =
            capture_callback_count_.fetch_add(1, std::memory_order_relaxed) + 1U;

        // One-time log when buffer first enters circular (overwrite) mode.
        if (!buffer_full_logged_.load(std::memory_order_relaxed) &&
            memory_manager_.audio_memory_available_bytes() == 0U)
        {
            if (!buffer_full_logged_.exchange(true, std::memory_order_relaxed)) {
                std::uint32_t oldest_id = 0U;
                std::uint32_t newest_id = 0U;
                memory_manager_.oldest_sample_id(oldest_id);
                memory_manager_.newest_sample_id(newest_id);
                asr::logging::log(asr::logging::tags::kRawAudioCapture)
                    << "buffer_full: entering circular overwrite mode"
                    << " callback="        << callback_index
                    << " total_stored="    << total_captured
                    << " oldest_id="       << oldest_id
                    << " newest_id="       << newest_id
                    << " capacity_samples=" << memory_manager_.audio_sample_capacity();
            }
        }

        if constexpr (ConfigT::kDebugCaptureLogEveryCallbacks > 0U) {
            if (callback_index % ConfigT::kDebugCaptureLogEveryCallbacks == 0U) {
                std::uint32_t oldest_id = 0U;
                std::uint32_t newest_id = 0U;
                memory_manager_.oldest_sample_id(oldest_id);
                memory_manager_.newest_sample_id(newest_id);
                asr::logging::log(asr::logging::tags::kRawAudioCapture)
                    << "capture callback=" << callback_index
                    << " received="        << received_count
                    << " stored="          << stored_count
                    << " total_stored="    << total_captured
                    << " oldest_id="       << oldest_id
                    << " newest_id="       << newest_id
                    << " memory_used="     << asr::utils::human_readable_bytes(memory_manager_.audio_memory_used_bytes())
                    << " memory_capacity=" << asr::utils::human_readable_bytes(memory_manager_.audio_memory_capacity_bytes())
                    << " memory_available=" << asr::utils::human_readable_bytes(memory_manager_.audio_memory_available_bytes());
            }
        }

        if constexpr (ConfigT::kDebugPreprocessLogEveryCallbacks > 0U) {
            if (callback_index % ConfigT::kDebugPreprocessLogEveryCallbacks == 0U) {
                asr::logging::log(asr::logging::tags::kRawAudioPreprocess)
                    << "callback="  << callback_index
                    << " raw_peak=" << stats.peak_raw
                    << " raw_rms="  << stats.rms_raw
                    << " dc_offset=" << stats.dc_offset
                    << " gain="     << stats.gain_applied
                    << " out_peak=" << stats.peak_processed
                    << " out_rms="  << stats.rms_processed;
            }
        }
    }

    void record_dropped_samples(std::size_t dropped_count, const char* reason) {
        const auto total_dropped =
            dropped_samples_.fetch_add(dropped_count, std::memory_order_relaxed) + dropped_count;

        if constexpr (ConfigT::kMaxDebugOverflowLogs > 0U) {
            const auto log_index =
                dropped_log_events_.fetch_add(1, std::memory_order_relaxed);

            if (log_index < ConfigT::kMaxDebugOverflowLogs) {
                asr::logging::log(asr::logging::tags::kRawAudioCapture)
                    << "dropped_samples="       << dropped_count
                    << " total_dropped_samples=" << total_dropped
                    << " reason=\""             << reason << "\"";

                if (log_index + 1U == ConfigT::kMaxDebugOverflowLogs) {
                    asr::logging::log(asr::logging::tags::kRawAudioCapture)
                        << "further drop logs suppressed";
                }
            }
        }
    }

    MemMgrT&                   memory_manager_;
    RtAudio                    rt_audio_;
    std::atomic<std::uint64_t> captured_samples_{ 0 };
    std::atomic<std::uint32_t> capture_callback_count_{ 0 };
    std::atomic<std::uint64_t> dropped_samples_{ 0 };
    std::atomic<std::uint32_t> dropped_log_events_{ 0 };
    std::atomic<bool>          buffer_full_logged_{ false };
    float                      smoothed_gain_{ 1.0F };
    std::uint32_t              actual_frames_per_buffer_{ ConfigT::kFramesPerBuffer };
};

} // namespace asr

#endif // ASR_RAW_AUDIO_CAPTURE_H
