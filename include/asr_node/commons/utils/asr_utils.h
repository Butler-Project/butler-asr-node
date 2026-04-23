#ifndef ASR_COMMONS_ASR_UTILS_H
#define ASR_COMMONS_ASR_UTILS_H

#include "commons/utils.h"

namespace asr::utils {

[[nodiscard]] constexpr bool is_speech_start(AudioType event) noexcept {
    return event == AudioType::kSpeechStart;
}

template <typename AudioSampleT>
[[nodiscard]] constexpr bool is_speech_stop(const AudioSampleT& sample) noexcept {
    return sample.type_ == AudioType::kSpeechStop;
}

[[nodiscard]] constexpr const char* event_name(AudioType event) noexcept {
    switch (event) {
        case AudioType::kSpeechStart:   return "speech_start";
        case AudioType::kSpeechStop:    return "speech_stop";
        case AudioType::kRawInputAudio: return "raw_input_audio";
        case AudioType::kUnknown:       return "none";
    }

    return "unknown";
}

} // namespace asr::utils

#endif // ASR_COMMONS_ASR_UTILS_H
