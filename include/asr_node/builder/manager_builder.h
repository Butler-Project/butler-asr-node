#ifndef ASR_NODE_BUILDER_MANAGER_BUILDER_H
#define ASR_NODE_BUILDER_MANAGER_BUILDER_H

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

#include "manager/manager.h"

namespace asr::builder {

template <typename ManagerT = ::asr::manager::Manager<>>
class ManagerBuilder {
public:
    using Config = typename ManagerT::Config;
    using FinalTranscriptionCallback = typename ManagerT::FinalTranscriptionCallback;

    ManagerBuilder& with_vad_poll_interval(std::chrono::milliseconds interval) {
        config_.vad_poll_interval = interval;
        return *this;
    }

    ManagerBuilder& with_final_transcription_callback(FinalTranscriptionCallback callback) {
        config_.final_transcription_callback = std::move(callback);
        return *this;
    }

    // Backward compatible alias.
    ManagerBuilder& with_result_callback(FinalTranscriptionCallback callback) {
        config_.final_transcription_callback = std::move(callback);
        return *this;
    }

    [[nodiscard]] Config build_config() const {
        return config_;
    }

    [[nodiscard]] ManagerT build() const {
        return ManagerT{ config_ };
    }

    [[nodiscard]] std::unique_ptr<ManagerT> build_unique() const {
        return std::make_unique<ManagerT>(config_);
    }

private:
    Config config_{};
};

} // namespace asr::builder

#endif // ASR_NODE_BUILDER_MANAGER_BUILDER_H
