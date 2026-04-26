#ifndef MEMORY_AUDIO_SAMPLE_HANDLE_H
#define MEMORY_AUDIO_SAMPLE_HANDLE_H

#include <cstddef>
#include <cstdint>

#include "raw_audio_type.h"

namespace memory {

struct AudioSampleHandle {
    AudioSample* ptr{ nullptr };
    std::size_t slot_index{ 0U };
    std::uint32_t sample_id{ 0U };
};

} // namespace memory

#endif // MEMORY_AUDIO_SAMPLE_HANDLE_H
