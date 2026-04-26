// Integration test: feeds a WAV file through the full VAD+ASR pipeline
// without using RtAudio or a microphone.
//
// Pipeline:  WAV file -> MemoryManager -> VadProcessor -> ASRProcessor
//
// Run from the build/ directory:
//   ./pipeline_wav_test
//
// Pass: exits 0.  Fail: exits 1 and prints which test failed.

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <onnxruntime_cxx_api.h>

#include "ASRProcessor/asr_processor_config.h"
#include "ASRProcessor/processor.h"
#include "ASRProcessor/sherpa_onnx_config.h"
#include "MemoryManager/memory_manager.h"
#include "VadProcessor/procesor.h"
#include "VadProcessor/vad_processor_config.h"
#include "commons/logging.h"
#include "commons/preprocessing/audio.h"
#include "test/wav_loader.h"

// ---------------------------------------------------------------------------
// Path resolution (mirrors SherpaOnnxRecognizerConfigBuilder::model_path)
// ---------------------------------------------------------------------------

// Returns the project root = directory two levels above the running executable.
// Assumes binary lives at <project>/build/<name>.
static std::filesystem::path project_root() {
    return std::filesystem::canonical("/proc/self/exe").parent_path().parent_path();
}

// ---------------------------------------------------------------------------
// Test-specific configs (no RtAudio dependency)
// ---------------------------------------------------------------------------

// Minimal config for preprocess_with_gain<ConfigT> — only kRemoveDcOffset is used.
struct TestPreprocessConfig {
    static constexpr bool kRemoveDcOffset = true;
};

// VAD: match the best-run settings; silence logging to reduce noise.
struct TestVadConfig : asr::vad::DefaultVadProcessorConfig {
    static constexpr std::uint32_t kSpeechStopWindows           = 12U;
    static constexpr std::uint32_t kDebugLogEveryProcessCalls   = 0U;
    static constexpr std::uint32_t kDebugLogEveryWindows        = 0U;
    static constexpr std::uint32_t kSpeechLogEveryWindows       = 0U;
};

// ASR: use greedy_search + no hotwords so results are deterministic for the
// test WAVs (hotwords are tuned for the benchmark speech, not LibriSpeech).
struct TestSherpaConfig : asr::asr_processor::DefaultSherpaOnnxConfig {
    static constexpr std::string_view kDecodingMethod{ "greedy_search" };
    static constexpr std::string_view kHotwords{ "" }; // disable hotword file
};

// ---------------------------------------------------------------------------
// Memory pool — sized for 30 s of audio at 16 kHz
// ---------------------------------------------------------------------------

static constexpr std::size_t kTestStoredSamples = 30U * 16000U;
static constexpr std::size_t kTestPoolBytes     = kTestStoredSamples * sizeof(memory::AudioSample);
using TestMemMgr = memory::MemoryManager<kTestPoolBytes>;


// ---------------------------------------------------------------------------
// Helpers: text normalization and WER
// ---------------------------------------------------------------------------

static std::string normalize(std::string s) {
    for (auto& c : s) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        else if (c < 'A' || c > 'Z') c = ' ';
    }
    // Collapse multiple spaces and strip leading/trailing.
    std::string out;
    out.reserve(s.size());
    bool in_space = true;
    for (char c : s) {
        if (c == ' ') {
            if (!in_space) { out += ' '; in_space = true; }
        } else {
            out += c;
            in_space = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> words;
    std::string w;
    for (char c : s) {
        if (c == ' ') { if (!w.empty()) { words.push_back(w); w.clear(); } }
        else          { w += c; }
    }
    if (!w.empty()) words.push_back(w);
    return words;
}

// Returns word error rate = edit_distance(ref, hyp) / ref.size().
static float wer(const std::string& ref_text, const std::string& hyp_text) {
    const auto ref = split(ref_text);
    const auto hyp = split(hyp_text);
    const auto n = ref.size();
    const auto m = hyp.size();
    if (n == 0) return 0.0F;

    std::vector<std::vector<std::size_t>> dp(n + 1, std::vector<std::size_t>(m + 1));
    for (std::size_t i = 0; i <= n; ++i) dp[i][0] = i;
    for (std::size_t j = 0; j <= m; ++j) dp[0][j] = j;
    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            dp[i][j] = ref[i-1] == hyp[j-1]
                ? dp[i-1][j-1]
                : 1U + std::min({ dp[i-1][j-1], dp[i-1][j], dp[i][j-1] });
        }
    }
    return static_cast<float>(dp[n][m]) / static_cast<float>(n);
}

// ---------------------------------------------------------------------------
// EMA gain — mirrors the logic in RawAudioCapture::audio_callback
// ---------------------------------------------------------------------------

struct EmaGain {
    static constexpr float kTarget    = 0.3F;
    static constexpr float kAlpha     = 0.08F;
    static constexpr float kMaxGain   = 40.0F;
    static constexpr float kNoiseGate = 0.005F;

    float smoothed{ 1.0F };

    float next(float peak_raw) {
        if (peak_raw < kNoiseGate) return 1.0F;
        const float instant = asr::preprocessing::audio::target_peak_gain(peak_raw, kTarget);
        smoothed = kAlpha * instant + (1.0F - kAlpha) * smoothed;
        smoothed = std::min(smoothed, kMaxGain);
        return smoothed;
    }
};

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

struct TestCase {
    std::string wav_path;
    std::string expected; // uppercase, no punctuation
    float       wer_threshold{ 0.30F };
};

static bool run_test(const TestCase& tc, Ort::Env& env, TestMemMgr& mm) {
    std::cout << "\n[TEST] " << tc.wav_path << "\n";

    const auto wav_path = (project_root() / tc.wav_path).string();
    auto wav = test::load_wav_pcm16(wav_path);
    std::cout << "[TEST] loaded: " << wav.samples.size() << " samples  "
              << wav.sample_rate << " Hz  "
              << (static_cast<float>(wav.samples.size()) / static_cast<float>(wav.sample_rate))
              << " s\n";

    if (wav.sample_rate != 16000U)
        throw std::runtime_error{ "WAV must be 16 kHz: " + tc.wav_path };
    if (wav.channels != 1U)
        throw std::runtime_error{ "WAV must be mono: " + tc.wav_path };

    // Reset shared memory manager.
    mm.clear_audio_samples();
    mm.reserve_audio_samples(kTestStoredSamples);

    // Collect ASR results via callback (called from ASR worker thread).
    // Also signals a condition variable so the test can wait for completion
    // instead of calling stop() while ASR is still processing.
    std::vector<std::string> segments;
    std::mutex               segments_mutex;
    std::size_t              completed_segs{ 0 };
    std::condition_variable  completion_cv;
    std::mutex               completion_mutex;

    auto result_cb = [&](std::string_view text) {
        const auto s = normalize(std::string{ text });
        {
            std::lock_guard lock{ segments_mutex };
            if (!s.empty()) segments.emplace_back(s);
        }
        {
            std::lock_guard lock{ completion_mutex };
            ++completed_segs;
        }
        completion_cv.notify_one();
    };

    // Build pipeline.
    asr::vad::VadProcessor<TestMemMgr, TestVadConfig> vad{ env, mm };

    asr::asr_processor::ASRProcessor<
        TestMemMgr,
        asr::asr_processor::DefaultASRProcessorConfig,
        TestSherpaConfig
    > asr_proc{ mm, result_cb };

    vad.add_observer(asr_proc);

    // Feed WAV in 20 ms chunks (320 samples) — same block size as live capture.
    static constexpr std::size_t kChunk = 320U;
    EmaGain gain_state;

    std::cout << "[TEST] pushing audio...\n";
    for (std::size_t off = 0; off < wav.samples.size(); off += kChunk) {
        const auto count = std::min(kChunk, wav.samples.size() - off);
        const float* ptr = wav.samples.data() + off;

        auto stats = asr::preprocessing::audio::compute_raw_stats(ptr, count);
        const float gain = gain_state.next(stats.peak_raw);

        auto processed = asr::preprocessing::audio::preprocess_with_gain<TestPreprocessConfig>(
            ptr, count, stats, gain);

        mm.push_raw_audio_samples(processed.begin(), processed.end());
        vad.process_available();
    }

    // Drain: keep calling VAD until no new windows can be processed.
    std::cout << "[TEST] draining VAD...\n";
    {
        asr::vad::VadResult r;
        do { r = vad.process_available(); } while (r.processed_windows > 0);
    }

    // The test pushes audio much faster than real-time, so when VAD finishes,
    // the ASR worker may still be far behind — it needs time to decode all
    // the accumulated samples.  If VAD already detected the end of speech
    // (is_speech() == false), wait for ASR to finish the segment naturally
    // before calling stop().  If speech never ended (very long recording),
    // stop() will force-flush the current segment.
    if (!vad.is_speech()) {
        std::cout << "[TEST] waiting for ASR to finish segment...\n";
        std::unique_lock lock{ completion_mutex };
        const bool done = completion_cv.wait_for(
            lock, std::chrono::seconds{ 180 },
            [&]{ return completed_segs > 0; });
        if (!done)
            std::cout << "[TEST] WARNING: timed out waiting for ASR segment\n";
    }

    // Stop ASR: flushes any remaining partial segment and joins the worker.
    std::cout << "[TEST] stopping ASR processor...\n";
    asr_proc.stop();

    // Build full transcript from all segments.
    std::string hyp;
    {
        std::lock_guard lock{ segments_mutex };
        for (const auto& seg : segments) {
            if (!hyp.empty()) hyp += ' ';
            hyp += seg;
        }
    }
    hyp = normalize(hyp);
    const auto ref = normalize(tc.expected);

    const float w = wer(ref, hyp);

    std::cout << "[TEST] segments : " << segments.size() << "\n";
    std::cout << "[TEST] expected : " << ref << "\n";
    std::cout << "[TEST] got      : " << (hyp.empty() ? "(empty)" : hyp) << "\n";
    std::cout << "[TEST] WER      : " << (w * 100.0F) << "% (threshold "
              << (tc.wer_threshold * 100.0F) << "%)\n";

    const bool passed = w <= tc.wer_threshold;
    std::cout << "[TEST] " << (passed ? "PASS" : "FAIL") << "\n";
    return passed;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::cout << std::unitbuf;

    // Suppress verbose per-window VAD logs; keep segment-level ASR logs.
    asr::logging::disable(asr::logging::tags::kVadWindow);
    asr::logging::disable(asr::logging::tags::kVadProcessor);
    asr::logging::disable(asr::logging::tags::kRawAudioCapture);
    asr::logging::disable(asr::logging::tags::kRawAudioPreprocess);

    Ort::Env env{ ORT_LOGGING_LEVEL_WARNING, "pipeline_wav_test" };

    // Static so the 5.76 MB pool goes in BSS, not on the stack.
    // Same pattern as main.cpp's production MemoryManager.
    static TestMemMgr mm;

    const std::vector<TestCase> tests{
        {
            "models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26/test_wavs/0.wav",
            "AFTER EARLY NIGHTFALL THE YELLOW LAMPS WOULD LIGHT UP HERE AND THERE "
            "THE SQUALID QUARTER OF THE BROTHELS",
            0.30F,
        },
        {
            "models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26/test_wavs/1.wav",
            "GOD AS A DIRECT CONSEQUENCE OF THE SIN WHICH MAN THUS PUNISHED HAD GIVEN HER "
            "A LOVELY CHILD WHOSE PLACE WAS ON THAT SAME DISHONOURED BOSOM TO CONNECT HER "
            "PARENT FOR EVER WITH THE RACE AND DESCENT OF MORTALS AND TO BE FINALLY A "
            "BLESSED SOUL IN HEAVEN",
            0.30F,
        },
    };

    int failed = 0;
    for (const auto& tc : tests) {
        try {
            if (!run_test(tc, env, mm)) ++failed;
        } catch (const std::exception& e) {
            std::cerr << "[TEST] ERROR: " << e.what() << "\n";
            ++failed;
        }
    }

    std::cout << "\n[TEST] Results: "
              << (static_cast<int>(tests.size()) - failed) << "/"
              << tests.size() << " passed\n";

    return failed == 0 ? 0 : 1;
}
