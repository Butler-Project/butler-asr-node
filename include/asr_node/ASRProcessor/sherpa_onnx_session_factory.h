#ifndef ASR_PROCESSOR_SHERPA_ONNX_SESSION_FACTORY_H
#define ASR_PROCESSOR_SHERPA_ONNX_SESSION_FACTORY_H

#include <chrono>
#include <utility>

#include <sherpa-onnx/c-api/cxx-api.h>

#include "ASRProcessor/sherpa_onnx_config.h"
#include "ASRProcessor/sherpa_onnx_recognizer_config_builder.h"
#include "ASRProcessor/sherpa_onnx_session.h"
#include "commons/logging.h"

namespace asr::asr_processor {

template <typename ConfigT = DefaultSherpaOnnxConfig>
struct SherpaOnnxSessionFactory {
    [[nodiscard]] static SherpaOnnxSession create() {
        const auto load_started = std::chrono::steady_clock::now();

        asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
            << "loading started"
            << " model_dir=\"" << ConfigT::kModelDir << "\""
            << " provider="    << ConfigT::kProvider
            << " decoding_method=" << ConfigT::kDecodingMethod
            << " max_active_paths=" << ConfigT::kMaxActivePaths
            << " sample_rate=" << ConfigT::kSampleRate;

        auto config = SherpaOnnxRecognizerConfigBuilder<ConfigT>::build();
        auto recognizer = sherpa_onnx::cxx::OnlineRecognizer::Create(config);

        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - load_started);

        asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
            << "loading completed"
            << " elapsed_ms="  << elapsed_ms.count()
            << " model_dir=\"" << ConfigT::kModelDir << "\""
            << " provider="    << ConfigT::kProvider
            << " decoding_method=" << ConfigT::kDecodingMethod
            << " max_active_paths=" << ConfigT::kMaxActivePaths;

        return SherpaOnnxSession{ std::move(recognizer), ConfigT::kSampleRate };
    }
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_SHERPA_ONNX_SESSION_FACTORY_H
