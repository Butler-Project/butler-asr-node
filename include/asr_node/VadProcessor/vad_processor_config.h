#ifndef ASR_VAD_PROCESSOR_CONFIG_H
#define ASR_VAD_PROCESSOR_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace asr::vad {

struct DefaultVadProcessorConfig {
    static constexpr std::string_view kModelPath{ "models/vad/silero_vad.onnx" };
    static constexpr std::size_t kWindowSamples = 512U;
    static constexpr std::size_t kStateElements = 2U * 1U * 64U;
    static constexpr float kSpeechThreshold = 0.50F;
    static constexpr float kSilenceThreshold = 0.38F;//previously 0.35
    static constexpr std::uint32_t kSpeechStartWindows = 1U;//increase the windows could add false positives
    static constexpr std::uint32_t kSpeechStopWindows = 20U;//~640 ms
    static constexpr std::int32_t kIntraOpThreads = 1;
    static constexpr std::string_view kProvider{ "openvino" };
    static constexpr std::string_view kOpenVinoDeviceType{ "CPU" };
    static constexpr std::string_view kOpenVinoLoadConfig{
        R"({"CPU":{"PERFORMANCE_HINT":"LATENCY","NUM_STREAMS":"1"}})"
    };
    // Log frequency controls (0 = disable that log category entirely at compile-time).
    static constexpr std::uint32_t kDebugLogEveryProcessCalls = 1U;
    static constexpr std::uint32_t kDebugLogEveryWindows      = 10U;
    static constexpr std::uint32_t kSpeechLogEveryWindows     = 1U;

    static constexpr std::array<const char*, 3> kInputNames{ "x", "h", "c" };
    static constexpr std::array<const char*, 3> kOutputNames{ "prob", "new_h", "new_c" };
    static constexpr std::array<std::int64_t, 2> kInputShape{ 1, static_cast<std::int64_t>(kWindowSamples) };
    static constexpr std::array<std::int64_t, 3> kStateShape{ 2, 1, 64 };
};

} // namespace asr::vad

#endif // ASR_VAD_PROCESSOR_CONFIG_H
