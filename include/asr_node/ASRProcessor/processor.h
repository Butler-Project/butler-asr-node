#ifndef ASR_PROCESSOR_PROCESSOR_H
#define ASR_PROCESSOR_PROCESSOR_H

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <thread>

#include "ASRProcessor/asr_processor_config.h"
#include "ASRProcessor/sherpa_onnx_config.h"
#include "ASRProcessor/sherpa_onnx_session_factory.h"
#include "MemoryManager/memory_manager.h"
#include "VadProcessor/vad_event.h"
#include "VadProcessor/vad_observer.h"
#include "commons/asr_utils.h"
#include "commons/logging.h"

namespace asr::asr_processor {

template <typename MemMgrT,
          typename ConfigT       = DefaultASRProcessorConfig,
          typename SherpaConfigT = DefaultSherpaOnnxConfig>
struct ASRProcessor final : public ::asr::vad::IVadObserver {
    ASRProcessor() = delete;

    explicit ASRProcessor(MemMgrT& memory_manager,
                          std::function<void(std::string_view)> result_cb = {})
        : memory_manager_{ memory_manager },
          result_cb_{ std::move(result_cb) },
          sherpa_session_{ SherpaOnnxSessionFactory<SherpaConfigT>::create() }
    {
        std::lock_guard lock{ mutex_ };
        stop_requested_ = false;
        worker_ = std::thread{ [this] { worker_loop(); } };

        asr::logging::log(asr::logging::tags::kASRProcessor) << "worker started";
    }

    ~ASRProcessor() override { stop(); }

    ASRProcessor(const ASRProcessor&)            = delete;
    ASRProcessor& operator=(const ASRProcessor&) = delete;
    ASRProcessor(ASRProcessor&&)                 = delete;
    ASRProcessor& operator=(ASRProcessor&&)      = delete;

    void stop() {
        const auto was_running = worker_.joinable();

        {
            std::lock_guard lock{ mutex_ };
            stop_requested_ = true;
        }
        cv_.notify_one();

        if (was_running) worker_.join();

        if (was_running) {
            asr::logging::log(asr::logging::tags::kASRProcessor) << "worker stopped";
        }
    }

    void on_vad_event(const ::asr::vad::VadEvent& event) override {
        if (::asr::utils::is_speech_start(event.type)) {
            register_speech_start(event);
            log_received_vad_event(event);
            notify_asr_thread();
            return;
        }

        if (event.type == ::asr::utils::AudioType::kSpeechStop) {
            register_speech_stop(event);
            log_received_vad_event(event);
            notify_asr_thread();
        }
    }

private:
    void worker_loop() {
        for (;;) {
            auto start_handle = wait_for_speech_start();
            if (!start_handle.has_value()) break;
            process_speech_segment(*start_handle);
        }
    }

    [[nodiscard]] std::optional<memory::AudioSampleHandle> wait_for_speech_start() {
        std::unique_lock lock{ mutex_ };
        cv_.wait(lock, [this] {
            return stop_requested_ || pending_speech_start_.has_value();
        });
        if (stop_requested_) return std::nullopt;
        return take_pending_speech_start();
    }

    [[nodiscard]] std::optional<memory::AudioSampleHandle> take_pending_speech_start() {
        auto handle = pending_speech_start_;
        pending_speech_start_.reset();
        return handle;
    }

    void process_speech_segment(const memory::AudioSampleHandle& start_handle) {
        const auto pre_roll_handle = pre_roll_start_handle(start_handle);
        start_segment(pre_roll_handle, start_handle);
        sherpa_session_.start_stream();
        reset_asr_sample_block();

        while (active_segment_) {
            if (should_stop_worker()) {
                asr::logging::log(asr::logging::tags::kASRProcessor)
                    << "finishing active segment because worker stop was requested";
                finish_segment();
                break;
            }

            if (!consume_current_sample() && active_segment_) wait_for_more_audio();
        }
    }

    void start_segment(const memory::AudioSampleHandle& handle,
                       const memory::AudioSampleHandle& vad_start_handle) {
        clear_pending_speech_stop();

        active_segment_          = true;
        read_handle_             = handle;
        next_sample_id_to_consume_ = handle.sample_id;
        active_segment_samples_  = 0U;

        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "SPEECH_START"
            << " sample_id="  << read_handle_.sample_id
            << " slot_index=" << read_handle_.slot_index
            << " vad_start_sample_id=" << vad_start_handle.sample_id
            << " pre_roll_samples=" << (vad_start_handle.sample_id - read_handle_.sample_id);
    }

    [[nodiscard]] memory::AudioSampleHandle
    pre_roll_start_handle(const memory::AudioSampleHandle& vad_start_handle) {
        const auto pre_roll_samples = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(SherpaConfigT::kSampleRate) * ConfigT::kPreRollMs) / 1000U);
        if (pre_roll_samples == 0U) return vad_start_handle;

        std::uint32_t oldest_id = 0U;
        if (!memory_manager_.oldest_sample_id(oldest_id)) return vad_start_handle;

        const auto requested_start_id = vad_start_handle.sample_id > pre_roll_samples
            ? vad_start_handle.sample_id - pre_roll_samples
            : 1U;
        const auto min_start_id = last_segment_stop_sample_id_ > 0U &&
                                  last_segment_stop_sample_id_ < vad_start_handle.sample_id
            ? std::max(oldest_id, last_segment_stop_sample_id_ + 1U)
            : oldest_id;
        const auto start_id = std::max(requested_start_id, min_start_id);

        auto handle = memory_manager_.sample_handle(start_id);
        if (handle.has_value()) return *handle;

        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "pre_roll_unavailable"
            << " requested_start_sample_id=" << requested_start_id
            << " fallback_sample_id=" << vad_start_handle.sample_id;
        return vad_start_handle;
    }

    [[nodiscard]] bool consume_current_sample() {
        if (finish_if_vad_stop_reached(next_sample_id_to_consume_)) return false;

        typename MemMgrT::AudioSampleType sample{};
        if (!load_next_sample(sample)) return false;

        if (::asr::utils::is_speech_stop(sample)) {
            clear_pending_speech_stop();
            log_consumed_sample(sample, "speech_stop");
            log_read_speech_stop_marker(sample);
            finish_segment();
            return false;
        }

        if (finish_if_vad_stop_reached(sample.sample_id_)) return false;

        const bool is_start = (sample.type_ == ::asr::utils::AudioType::kSpeechStart);
        if (is_start) {
            log_consumed_sample(sample, "speech_start");
        } else if constexpr (ConfigT::kDebugLogConsumedEvery > 0U) {
            if (active_segment_samples_ % ConfigT::kDebugLogConsumedEvery == 0U) {
                log_consumed_sample(sample, "raw_audio");
                log_buffer_lag(sample.sample_id_);
            }
        }

        append_asr_sample(sample.raw_audio_);
        ++next_sample_id_to_consume_;
        return true;
    }

    [[nodiscard]] bool load_next_sample(typename MemMgrT::AudioSampleType& sample) {
        auto handle = memory_manager_.sample_handle(next_sample_id_to_consume_);
        if (!handle.has_value()) {
            log_waiting_for_sample();
            return false;
        }
        read_handle_ = *handle;
        return read_current_sample(sample);
    }

    [[nodiscard]] bool read_current_sample(typename MemMgrT::AudioSampleType& sample) {
        if (memory_manager_.read_sample(read_handle_, sample)) return true;
        log_waiting_for_sample();
        return false;
    }

    void wait_for_more_audio() {
        std::unique_lock lock{ mutex_ };
        cv_.wait_for(lock, ConfigT::kActivePollInterval, [this] {
            return stop_requested_ || pending_speech_stop_.has_value();
        });
    }

    void finish_segment() {
        active_segment_ = false;
        clear_pending_speech_stop();
        flush_asr_sample_block();
        const auto text = sherpa_session_.finish_stream();

        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "text=\"" << text << "\""
            << " samples="         << active_segment_samples_
            << " start_sample_id=" << (read_handle_.sample_id -
                                       static_cast<std::uint32_t>(active_segment_samples_))
            << " stop_sample_id="  << read_handle_.sample_id;

        if (result_cb_) result_cb_(text);

        // Use next_sample_id_to_consume_ (the stop marker's position) rather than
        // read_handle_.sample_id (the last consumed sample before the marker).
        // This ensures the next segment's pre-roll starts past the stop marker
        // and avoids consuming it immediately as the first sample of the new segment.
        last_segment_stop_sample_id_ = next_sample_id_to_consume_;
        active_segment_samples_ = 0U;
    }

    void append_asr_sample(float sample) {
        asr_sample_block_[asr_sample_block_size_++] = sample;
        ++active_segment_samples_;
        if (asr_sample_block_size_ == asr_sample_block_.size()) flush_asr_sample_block();
    }

    void flush_asr_sample_block() {
        if (asr_sample_block_size_ == 0U) return;
        sherpa_session_.accept_samples(current_asr_sample_block());
        sherpa_session_.decode_ready();
        reset_asr_sample_block();
    }

    [[nodiscard]] std::span<const float> current_asr_sample_block() const noexcept {
        return { asr_sample_block_.data(), asr_sample_block_size_ };
    }

    void reset_asr_sample_block() noexcept { asr_sample_block_size_ = 0U; }

    void register_speech_start(const ::asr::vad::VadEvent& event) {
        std::lock_guard lock{ mutex_ };
        pending_speech_start_ = event.handle;
    }

    void register_speech_stop(const ::asr::vad::VadEvent& event) {
        std::lock_guard lock{ mutex_ };
        if (!pending_speech_stop_.has_value() ||
            event.handle.sample_id < pending_speech_stop_->sample_id)
        {
            pending_speech_stop_ = event.handle;
        }
    }

    void notify_asr_thread() { cv_.notify_one(); }

    void log_received_vad_event(const ::asr::vad::VadEvent& event) const {
        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "received VAD event"
            << " type="        << ::asr::utils::event_name(event.type)
            << " sample_id="   << event.handle.sample_id
            << " slot_index="  << event.handle.slot_index
            << " probability=" << event.speech_probability;
    }

    void log_waiting_for_sample() const {
        const bool aged = memory_manager_.sample_id_aged(next_sample_id_to_consume_);
        if (aged) {
            std::uint32_t oldest_id = 0U;
            memory_manager_.oldest_sample_id(oldest_id);
            asr::logging::log(asr::logging::tags::kASRProcessor)
                << "sample_overwritten: buffer lag detected"
                << " sample_id="  << next_sample_id_to_consume_
                << " oldest_id="  << oldest_id
                << " lost_samples=" << (oldest_id - next_sample_id_to_consume_);
        } else {
            asr::logging::log(asr::logging::tags::kASRProcessor)
                << "waiting for valid sample"
                << " sample_id=" << next_sample_id_to_consume_;
        }
    }

    void log_consumed_sample(const typename MemMgrT::AudioSampleType& sample,
                              const char* label) const
    {
        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "consume"
            << " type="           << label
            << " sample_id="      << sample.sample_id_
            << " raw_audio="      << sample.raw_audio_
            << " segment_samples=" << active_segment_samples_;
    }

    void log_buffer_lag(std::uint32_t current_sample_id) const {
        std::uint32_t oldest_id = 0U;
        std::uint32_t newest_id = 0U;
        if (!memory_manager_.oldest_sample_id(oldest_id) ||
            !memory_manager_.newest_sample_id(newest_id)) return;

        const auto lag   = newest_id > current_sample_id ? newest_id - current_sample_id : 0U;
        const auto headroom = current_sample_id > oldest_id ? current_sample_id - oldest_id : 0U;
        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "buffer_lag"
            << " asr_id="    << current_sample_id
            << " oldest_id=" << oldest_id
            << " newest_id=" << newest_id
            << " lag_samples="     << lag
            << " lag_ms="          << (lag * 1000U / SherpaConfigT::kSampleRate)
            << " headroom_samples=" << headroom
            << " headroom_ms="     << (headroom * 1000U / SherpaConfigT::kSampleRate);
    }

    void log_read_speech_stop_marker(const typename MemMgrT::AudioSampleType& sample) const {
        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "*****************************"
            << " SPEECH_STOP read from MemoryManager"
            << " sample_id="       << sample.sample_id_
            << " raw_audio="       << sample.raw_audio_
            << " segment_samples=" << active_segment_samples_
            << " *****************************";
    }

    [[nodiscard]] bool finish_if_vad_stop_reached(std::uint32_t sample_id) {
        const auto stop_handle = take_reached_speech_stop(sample_id);
        if (!stop_handle.has_value()) return false;

        asr::logging::log(asr::logging::tags::kASRProcessor)
            << "*****************************"
            << " VAD SPEECH_STOP reached in ASR"
            << " stop_sample_id=" << stop_handle->sample_id
            << " current_sample_id=" << sample_id
            << " segment_samples=" << active_segment_samples_
            << " *****************************";

        finish_segment();
        return true;
    }

    [[nodiscard]] std::optional<memory::AudioSampleHandle>
    take_reached_speech_stop(std::uint32_t sample_id) {
        std::lock_guard lock{ mutex_ };
        if (!pending_speech_stop_.has_value()) return std::nullopt;
        if (sample_id < pending_speech_stop_->sample_id) return std::nullopt;

        auto stop_handle = pending_speech_stop_;
        pending_speech_stop_.reset();
        return stop_handle;
    }

    void clear_pending_speech_stop() {
        std::lock_guard lock{ mutex_ };
        pending_speech_stop_.reset();
    }

    [[nodiscard]] bool should_stop_worker() {
        std::lock_guard lock{ mutex_ };
        return stop_requested_;
    }

    MemMgrT&           memory_manager_;
    std::function<void(std::string_view)> result_cb_{};
    SherpaOnnxSession  sherpa_session_;
    std::mutex         mutex_;
    std::condition_variable cv_;
    std::optional<memory::AudioSampleHandle> pending_speech_start_{};
    std::optional<memory::AudioSampleHandle> pending_speech_stop_{};
    std::thread        worker_;
    bool               stop_requested_{ false };
    bool               active_segment_{ false };
    memory::AudioSampleHandle read_handle_{};
    std::uint32_t      next_sample_id_to_consume_{ 0U };
    std::uint32_t      last_segment_stop_sample_id_{ 0U };
    std::size_t        active_segment_samples_{ 0U };
    std::array<float, SherpaConfigT::kAcceptSampleBlockSize> asr_sample_block_{};
    std::size_t        asr_sample_block_size_{ 0U };
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_PROCESSOR_H
