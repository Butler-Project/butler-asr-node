#ifndef MEMORY_CHUNK_H
#define MEMORY_CHUNK_H
#include "commons/utils.h"
#include <cstddef>
#include <cstdint>


namespace memory {


struct AudioSample {
    ::asr::utils::AudioType type_{::asr::utils::AudioType::kUnknown};
    float raw_audio_{0.0};
    std::uint32_t sample_id_{0u};
};

}


#endif // MEMORY_CHUNK_H
