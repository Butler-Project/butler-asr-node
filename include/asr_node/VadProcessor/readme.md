# VAD Settings

Current values are taken from `VadProcessor/vad_processor_config.h`.
Derived timings assume 16 kHz input audio, as currently configured in `RawInputAudioProcessor/raw_audio_capture_config.h`.

| Setting | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| `kModelPath` | `models/vad/silero_vad.onnx` | Path to the Silero VAD ONNX model. The processor tries this path and also `../models/vad/silero_vad.onnx`. | Changes the model used to compute speech probability. If the model is missing, VAD initialization fails. |
| `kWindowSamples` | `512` | Number of samples sent to the model per inference. At 16 kHz this is ~32 ms per window. | Defines VAD time resolution. It must also match the shape expected by the model. Changing it can break inference if the model does not support another size. |
| `kStateElements` | `128` | Number of elements in the recurrent states `h` and `c` (`2 * 1 * 64`). | Must match the ONNX model. If it does not match, inference can fail or produce invalid results. |
| `kSpeechThreshold` | `0.50` | Minimum probability required to count a window as speech while VAD is currently in silence state. | Lower values detect speech start more easily, with more false positives. Higher values require clearer speech and can miss soft starts. |
| `kSilenceThreshold` | `0.35` | Maximum probability required to count a window as silence while VAD is currently in speech state. | Higher values make VAD stop earlier during pauses. Lower values require clearer silence and help avoid cutting slow speakers or natural pauses. |
| `kSpeechStartWindows` | `2` | Number of consecutive windows with probability `>= kSpeechThreshold` required to emit `speech_start`. Currently ~64 ms. | Lower values start faster, with more noise risk. Higher values require sustained speech and can delay or miss short starts. |
| `kSpeechStopWindows` | `6` | Number of consecutive windows with probability `<= kSilenceThreshold` required to emit `speech_stop`. Currently ~192 ms. | Main setting for slow speech. Higher values tolerate longer pauses before stopping. Lower values stop faster, but can split phrases. |
| `kIntraOpThreads` | `1` | Number of ONNX Runtime intra-op threads used to run the VAD model. | Can affect CPU usage and inference latency. `1` keeps behavior more predictable and lightweight. More threads do not always help for a small model. |
| `kDebugLogEveryProcessCalls` | `1` | Logging frequency for calls to `process_available()`. `0` disables it. | Logging only. `1` logs every call, useful for debugging but noisy. |
| `kDebugLogEveryWindows` | `10` | General logging frequency for processed windows. `0` disables it. | Logging only. Lower values produce more detail; higher values reduce log noise. |
| `kSpeechLogEveryWindows` | `1` | Logging frequency while `is_speech=true`. `0` disables it. | Logging only during speech segments. `1` logs every speech window. |
| `kInputNames` | `{ "x", "h", "c" }` | Input names expected by the ONNX model: audio and recurrent states. | Must match the ONNX graph. Changing them breaks inference if the model does not use those names. |
| `kOutputNames` | `{ "prob", "new_h", "new_c" }` | Output names expected from the ONNX model: probability and updated states. | Must match the ONNX graph. `prob` drives the start/stop state machine. |
| `kInputShape` | `{ 1, kWindowSamples }` | Shape of the audio tensor sent to the model. Currently equivalent to `{ 1, 512 }`. | Must match both the model and `kWindowSamples`. |
| `kStateShape` | `{ 2, 1, 64 }` | Shape of the recurrent state tensors `h` and `c`. | Must match the model. Affects temporal continuity across windows. |

## Tunable Ranges

These are practical tuning ranges for the current pipeline. Model-bound settings should stay fixed unless the ONNX model is changed and revalidated.

| Setting | Current value | Practical range | Notes |
| --- | ---: | --- | --- |
| `kModelPath` | `models/vad/silero_vad.onnx` | N/A | The model must expose the same inputs, outputs, and tensor shapes unless the builder/code is updated too. |
| `kWindowSamples` | `512` | `[512, 512]` | Treat as fixed unless the model explicitly supports another window size. |
| `kStateElements` | `128` | `[128, 128]` | Model-bound. Must match `kStateShape`. |
| `kSpeechThreshold` | `0.50` | `[0.30, 0.80]` | Must stay between `0.0` and `1.0`. Lower detects speech more easily; higher reduces false starts. |
| `kSilenceThreshold` | `0.35` | `[0.10, 0.50]` | Usually keep below `kSpeechThreshold`. Lower tolerates pauses better; higher stops sooner. |
| `kSpeechStartWindows` | `2` | `[1, 5]` | At 512 samples and 16 kHz, this is ~32 ms to ~160 ms. |
| `kSpeechStopWindows` | `6` | `[3, 30]` | At 512 samples and 16 kHz, this is ~96 ms to ~960 ms. Increase for slow speech or natural pauses. |
| `kIntraOpThreads` | `1` | `[1, 8]` | More threads can help or hurt depending on CPU load and model size. |
| `kDebugLogEveryProcessCalls` | `1` | `[0, 1000]` | `0` disables this log category. Larger values reduce log volume. |
| `kDebugLogEveryWindows` | `10` | `[0, 1000]` | `0` disables this log category. |
| `kSpeechLogEveryWindows` | `1` | `[0, 1000]` | `0` disables this log category. |
| `kInputNames` | `{ "x", "h", "c" }` | N/A | Only change when using a model with different input names. |
| `kOutputNames` | `{ "prob", "new_h", "new_c" }` | N/A | Only change when using a model with different output names. |
| `kInputShape` | `{ 1, 512 }` | N/A | Must match `kWindowSamples` and the ONNX graph. |
| `kStateShape` | `{ 2, 1, 64 }` | N/A | Must match `kStateElements` and the ONNX graph. |

## Most Important Settings For `speech_stop`

| Case | Recommended adjustment |
| --- | --- |
| VAD stops too quickly when someone speaks slowly or pauses naturally | Increase `kSpeechStopWindows`, for example from `6` to `12` or `16`. |
| VAD keeps listening too long after the speaker finishes | Decrease `kSpeechStopWindows` or slightly increase `kSilenceThreshold`. |
| VAD detects silence while there is still quiet speech | Decrease `kSilenceThreshold`. |
| VAD never stops, or takes too long to stop | Increase `kSilenceThreshold` or decrease `kSpeechStopWindows`. |
