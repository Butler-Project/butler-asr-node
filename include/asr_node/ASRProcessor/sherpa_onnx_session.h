#ifndef ASR_PROCESSOR_SHERPA_ONNX_SESSION_H
#define ASR_PROCESSOR_SHERPA_ONNX_SESSION_H

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include <sherpa-onnx/c-api/cxx-api.h>

#include "commons/logging.h"

namespace asr::asr_processor {

struct SherpaOnnxSession {
    SherpaOnnxSession() = delete;

    explicit SherpaOnnxSession(sherpa_onnx::cxx::OnlineRecognizer recognizer,
                               std::int32_t sample_rate)
        : recognizer_{ std::move(recognizer) }, sample_rate_{ sample_rate }
    {
        if (recognizer_.Get() == nullptr) {
            throw std::runtime_error{ "failed to create sherpa-onnx recognizer" };
        }
        asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
            << "recognizer created sample_rate=" << sample_rate_;
    }

    SherpaOnnxSession(const SherpaOnnxSession&)            = delete;
    SherpaOnnxSession& operator=(const SherpaOnnxSession&) = delete;
    SherpaOnnxSession(SherpaOnnxSession&&)                 = delete;
    SherpaOnnxSession& operator=(SherpaOnnxSession&&)      = delete;

    void start_stream() {
        stream_.emplace(recognizer_.CreateStream());
        total_samples_accepted_ = 0;
        total_decode_steps_     = 0;
        accept_waveform_calls_  = 0;
        asr::logging::log(asr::logging::tags::kSherpaOnnxSession) << "stream started";
    }

    void accept_samples(std::span<const float> samples) {
        if (samples.empty()) return;
        ensure_stream();
        stream_->AcceptWaveform(sample_rate_,
                                samples.data(),
                                static_cast<std::int32_t>(samples.size()));
        total_samples_accepted_ += static_cast<std::uint32_t>(samples.size());
        ++accept_waveform_calls_;
    }

    // Returns the number of Decode steps executed (0 = model not ready yet).
    int decode_ready() {
        ensure_stream();
        int steps = 0;
        while (recognizer_.IsReady(&*stream_)) {
            recognizer_.Decode(&*stream_);
            ++steps;
        }
        total_decode_steps_ += static_cast<std::uint32_t>(steps);

        if (steps > 0) {
            auto partial = recognizer_.GetResult(&*stream_);
            asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
                << "decode_ready"
                << " steps="              << steps
                << " total_decode_steps=" << total_decode_steps_
                << " accepted_calls="     << accept_waveform_calls_
                << " total_samples="      << total_samples_accepted_
                << " partial_text=\""     << partial.text << "\"";
        }

        return steps;
    }

    [[nodiscard]] std::string finish_stream() {
        ensure_stream();

        asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
            << "finish_stream"
            << " total_samples="       << total_samples_accepted_
            << " accept_calls="        << accept_waveform_calls_
            << " decode_steps_so_far=" << total_decode_steps_;

        stream_->InputFinished();

        int final_steps = 0;
        while (recognizer_.IsReady(&*stream_)) {
            recognizer_.Decode(&*stream_);
            ++final_steps;
        }

        auto result = recognizer_.GetResult(&*stream_);

        asr::logging::log(asr::logging::tags::kSherpaOnnxSession)
            << "finish_stream done"
            << " final_decode_steps="  << final_steps
            << " total_decode_steps="  << total_decode_steps_ + static_cast<std::uint32_t>(final_steps)
            << " text=\""              << result.text << "\"";

        stream_.reset();
        return result.text;
    }

private:
    void ensure_stream() const {
        if (stream_.has_value()) return;
        throw std::runtime_error{ "sherpa-onnx stream is not active" };
    }

    sherpa_onnx::cxx::OnlineRecognizer recognizer_;
    std::int32_t sample_rate_{ 0 };
    std::optional<sherpa_onnx::cxx::OnlineStream> stream_{};
    std::uint32_t total_samples_accepted_{ 0 };
    std::uint32_t total_decode_steps_{ 0 };
    std::uint32_t accept_waveform_calls_{ 0 };
};

} // namespace asr::asr_processor

#endif // ASR_PROCESSOR_SHERPA_ONNX_SESSION_H
