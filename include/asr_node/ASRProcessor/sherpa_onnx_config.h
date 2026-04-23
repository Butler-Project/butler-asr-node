#ifndef ASR_PROCESSOR_SHERPA_ONNX_CONFIG_H
#define ASR_PROCESSOR_SHERPA_ONNX_CONFIG_H

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace asr::asr_processor {

struct DefaultSherpaOnnxConfig {
    static constexpr std::string_view kModelDir{
        "models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26"
    };
    static constexpr std::string_view kEncoderModel{
        "encoder-epoch-99-avg-1-chunk-16-left-128.onnx"
    };//full precision model
    static constexpr std::string_view kDecoderModel{
        "decoder-epoch-99-avg-1-chunk-16-left-128.onnx"
    };
    static constexpr std::string_view kJoinerModel{
        "joiner-epoch-99-avg-1-chunk-16-left-128.onnx"
    };//full presicion model
    static constexpr std::string_view kTokens{ "tokens.txt" };
    static constexpr std::string_view kHotwords{ "hotwords.txt" };
    static constexpr std::string_view kProvider{ "openvino" };
    // modified_beam_search is required for hotwords — greedy_search ignores them.
    static constexpr std::string_view kDecodingMethod{ "modified_beam_search" };
    static constexpr std::int32_t kMaxActivePaths = 4;
    static constexpr float kHotwordsScore = 1.5F;
    static constexpr float kBlankPenalty = 0.0F;
    static constexpr std::int32_t kSampleRate = 16000;
    static constexpr std::int32_t kFeatureDim = 80;
    static constexpr std::int32_t kNumThreads = 1;
    static constexpr std::size_t kAcceptSampleBlockSize = 320U;
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_SHERPA_ONNX_CONFIG_H
