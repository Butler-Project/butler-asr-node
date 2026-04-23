# Raw Input Audio Settings

Current default values are taken from `RawInputAudioProcessor/raw_audio_capture_config.h`.
The running application uses `asr::app::config::AppRawAudioCaptureConfig` from `app_config.h`, so settings overridden there are shown in the "Active value" column.

## Capture And Buffer Settings

| Setting | Default value | Active value | Meaning | Impact |
| --- | ---: | ---: | --- | --- |
| `InputSampleType` | `float` | `float` | Raw sample type expected from RtAudio. | Must match `kRtAudioFormat`. The capture code currently asserts that input samples are `float`. |
| `kSampleRate` | `16000` | `16000` | Audio sample rate requested from the input device. | Must match VAD and ASR expectations. A mismatch can break VAD timing and degrade recognition quality. |
| `kFrameDurationMs` | `20` | `20` | Desired duration of each RtAudio callback buffer. | Lower values reduce capture latency but increase callback frequency. Higher values reduce callback overhead but add latency. |
| `kChannels` | `1` | `1` | Number of input channels requested from the audio device. | `1` means mono. Increasing this changes sample count per callback and must be handled by downstream processors. |
| `kBytesPerInputSample` | `4` | `4` | Size in bytes of `InputSampleType`. | Used to compute buffer sizes. For `float`, this is 4 bytes. |
| `kFramesPerBuffer` | `320` | `320` | Number of frames requested per callback, derived from `kSampleRate` and `kFrameDurationMs`. | At 16 kHz and 20 ms, each callback receives 320 frames. This is the capture granularity. |
| `kInternalBuffers` | `4` | `4` | Number of internal RtAudio buffers requested through `RtAudio::StreamOptions`. | More buffers can improve stability under load but may add latency. Fewer buffers can reduce latency but increase underrun/overrun risk. |
| `kBufferedAudioSeconds` | `10` | `20` | Target amount of audio history stored in `MemoryManager`. Active value is overridden in `app_config.h`. | Larger values keep more history for VAD/ASR and reduce aging risk. Smaller values use less memory but can overwrite samples sooner. |
| `kBufferedChunkCount` | `500` | `1000` | Number of 20 ms chunks stored, derived from `kBufferedAudioSeconds / kFrameDurationMs`. | Drives total stored audio capacity. Active value is 1000 chunks for 20 seconds. |
| `kBufferedInputAudioBytes` | `640000` | `1280000` | Raw input audio bytes represented by the stored audio window. | Used to derive stored sample capacity. Active value represents 20 seconds of mono float32 audio at 16 kHz. |
| `kStoredSampleCapacity` | `160000` | `320000` | Number of processed float samples reserved in `MemoryManager`. | Main audio history capacity. Active value stores 20 seconds at 16 kHz mono. |
| `memory_pool_size_bytes(multiplier)` | `kStoredSampleCapacity * sizeof(StoredSampleT)` | `320000 * sizeof(memory::AudioSample) * 1.0` | Compile-time helper used by `main.cpp` to size `MemoryManager`. The argument defaults to `1.0`. | Controls backing pool bytes. With `memory::AudioSample`, active size is about 3.66 MiB. Increasing the function multiplier only grows the backing pool unless stored capacity is also increased. |

## Preprocessing Settings

| Setting | Default value | Active value | Meaning | Impact |
| --- | ---: | ---: | --- | --- |
| `kRemoveDcOffset` | `true` | `true` | Removes the mean value from each callback buffer before storing samples. | Helps center the waveform around zero. Usually improves VAD/ASR stability when the input device has DC bias. |
| `kTargetPeakAmplitude` | `0.3` | `0.3` | Peak-normalizes each callback buffer to this target amplitude. `0` disables normalization. | Raises quiet input and limits peak level. Too high can amplify background noise; too low can make speech harder for VAD/ASR. |
| `kRtAudioFormat` | `RTAUDIO_FLOAT32` | `RTAUDIO_FLOAT32` | RtAudio sample format requested from the device. | Must match `InputSampleType`. The code asserts this relationship at compile time. |

## Logging Settings

| Setting | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| `kDebugCaptureLogEveryCallbacks` | `50` | Logs capture memory counters every N callbacks. `0` disables this log category at compile time. | At 20 ms per callback, `50` logs about once per second. Lower values produce more detailed but larger logs. |
| `kDebugPreprocessLogEveryCallbacks` | `50` | Logs preprocessing stats every N callbacks. `0` disables this log category at compile time. | Logs raw peak, raw RMS, DC offset, gain, output peak, and output RMS. Useful for diagnosing microphone level and normalization. |
| `kMaxDebugOverflowLogs` | `8` | Maximum number of dropped-sample log events before suppression. | Prevents log spam if RtAudio reports status issues or the memory buffer cannot store all samples. |

## Runtime Behavior

| Behavior | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| Input device selection | default input device | `RawAudioCapture` uses `rt_audio_.getDefaultInputDevice()`. | The selected microphone comes from the OS/default RtAudio configuration. Logs include device id and name. |
| Callback input validation | enabled | Callback exits early if `input_buffer` or `user_data` is null. | Avoids invalid memory access, but no samples are stored for that callback. |
| RtAudio status handling | enabled | Nonzero RtAudio status records dropped samples with reason `RtAudio status`. | Helps identify audio backend issues. |
| Memory full handling | enabled | If fewer samples are stored than received, dropped samples are logged with reason `memory buffer full`. | Indicates memory capacity is too small or the memory manager could not store all incoming samples. |
| Stream restart counters | reset on `start()` | Captured samples, callback count, dropped samples, and dropped-log counters reset when opening a stream. | Per-run counters start cleanly when capture starts. |

## Tunable Ranges

These are practical tuning ranges for the current pipeline. Device-bound and model-bound settings should only be changed together with the downstream VAD/ASR assumptions.

| Setting | Current active value | Practical range | Notes |
| --- | ---: | --- | --- |
| `InputSampleType` | `float` | N/A | The implementation asserts `float` with `RTAUDIO_FLOAT32`. Other sample types require code changes. |
| `kSampleRate` | `16000` | `[16000, 16000]` | Other rates may work at the device level but require matching VAD/ASR configs and models. |
| `kFrameDurationMs` | `20` | `[10, 100]` | Lower values reduce capture latency and increase callback rate. Common values are `10`, `20`, `30`, or `40`. |
| `kChannels` | `1` | `[1, 1]` | Stereo or multichannel capture requires downstream channel handling. |
| `kBytesPerInputSample` | `4` | `[4, 4]` | Do not tune directly. |
| `kFramesPerBuffer` | `320` | `[160, 1600]` | Do not tune directly; tune `kFrameDurationMs` instead. |
| `kInternalBuffers` | `4` | `[2, 8]` | Higher can improve stability; lower can reduce latency but increase audio backend risk. |
| `kBufferedAudioSeconds` | `20` | `[5, 120]` | Main history-size knob. Increase if VAD/ASR processing falls behind or samples age out too soon. |
| `kBufferedChunkCount` | `1000` | `[250, 6000]` | Do not tune directly unless overriding derived capacity intentionally. |
| `kBufferedInputAudioBytes` | `1280000` | `[320000, 7680000]` | Do not tune directly unless overriding derived capacity intentionally. |
| `kStoredSampleCapacity` | `320000` | `[80000, 1920000]` | Do not tune directly unless overriding derived capacity intentionally. |
| `memory_pool_size_bytes(multiplier)` | `1.0` multiplier in `main.cpp` | `[1.0, 4.0]` | This only changes backing pool size. To store more audio, increase `kBufferedAudioSeconds` too. |
| `kRemoveDcOffset` | `true` | N/A | Usually keep `true` unless preprocessing logs show it hurts the signal. |
| `kTargetPeakAmplitude` | `0.3` | `[0.0, 0.9]` | `0` disables normalization. Avoid high values if background noise becomes dominant. |
| `kRtAudioFormat` | `RTAUDIO_FLOAT32` | N/A | Must match `InputSampleType`. Other formats require code changes. |
| `kDebugCaptureLogEveryCallbacks` | `50` | `[0, 1000]` | `0` disables this log category. At 20 ms callbacks, `50` logs about once per second. |
| `kDebugPreprocessLogEveryCallbacks` | `50` | `[0, 1000]` | `0` disables this log category. |
| `kMaxDebugOverflowLogs` | `8` | `[0, 100]` | Higher values expose more repeated drop logs; lower values suppress sooner. |

## Most Important Settings For Tuning

| Case | Recommended adjustment |
| --- | --- |
| VAD/ASR loses old samples before processing catches up | Increase `kBufferedAudioSeconds` in `app_config.h`. |
| Need lower capture latency | Lower `kFrameDurationMs`, but verify RtAudio/device stability and downstream assumptions. |
| Logs are too noisy | Increase `kDebugCaptureLogEveryCallbacks` and `kDebugPreprocessLogEveryCallbacks`, or set them to `0`. |
| Quiet speech is not detected well | Check `kTargetPeakAmplitude` and preprocessing logs. Avoid raising it so much that background noise dominates. |
| Input looks biased or offset in logs | Keep `kRemoveDcOffset=true`; inspect `dc_offset` in `RawAudioCapture/preprocess` logs. |
