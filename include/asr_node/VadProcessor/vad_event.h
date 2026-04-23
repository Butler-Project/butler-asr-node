#ifndef ASR_VAD_PROCESSOR_VAD_EVENT_H
#define ASR_VAD_PROCESSOR_VAD_EVENT_H

#include <cstddef>

#include "MemoryManager/audio_sample_handle.h"
#include "commons/utils.h"

namespace asr::vad {

struct VadEvent {
    ::asr::utils::AudioType type{ ::asr::utils::AudioType::kUnknown };
    memory::AudioSampleHandle handle{};
    float speech_probability{ 0.0F };
    std::size_t processed_windows{ 0U };
};

} // namespace asr::vad

#endif // ASR_VAD_PROCESSOR_VAD_EVENT_H
