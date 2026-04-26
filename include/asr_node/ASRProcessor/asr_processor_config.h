#ifndef ASR_PROCESSOR_CONFIG_H
#define ASR_PROCESSOR_CONFIG_H

#include <chrono>

namespace asr::asr_processor {

struct DefaultASRProcessorConfig {
    static constexpr auto kActivePollInterval = std::chrono::milliseconds{ 20 };
    static constexpr std::uint32_t kPreRollMs = 500U;
    // Log frequency: log consumed sample every N samples (0 = disabled).
    static constexpr std::uint32_t kDebugLogConsumedEvery = 1600U;
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_CONFIG_H
