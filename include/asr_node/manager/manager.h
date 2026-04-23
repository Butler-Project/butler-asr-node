#ifndef ASR_NODE_MANAGER_MANAGER_H
#define ASR_NODE_MANAGER_MANAGER_H

#include <chrono>
#include <cstddef>
#include <functional>
#include <string_view>
#include <thread>
#include <utility>

#include <onnxruntime_cxx_api.h>

#include "ASRProcessor/processor.h"
#include "MemoryManager/memory_manager.h"
#include "RawInputAudioProcessor/raw_audio_capture.h"
#include "VadProcessor/procesor.h"
#include "app_config.h"
#include "commons/human_readable_bytes.h"
#include "commons/logging.h"

namespace asr::manager {

template <
    std::size_t MemoryPoolSize =
        ::asr::app::config::AppRawAudioCaptureConfig::memory_pool_size_bytes<::memory::AudioSample>(1.0),
    typename RawAudioCaptureConfigT = ::asr::app::config::AppRawAudioCaptureConfig,
    typename VadConfigT = ::asr::vad::DefaultVadProcessorConfig,
    typename ASRProcessorConfigT = ::asr::asr_processor::DefaultASRProcessorConfig,
    typename SherpaConfigT = ::asr::asr_processor::DefaultSherpaOnnxConfig>
class Manager {
public:
    using MemoryManagerT = ::memory::MemoryManager<MemoryPoolSize>;
    using RawAudioCaptureT = ::asr::RawAudioCapture<MemoryManagerT, RawAudioCaptureConfigT>;
    using VadProcessorT = ::asr::vad::VadProcessor<MemoryManagerT, VadConfigT>;
    using ASRProcessorT =
        ::asr::asr_processor::ASRProcessor<MemoryManagerT, ASRProcessorConfigT, SherpaConfigT>;
    using VadResult = ::asr::vad::VadResult;
    using FinalTranscriptionCallback = std::function<void(std::string_view)>;

    struct Config {
        std::chrono::milliseconds vad_poll_interval{ 100 };
        FinalTranscriptionCallback final_transcription_callback{};
    };

    explicit Manager(Config config = {})
        : config_{ std::move(config) },
          final_transcription_callback_{ std::move(config_.final_transcription_callback) },
          env_{ ORT_LOGGING_LEVEL_WARNING, "asr" },
          capture_{ memory_manager_ },
          vad_{ env_, memory_manager_ },
          asr_processor_{ memory_manager_, [this](std::string_view text) {
              on_final_transcription(text);
          } }
    {
        vad_.add_observer(asr_processor_);
        log_memory_reservation();
    }

    ~Manager() { stop(); }

    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

    void start() {
        if (running_) return;

        asr::logging::log(asr::logging::tags::kMain) << "Iniciando captura de microfono...";
        capture_.start();
        running_ = true;
        asr::logging::log(asr::logging::tags::kMain)
            << "Capturando audio. Presiona Ctrl+C para salir...";
    }

    void stop() noexcept {
        if (!running_) {
            asr_processor_.stop();
            return;
        }

        asr::logging::log(asr::logging::tags::kMain) << "Deteniendo captura...";
        capture_.stop();
        asr_processor_.stop();
        running_ = false;
    }

    [[nodiscard]] bool running() const noexcept { return running_; }

    [[nodiscard]] VadResult process_available() {
        const auto vad_result = vad_.process_available();

        if (vad_result.processed_windows > 0U) {
            asr::logging::log(asr::logging::tags::kMain)
                << "VAD processed_windows=" << vad_result.processed_windows
                << " next_sample_id="       << vad_result.next_sample_id
                << " probability="          << vad_result.speech_probability
                << " is_speech="            << vad_result.is_speech;
        }

        return vad_result;
    }

    [[nodiscard]] VadResult spin_once() {
        std::this_thread::sleep_for(config_.vad_poll_interval);
        return process_available();
    }

    template <typename StopPredicateT>
    void run_until(StopPredicateT should_stop) {
        start();
        while (!should_stop()) {
            (void)spin_once();
        }
        stop();
        log_summary();
    }

    void log_summary() const {
        asr::logging::log(asr::logging::tags::kMain)
            << "Muestras capturadas: " << memory_manager_.audio_sample_size();
        asr::logging::log(asr::logging::tags::kMain)
            << "Muestras capturadas por capture: " << capture_.captured_samples();
        asr::logging::log(asr::logging::tags::kMain)
            << "Muestras descartadas: " << capture_.dropped_samples();
        asr::logging::log(asr::logging::tags::kMain)
            << "VAD ventanas procesadas: " << vad_.processed_windows();
        asr::logging::log(asr::logging::tags::kMain)
            << "VAD next sample id: " << vad_.next_sample_id();
        asr::logging::log(asr::logging::tags::kMain)
            << "VAD ultima probabilidad: " << vad_.last_probability();
        asr::logging::log(asr::logging::tags::kMain)
            << "VAD speech activo: " << vad_.is_speech();
        asr::logging::log(asr::logging::tags::kMain)
            << "Memoria de audio usada: "
            << asr::utils::human_readable_bytes(memory_manager_.audio_memory_used_bytes());
        asr::logging::log(asr::logging::tags::kMain)
            << "Memoria de audio reservada: "
            << asr::utils::human_readable_bytes(memory_manager_.audio_memory_capacity_bytes());
        asr::logging::log(asr::logging::tags::kMain)
            << "Memoria de audio disponible: "
            << asr::utils::human_readable_bytes(memory_manager_.audio_memory_available_bytes());
        asr::logging::log(asr::logging::tags::kMain)
            << "Memoria total del pool: "
            << asr::utils::human_readable_bytes(memory_manager_.memory_pool_size_bytes());
    }

    [[nodiscard]] MemoryManagerT& memory_manager() noexcept { return memory_manager_; }
    [[nodiscard]] const MemoryManagerT& memory_manager() const noexcept { return memory_manager_; }

private:
    void on_final_transcription(std::string_view text) {
        if (text.empty()) return;

        asr::logging::log(asr::logging::tags::kMain)
            << "Final transcription=\"" << text << "\"";

        if (final_transcription_callback_) {
            final_transcription_callback_(text);
        }
    }

    void log_memory_reservation() const {
        asr::logging::log(asr::logging::tags::kMain)
            << "MemoryManager reservada: "
            << asr::utils::human_readable_bytes(MemoryPoolSize)
            << " buffered_seconds=" << RawAudioCaptureConfigT::kBufferedAudioSeconds
            << " stored_samples="   << RawAudioCaptureConfigT::kStoredSampleCapacity;
    }

    Config config_;
    FinalTranscriptionCallback final_transcription_callback_{};
    Ort::Env env_;
    MemoryManagerT memory_manager_{};
    RawAudioCaptureT capture_;
    VadProcessorT vad_;
    ASRProcessorT asr_processor_;
    bool running_{ false };
};

} // namespace asr::manager

#endif // ASR_NODE_MANAGER_MANAGER_H
