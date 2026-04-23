#ifndef ASR_VAD_PROCESSOR_VAD_OBSERVER_H
#define ASR_VAD_PROCESSOR_VAD_OBSERVER_H

#include "VadProcessor/vad_event.h"

namespace asr::vad {

struct IVadObserver {
    virtual ~IVadObserver() = default;
    virtual void on_vad_event(const VadEvent& event) = 0;
};

} // namespace asr::vad

#endif // ASR_VAD_PROCESSOR_VAD_OBSERVER_H
