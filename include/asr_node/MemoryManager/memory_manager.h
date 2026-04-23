#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory_resource>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

#include "audio_sample_handle.h"
#include "raw_audio_type.h"

namespace memory {

template <std::size_t kInputBufferSize = 4096>
struct MemoryManager {
    static constexpr std::size_t kMemoryPoolSizeBytes = kInputBufferSize;

    using AudioSampleType = AudioSample;
    using AudioBufferType = std::pmr::vector<AudioSampleType>;
    using AudioSampleHandleType = AudioSampleHandle;

    MemoryManager() = default;
    MemoryManager(const MemoryManager&)            = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;
    MemoryManager(MemoryManager&&)                 = delete;
    MemoryManager& operator=(MemoryManager&&)      = delete;

    [[nodiscard]] std::pmr::memory_resource* resource() noexcept { return &memory_pool_; }
    [[nodiscard]] const std::pmr::memory_resource* resource() const noexcept { return &memory_pool_; }

    void reserve_audio_samples(std::size_t sample_count) {
        std::lock_guard lock{ audio_mutex_ };
        audio_memory_.reserve(sample_count);
    }

    [[nodiscard]] bool try_push_audio_sample(const AudioSampleType& sample) {
        std::lock_guard lock{ audio_mutex_ };
        if (audio_memory_.capacity() == 0U) return false;

        write_audio_sample_unlocked(sample);
        return true;
    }

    template <typename InputIt>
    [[nodiscard]] std::size_t push_raw_audio_samples(InputIt first, InputIt last) {
        std::lock_guard lock{ audio_mutex_ };
        if (audio_memory_.capacity() == 0U) return 0U;

        const auto requested_samples = static_cast<std::size_t>(std::distance(first, last));

        std::for_each(first, last, [this](auto raw_sample) {
            AudioSampleType sample{};
            sample.type_ = ::asr::utils::AudioType::kRawInputAudio;
            sample.raw_audio_ = static_cast<float>(raw_sample);
            write_audio_sample_unlocked(sample);
        });

        return requested_samples;
    }

    void clear_audio_samples() noexcept {
        std::lock_guard lock{ audio_mutex_ };
        audio_memory_.clear();
        valid_sample_count_ = 0U;
        write_index_ = 0U;
        next_sample_id_ = 1U;
    }

    [[nodiscard]] bool raw_audio_window_available(std::uint32_t start_sample_id,
                                                  std::size_t sample_count) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return raw_audio_window_available_unlocked(start_sample_id, sample_count);
    }

    [[nodiscard]] bool copy_raw_audio_window(std::uint32_t start_sample_id,
                                             std::span<float> output,
                                             std::span<AudioSampleHandleType> handles) {
        std::lock_guard lock{ audio_mutex_ };

        if (output.size() != handles.size()) return false;
        if (!raw_audio_window_available_unlocked(start_sample_id, output.size())) return false;

        for (std::size_t i = 0; i < output.size(); ++i) {
            auto* sample = sample_ptr_for_id_unlocked(start_sample_id + static_cast<std::uint32_t>(i));
            if (sample == nullptr) return false;

            output[i] = sample->raw_audio_;
            handles[i] = AudioSampleHandleType{
                .ptr = sample,
                .slot_index = slot_index_for_id_unlocked(sample->sample_id_),
                .sample_id = sample->sample_id_,
            };
        }

        return true;
    }

    [[nodiscard]] bool mark_sample(const AudioSampleHandleType& handle,
                                   ::asr::utils::AudioType type) noexcept {
        std::lock_guard lock{ audio_mutex_ };
        if (!is_valid_unlocked(handle)) return false;

        handle.ptr->type_ = type;
        return true;
    }

    [[nodiscard]] bool read_sample(const AudioSampleHandleType& handle,
                                   AudioSampleType& output) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        if (!is_valid_unlocked(handle)) return false;

        output = *handle.ptr;
        return true;
    }

    [[nodiscard]] std::optional<AudioSampleHandleType>
    next_handle(const AudioSampleHandleType& handle) noexcept {
        std::lock_guard lock{ audio_mutex_ };
        if (!is_valid_unlocked(handle)) return std::nullopt;

        const auto next_id = handle.sample_id + 1U;
        auto* sample = sample_ptr_for_id_unlocked(next_id);
        if (sample == nullptr) return std::nullopt;

        return AudioSampleHandleType{
            .ptr = sample,
            .slot_index = slot_index_for_id_unlocked(next_id),
            .sample_id = next_id,
        };
    }

    [[nodiscard]] std::optional<AudioSampleHandleType>
    sample_handle(std::uint32_t sample_id) noexcept {
        std::lock_guard lock{ audio_mutex_ };
        auto* sample = sample_ptr_for_id_unlocked(sample_id);
        if (sample == nullptr) return std::nullopt;

        return AudioSampleHandleType{
            .ptr = sample,
            .slot_index = slot_index_for_id_unlocked(sample_id),
            .sample_id = sample_id,
        };
    }

    [[nodiscard]] bool is_valid(const AudioSampleHandleType& handle) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return is_valid_unlocked(handle);
    }

    [[nodiscard]] bool oldest_sample_id(std::uint32_t& output) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        if (valid_sample_count_ == 0U) return false;

        output = oldest_sample_id_unlocked();
        return true;
    }

    [[nodiscard]] bool newest_sample_id(std::uint32_t& output) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        if (valid_sample_count_ == 0U) return false;

        output = newest_sample_id_unlocked();
        return true;
    }

    [[nodiscard]] bool sample_id_aged(std::uint32_t sample_id) const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return valid_sample_count_ > 0U && sample_id < oldest_sample_id_unlocked();
    }

    [[nodiscard]] std::size_t audio_sample_capacity() const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return audio_memory_.capacity();
    }

    [[nodiscard]] std::size_t audio_sample_size() const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return valid_sample_count_;
    }

    [[nodiscard]] constexpr std::size_t memory_pool_size_bytes() const noexcept {
        return kMemoryPoolSizeBytes;
    }

    [[nodiscard]] std::size_t audio_memory_used_bytes() const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return audio_memory_used_bytes_unlocked();
    }

    [[nodiscard]] std::size_t audio_memory_capacity_bytes() const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return audio_memory_capacity_bytes_unlocked();
    }

    [[nodiscard]] std::size_t audio_memory_available_bytes() const noexcept {
        std::lock_guard lock{ audio_mutex_ };
        return audio_memory_capacity_bytes_unlocked() - audio_memory_used_bytes_unlocked();
    }

private:
    alignas(std::max_align_t) std::array<std::byte, kInputBufferSize> general_buffer_{};

    std::pmr::monotonic_buffer_resource memory_pool_{
        general_buffer_.data(),
        general_buffer_.size(),
        std::pmr::null_memory_resource()
    };

    AudioBufferType audio_memory_{&memory_pool_};
    std::size_t valid_sample_count_{ 0U };
    std::size_t write_index_{ 0U };
    std::uint32_t next_sample_id_{ 1U };

    void write_audio_sample_unlocked(AudioSampleType sample) {
        sample.sample_id_ = next_sample_id_++;

        if (audio_memory_.size() < audio_memory_.capacity()) {
            audio_memory_.push_back(sample);
            write_index_ = audio_memory_.size() % audio_memory_.capacity();
            valid_sample_count_ = audio_memory_.size();
            return;
        }

        audio_memory_[write_index_] = sample;
        write_index_ = (write_index_ + 1U) % audio_memory_.capacity();
        valid_sample_count_ = audio_memory_.capacity();
    }

    [[nodiscard]] bool raw_audio_window_available_unlocked(std::uint32_t start_sample_id,
                                                           std::size_t sample_count) const noexcept {
        if (sample_count == 0U) return true;
        if (valid_sample_count_ == 0U) return false;

        const auto oldest_id = oldest_sample_id_unlocked();
        const auto newest_id = newest_sample_id_unlocked();
        const auto end_sample_id = start_sample_id + static_cast<std::uint32_t>(sample_count - 1U);

        return start_sample_id >= oldest_id && end_sample_id <= newest_id;
    }

    [[nodiscard]] std::size_t audio_memory_used_bytes_unlocked() const noexcept {
        return valid_sample_count_ * sizeof(AudioSampleType);
    }

    [[nodiscard]] std::size_t audio_memory_capacity_bytes_unlocked() const noexcept {
        return audio_memory_.capacity() * sizeof(AudioSampleType);
    }

    [[nodiscard]] std::uint32_t oldest_sample_id_unlocked() const noexcept {
        return next_sample_id_ - static_cast<std::uint32_t>(valid_sample_count_);
    }

    [[nodiscard]] std::uint32_t newest_sample_id_unlocked() const noexcept {
        return next_sample_id_ - 1U;
    }

    [[nodiscard]] std::size_t oldest_slot_index_unlocked() const noexcept {
        if (audio_memory_.size() < audio_memory_.capacity()) return 0U;
        return write_index_;
    }

    [[nodiscard]] std::size_t slot_index_for_id_unlocked(std::uint32_t sample_id) const noexcept {
        const auto distance_from_oldest = sample_id - oldest_sample_id_unlocked();
        return (oldest_slot_index_unlocked() + distance_from_oldest) % audio_memory_.size();
    }

    [[nodiscard]] AudioSampleType* sample_ptr_for_id_unlocked(std::uint32_t sample_id) noexcept {
        if (!raw_audio_window_available_unlocked(sample_id, 1U)) return nullptr;

        auto& sample = audio_memory_[slot_index_for_id_unlocked(sample_id)];
        if (sample.sample_id_ != sample_id) return nullptr;

        return &sample;
    }

    [[nodiscard]] const AudioSampleType* sample_ptr_for_id_unlocked(std::uint32_t sample_id) const noexcept {
        if (!raw_audio_window_available_unlocked(sample_id, 1U)) return nullptr;

        const auto& sample = audio_memory_[slot_index_for_id_unlocked(sample_id)];
        if (sample.sample_id_ != sample_id) return nullptr;

        return &sample;
    }

    [[nodiscard]] bool is_valid_unlocked(const AudioSampleHandleType& handle) const noexcept {
        if (handle.ptr == nullptr) return false;
        if (handle.slot_index >= audio_memory_.size()) return false;

        const auto& sample = audio_memory_[handle.slot_index];
        return &sample == handle.ptr && sample.sample_id_ == handle.sample_id;
    }

    mutable std::mutex audio_mutex_;
};

} // namespace memory

#endif // MEMORY_MANAGER_H
