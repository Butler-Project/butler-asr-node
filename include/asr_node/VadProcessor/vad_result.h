#ifndef ASR_VAD_PROCESSOR_VAD_RESULT_H
#define ASR_VAD_PROCESSOR_VAD_RESULT_H

#include <cstddef>
#include <cstdint>

#include "MemoryManager/audio_sample_handle.h"
#include "commons/utils.h"

namespace asr::vad {

struct VadResult {
    ::asr::utils::AudioType event{ ::asr::utils::AudioType::kUnknown };
    memory::AudioSampleHandle event_handle{};
    bool is_speech{ false };
    float speech_probability{ 0.0F };
    std::uint32_t next_sample_id{ 0U };
    std::size_t processed_windows{ 0U };
};

} // namespace asr::vad

#endif // ASR_VAD_PROCESSOR_VAD_RESULT_H
