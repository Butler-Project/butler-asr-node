#ifndef ASR_VAD_PROCESSOR_PROCESOR_H
#define ASR_VAD_PROCESSOR_PROCESOR_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "MemoryManager/audio_sample_handle.h"
#include "MemoryManager/memory_manager.h"
#include "VadProcessor/vad_event.h"
#include "VadProcessor/vad_observer.h"
#include "VadProcessor/vad_processor_config.h"
#include "VadProcessor/vad_result.h"
#include "commons/logging.h"
#include "commons/utils.h"

namespace asr::vad {

template <typename MemMgrT, typename ConfigT = DefaultVadProcessorConfig>
struct VadProcessor {
    explicit VadProcessor(Ort::Env& env, MemMgrT& memory_manager)
        : memory_manager_{ memory_manager },
          model_path_{ resolve_model_path() },
          session_options_{ make_session_options() },
          session_{ env, model_path_.c_str(), session_options_ }
    {
        reset();

        asr::logging::log(asr::logging::tags::kVadProcessor)
            << "initialized"
            << " model=\"" << model_path_ << "\""
            << " provider=" << ConfigT::kProvider
            << " device_type=" << ConfigT::kOpenVinoDeviceType
            << " window_samples=" << ConfigT::kWindowSamples
            << " speech_threshold=" << ConfigT::kSpeechThreshold
            << " silence_threshold=" << ConfigT::kSilenceThreshold;
    }

    VadProcessor(const VadProcessor&)            = delete;
    VadProcessor& operator=(const VadProcessor&) = delete;
    VadProcessor(VadProcessor&&)                 = delete;
    VadProcessor& operator=(VadProcessor&&)      = delete;

    void add_observer(IVadObserver& observer) {
        observers_.push_back(&observer);
    }

    void remove_observer(IVadObserver& observer) {
        observers_.erase(std::remove(observers_.begin(), observers_.end(), &observer),
                         observers_.end());
    }

    void reset() noexcept {
        input_window_.fill(0.0F);
        input_handles_.fill(memory::AudioSampleHandle{});
        h_.fill(0.0F);
        c_.fill(0.0F);
        next_sample_id_    = 0U;
        processed_windows_ = 0U;
        process_call_count_ = 0U;
        speech_run_windows_ = 0U;
        silence_run_windows_ = 0U;
        is_speech_         = false;
        last_probability_  = 0.0F;
    }

    [[nodiscard]] VadResult process_available() {
        ++process_call_count_;

        VadResult result{
            .event            = ::asr::utils::AudioType::kUnknown,
            .event_handle     = {},
            .is_speech        = is_speech_,
            .speech_probability = last_probability_,
            .next_sample_id   = next_sample_id_,
            .processed_windows = 0U,
        };

        if (!ensure_cursor_is_valid()) {
            log_process_call(0U);
            log_waiting_for_audio(0U);
            return result;
        }

        const auto available_samples = memory_manager_.audio_sample_size();
        log_process_call(available_samples);

        while (memory_manager_.copy_raw_audio_window(
                   next_sample_id_,
                   std::span<float>{ input_window_ },
                   std::span<memory::AudioSampleHandle>{ input_handles_ }))
        {
            const auto probability = run_window();
            const auto event       = update_state(probability);
            auto event_handle      = memory::AudioSampleHandle{};

            if (event == ::asr::utils::AudioType::kSpeechStart) {
                event_handle = input_handles_.front();
                emit_event(event, event_handle, probability);
            } else if (event == ::asr::utils::AudioType::kSpeechStop) {
                event_handle = input_handles_.back();
                emit_event(event, event_handle, probability);
            }

            next_sample_id_ += static_cast<std::uint32_t>(ConfigT::kWindowSamples);
            ++processed_windows_;
            ++result.processed_windows;

            result.event       = event == ::asr::utils::AudioType::kUnknown ? result.event : event;
            result.event_handle = event == ::asr::utils::AudioType::kUnknown
                                      ? result.event_handle : event_handle;
            result.is_speech        = is_speech_;
            result.speech_probability = probability;
            result.next_sample_id   = next_sample_id_;

            log_window(probability, event);
        }

        if (result.processed_windows == 0U) {
            log_waiting_for_audio(available_samples);
        } else {
            asr::logging::log(asr::logging::tags::kVadProcessor)
                << "process_available done"
                << " processed_windows=" << result.processed_windows
                << " next_sample_id="    << result.next_sample_id
                << " last_probability="  << result.speech_probability
                << " is_speech="         << result.is_speech
                << " event="             << event_name(result.event);
        }

        return result;
    }

    [[nodiscard]] bool is_speech() const noexcept   { return is_speech_; }
    [[nodiscard]] float last_probability() const noexcept { return last_probability_; }
    [[nodiscard]] std::uint32_t next_sample_id() const noexcept { return next_sample_id_; }
    [[nodiscard]] std::size_t processed_windows() const noexcept { return processed_windows_; }

private:
    [[nodiscard]] static std::string resolve_model_path() {
        namespace fs = std::filesystem;

        const std::array candidates{
            fs::path{ ConfigT::kModelPath },
            fs::path{ ".." } / ConfigT::kModelPath,
        };

        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) return candidate.string();
        }

        throw std::runtime_error{ "VAD model not found: models/vad/silero_vad.onnx" };
    }

    [[nodiscard]] static Ort::SessionOptions make_session_options() {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(ConfigT::kIntraOpThreads);

        if constexpr (ConfigT::kProvider == std::string_view{ "openvino" }) {
            const std::unordered_map<std::string, std::string> provider_options{
                { "device_type",  std::string{ ConfigT::kOpenVinoDeviceType } },
                { "load_config",  std::string{ ConfigT::kOpenVinoLoadConfig } },
            };

            options.AppendExecutionProvider_OpenVINO_V2(provider_options);
            options.SetGraphOptimizationLevel(ORT_DISABLE_ALL);
        }

        return options;
    }

    [[nodiscard]] bool ensure_cursor_is_valid() {
        std::uint32_t oldest_id = 0U;
        if (!memory_manager_.oldest_sample_id(oldest_id)) return false;

        if (next_sample_id_ == 0U) {
            next_sample_id_ = oldest_id;
            return true;
        }

        if (memory_manager_.sample_id_aged(next_sample_id_)) {
            asr::logging::log(asr::logging::tags::kVadProcessor)
                << "cursor aged"
                << " next_sample_id=" << next_sample_id_
                << " oldest_sample_id=" << oldest_id;
            next_sample_id_ = oldest_id;
        }

        return true;
    }

    [[nodiscard]] float run_window() {
        std::vector<Ort::Value> input_tensors{};
        input_tensors.reserve(ConfigT::kInputNames.size());

        input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
            memory_info_, input_window_.data(), input_window_.size(),
            ConfigT::kInputShape.data(), ConfigT::kInputShape.size()));
        input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
            memory_info_, h_.data(), h_.size(),
            ConfigT::kStateShape.data(), ConfigT::kStateShape.size()));
        input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
            memory_info_, c_.data(), c_.size(),
            ConfigT::kStateShape.data(), ConfigT::kStateShape.size()));

        auto output_tensors = session_.Run(
            Ort::RunOptions{ nullptr },
            ConfigT::kInputNames.data(),  input_tensors.data(), input_tensors.size(),
            ConfigT::kOutputNames.data(), ConfigT::kOutputNames.size());

        const auto probability = output_tensors[0].template GetTensorMutableData<float>()[0];
        auto* new_h = output_tensors[1].template GetTensorMutableData<float>();
        auto* new_c = output_tensors[2].template GetTensorMutableData<float>();

        std::copy(new_h, new_h + h_.size(), h_.begin());
        std::copy(new_c, new_c + c_.size(), c_.begin());

        last_probability_ = probability;
        return probability;
    }

    [[nodiscard]] ::asr::utils::AudioType update_state(float probability) noexcept {
        if (!is_speech_) {
            if (probability >= ConfigT::kSpeechThreshold) ++speech_run_windows_;
            else                                          speech_run_windows_ = 0U;

            if (speech_run_windows_ >= ConfigT::kSpeechStartWindows) {
                is_speech_           = true;
                speech_run_windows_  = 0U;
                silence_run_windows_ = 0U;
                return ::asr::utils::AudioType::kSpeechStart;
            }
            return ::asr::utils::AudioType::kUnknown;
        }

        if (probability <= ConfigT::kSilenceThreshold) ++silence_run_windows_;
        else                                           silence_run_windows_ = 0U;

        if (silence_run_windows_ >= ConfigT::kSpeechStopWindows) {
            is_speech_           = false;
            silence_run_windows_ = 0U;
            speech_run_windows_  = 0U;
            return ::asr::utils::AudioType::kSpeechStop;
        }
        return ::asr::utils::AudioType::kUnknown;
    }

    void emit_event(::asr::utils::AudioType event_type,
                    const memory::AudioSampleHandle& handle,
                    float probability)
    {
        const auto marked = memory_manager_.mark_sample(handle, event_type);

        asr::logging::log(asr::logging::tags::kVadProcessor)
            << "event"
            << " type="       << event_name(event_type)
            << " sample_id="  << handle.sample_id
            << " slot_index=" << handle.slot_index
            << " probability=" << probability
            << " marked="     << marked;

        if (!marked) return;

        const auto event = VadEvent{
            .type              = event_type,
            .handle            = handle,
            .speech_probability = probability,
            .processed_windows = processed_windows_,
        };

        for (auto* observer : observers_) {
            if (observer != nullptr) observer->on_vad_event(event);
        }
    }

    void log_process_call(std::size_t available_samples) const {
        if constexpr (ConfigT::kDebugLogEveryProcessCalls > 0U) {
            if (process_call_count_ % ConfigT::kDebugLogEveryProcessCalls == 0U) {
                std::uint32_t oldest_id = 0U;
                std::uint32_t newest_id = 0U;
                const auto has_oldest = memory_manager_.oldest_sample_id(oldest_id);
                const auto has_newest = memory_manager_.newest_sample_id(newest_id);

                asr::logging::log(asr::logging::tags::kVadProcessor)
                    << "process_available call=" << process_call_count_
                    << " available_samples="     << available_samples
                    << " next_sample_id="        << next_sample_id_
                    << " oldest_sample_id="      << (has_oldest ? oldest_id : 0U)
                    << " newest_sample_id="      << (has_newest ? newest_id : 0U)
                    << " window_samples="        << ConfigT::kWindowSamples;
            }
        }
    }

    void log_waiting_for_audio(std::size_t available_samples) const {
        asr::logging::log(asr::logging::tags::kVadProcessor)
            << "waiting_for_audio"
            << " available_samples=" << available_samples
            << " next_sample_id="    << next_sample_id_
            << " window_samples="    << ConfigT::kWindowSamples;
    }

    void log_window(float probability, ::asr::utils::AudioType event) const {
        if constexpr (ConfigT::kSpeechLogEveryWindows > 0U) {
            if (is_speech_ && processed_windows_ % ConfigT::kSpeechLogEveryWindows == 0U) {
                asr::logging::log(asr::logging::tags::kVadWindow)
                    << "SPEECH"
                    << " window="      << processed_windows_
                    << " next_sample_id=" << next_sample_id_
                    << " probability=" << probability
                    << " event="       << event_name(event);
            }
        }
        if constexpr (ConfigT::kDebugLogEveryWindows > 0U) {
            if (processed_windows_ % ConfigT::kDebugLogEveryWindows == 0U ||
                event != ::asr::utils::AudioType::kUnknown)
            {
                asr::logging::log(asr::logging::tags::kVadWindow)
                    << "window="          << processed_windows_
                    << " next_sample_id=" << next_sample_id_
                    << " speech_probability=" << probability
                    << " is_speech="      << is_speech_
                    << " event="          << event_name(event);
            }
        }
    }

    [[nodiscard]] static constexpr const char* event_name(
        ::asr::utils::AudioType event) noexcept
    {
        switch (event) {
            case ::asr::utils::AudioType::kSpeechStart:   return "speech_start";
            case ::asr::utils::AudioType::kSpeechStop:    return "speech_stop";
            case ::asr::utils::AudioType::kUnknown:       return "none";
            case ::asr::utils::AudioType::kRawInputAudio: return "raw_input_audio";
        }
        return "unknown";
    }

    MemMgrT& memory_manager_;
    std::vector<IVadObserver*> observers_{};
    std::string model_path_;
    Ort::SessionOptions session_options_;
    Ort::Session session_;
    Ort::MemoryInfo memory_info_{
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault) };
    std::array<float, ConfigT::kWindowSamples>                    input_window_{};
    std::array<memory::AudioSampleHandle, ConfigT::kWindowSamples> input_handles_{};
    std::array<float, ConfigT::kStateElements> h_{};
    std::array<float, ConfigT::kStateElements> c_{};
    std::uint32_t next_sample_id_{ 0U };
    std::size_t   processed_windows_{ 0U };
    std::size_t   process_call_count_{ 0U };
    std::uint32_t speech_run_windows_{ 0U };
    std::uint32_t silence_run_windows_{ 0U };
    bool  is_speech_{ false };
    float last_probability_{ 0.0F };
};

} // namespace asr::vad

#endif // ASR_VAD_PROCESSOR_PROCESOR_H
