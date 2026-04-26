#ifndef ASR_COMMONS_LOGGING_H
#define ASR_COMMONS_LOGGING_H

#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace asr::logging {

// ---- Known log tags (one per subsystem) ----
namespace tags {
    static constexpr std::string_view kRawAudioCapture    = "RawAudioCapture";
    static constexpr std::string_view kRawAudioPreprocess = "RawAudioCapture/preprocess";
    static constexpr std::string_view kVadProcessor       = "VadProcessor";
    static constexpr std::string_view kVadWindow          = "VadProcessor/window";
    static constexpr std::string_view kASRProcessor       = "ASRProcessor";
    static constexpr std::string_view kSherpaOnnxSession  = "SherpaOnnxSession";
    static constexpr std::string_view kMain               = "main";
} // namespace tags

// ---- Internal detail ----
namespace detail {

inline constexpr const char* kLogFilePath =
#ifdef ASR_LOG_FILE_PATH
    ASR_LOG_FILE_PATH;
#else
    "build/logs/asr_logs.txt";
#endif

// Global mutex that serialises all writes to std::cout and the log file.
inline std::mutex& output_mutex() noexcept {
    static std::mutex m;
    return m;
}

inline std::ofstream& output_file() {
    static std::ofstream file = [] {
        const std::filesystem::path log_path{ kLogFilePath };

        if (const auto parent = log_path.parent_path(); !parent.empty()) {
            std::error_code error;
            std::filesystem::create_directories(parent, error);
        }

        return std::ofstream{ log_path, std::ios::out | std::ios::trunc };
    }();

    return file;
}

// Singleton registry: maps tag -> enabled.
// Default: any tag NOT in the map is considered enabled.
struct Registry {
    Registry() = default;
    Registry(const Registry&)            = delete;
    Registry& operator=(const Registry&) = delete;

    static Registry& instance() noexcept {
        static Registry reg;
        return reg;
    }

    [[nodiscard]] bool is_enabled(std::string_view tag) const noexcept {
        std::lock_guard lock{ mutex_ };
        const auto it = tags_.find(std::string{ tag });
        return it == tags_.end() ? global_default_ : it->second;
    }

    void set(std::string_view tag, bool enabled) {
        std::lock_guard lock{ mutex_ };
        tags_[std::string{ tag }] = enabled;
    }

    // Sets the global default and clears all per-tag overrides.
    void set_all(bool enabled) {
        std::lock_guard lock{ mutex_ };
        global_default_ = enabled;
        tags_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, bool> tags_;
    bool global_default_{ true }; // default: all logs enabled
};

} // namespace detail

// ---- LogStream ----
// RAII helper returned by asr::logging::log().
// Accumulates output via operator<< and flushes a single line to
// std::cout and the configured log file (thread-safely) on destruction.
//
// Usage:
//   asr::logging::log(asr::logging::tags::kVadProcessor)
//       << "window=" << w << " prob=" << p;
//
// Produces:  [VadProcessor] window=5 prob=0.73
class LogStream {
public:
    explicit LogStream(std::string_view tag, bool enabled) noexcept
        : enabled_{ enabled }
    {
        if (enabled_) buf_ << '[' << tag << "] ";
    }

    // Move ctor: transfer ownership, prevent double-flush.
    LogStream(LogStream&& other) noexcept
        : buf_{ std::move(other.buf_) }, enabled_{ other.enabled_ }
    {
        other.enabled_ = false;
    }

    ~LogStream() {
        if (!enabled_) return;
        const auto line = buf_.str();

        std::lock_guard lock{ detail::output_mutex() };
        std::cout << line << '\n';

        auto& file = detail::output_file();
        if (file.is_open()) {
            file << line << '\n';
            file.flush();
        }
    }

    LogStream(const LogStream&)            = delete;
    LogStream& operator=(const LogStream&) = delete;
    LogStream& operator=(LogStream&&)      = delete;

    template<typename T>
    LogStream& operator<<(T&& value) {
        if (enabled_) buf_ << std::forward<T>(value);
        return *this;
    }

private:
    std::ostringstream buf_;
    bool enabled_;
};

// ---- Public static API ----

// Returns true if the given tag is currently enabled.
[[nodiscard]] inline bool is_enabled(std::string_view tag) noexcept {
    return detail::Registry::instance().is_enabled(tag);
}

[[nodiscard]] inline std::string_view log_file_path() noexcept {
    return detail::kLogFilePath;
}

// Enable a specific log tag (runtime, takes effect immediately).
inline void enable(std::string_view tag) {
    detail::Registry::instance().set(tag, true);
}

// Disable a specific log tag (runtime, takes effect immediately).
inline void disable(std::string_view tag) {
    detail::Registry::instance().set(tag, false);
}

// Enable all tags (resets per-tag overrides).
inline void enable_all() {
    detail::Registry::instance().set_all(true);
}

// Disable all tags (resets per-tag overrides).
inline void disable_all() {
    detail::Registry::instance().set_all(false);
}

// Create a LogStream for the given tag.
// If the tag is disabled the stream is a no-op.
[[nodiscard]] inline LogStream log(std::string_view tag) {
    return LogStream{ tag, detail::Registry::instance().is_enabled(tag) };
}

} // namespace asr::logging

#endif // ASR_COMMONS_LOGGING_H
