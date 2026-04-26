#ifndef ASR_PROCESSOR_SHERPA_ONNX_RECOGNIZER_CONFIG_BUILDER_H
#define ASR_PROCESSOR_SHERPA_ONNX_RECOGNIZER_CONFIG_BUILDER_H

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include <sherpa-onnx/c-api/cxx-api.h>

#include "ASRProcessor/sherpa_onnx_config.h"

namespace asr::asr_processor {

template <typename ConfigT = DefaultSherpaOnnxConfig>
struct SherpaOnnxRecognizerConfigBuilder {
    [[nodiscard]] static sherpa_onnx::cxx::OnlineRecognizerConfig build() {
        sherpa_onnx::cxx::OnlineRecognizerConfig config{};
        configure_features(config);
        configure_model(config);
        configure_decoder(config);
        validate_files(config);
        return config;
    }

private:
    static void configure_features(sherpa_onnx::cxx::OnlineRecognizerConfig& config) {
        config.feat_config.sample_rate = ConfigT::kSampleRate;
        config.feat_config.feature_dim = ConfigT::kFeatureDim;
    }

    static void configure_model(sherpa_onnx::cxx::OnlineRecognizerConfig& config) {
        auto& model = config.model_config;
        model.transducer.encoder = model_path(ConfigT::kEncoderModel);
        model.transducer.decoder = model_path(ConfigT::kDecoderModel);
        model.transducer.joiner = model_path(ConfigT::kJoinerModel);
        model.tokens = model_path(ConfigT::kTokens);
        model.provider = std::string{ ConfigT::kProvider };
        model.num_threads = ConfigT::kNumThreads;
    }

    static void configure_decoder(sherpa_onnx::cxx::OnlineRecognizerConfig& config) {
        config.decoding_method = std::string{ ConfigT::kDecodingMethod };
        config.max_active_paths = ConfigT::kMaxActivePaths;
        config.hotwords_score = ConfigT::kHotwordsScore;
        config.blank_penalty = ConfigT::kBlankPenalty;
        if constexpr (!ConfigT::kHotwords.empty()) {
            config.hotwords_file = model_path(ConfigT::kHotwords);
        }
        config.enable_endpoint = false;
    }

    static void validate_files(const sherpa_onnx::cxx::OnlineRecognizerConfig& config) {
        require_file("encoder", config.model_config.transducer.encoder);
        require_file("decoder", config.model_config.transducer.decoder);
        require_file("joiner", config.model_config.transducer.joiner);
        require_file("tokens", config.model_config.tokens);
        if (!config.hotwords_file.empty()) {
            require_file("hotwords", config.hotwords_file);
        }
    }

    [[nodiscard]] static std::string model_path(std::string_view filename) {
        auto project_dir = std::filesystem::canonical("/proc/self/exe").parent_path().parent_path();
        return (project_dir / ConfigT::kModelDir / filename).string();
    }

    static void require_file(std::string_view name, const std::string& path) {
        if (std::filesystem::exists(path)) return;
        throw std::runtime_error{ "sherpa-onnx missing " + std::string{ name } + ": " + path };
    }
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_SHERPA_ONNX_RECOGNIZER_CONFIG_BUILDER_H
