# ASR Settings

Current values are taken from `ASRProcessor/asr_processor_config.h` and
`ASRProcessor/sherpa_onnx_config.h`.
Derived timings assume 16 kHz input audio, as currently configured for both capture and Sherpa ONNX.

## Processor Settings

| Setting | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| `kActivePollInterval` | `20 ms` | Maximum time the ASR worker waits when it is inside an active speech segment but the next sample is not available yet. | Lower values reduce latency while waiting for new samples, but wake the worker more often. Higher values reduce wakeups but can add latency during streaming. |
| `kDebugLogConsumedEvery` | `1600` | Logs one consumed sample every N samples while processing raw audio. `0` disables this log category at compile time. At 16 kHz, `1600` samples is ~100 ms. | Logging only. Lower values produce more detail and larger logs. Higher values reduce log noise. |

## Sherpa ONNX Settings

| Setting | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| `kModelDir` | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | Directory containing the streaming Zipformer ASR model files. Paths are resolved relative to the executable directory's parent. | Selects the ASR model package. If files are missing, recognizer creation fails before audio processing starts. |
| `kEncoderModel` | `encoder-epoch-99-avg-1-chunk-16-left-128.int8.onnx` | Encoder ONNX file used by the transducer model. | Main acoustic encoder. Changing it changes recognition behavior and must remain compatible with decoder, joiner, and tokens. |
| `kDecoderModel` | `decoder-epoch-99-avg-1-chunk-16-left-128.onnx` | Decoder ONNX file used by the transducer model. | Must match the encoder, joiner, and token set. Incompatible files can fail initialization or produce invalid text. |
| `kJoinerModel` | `joiner-epoch-99-avg-1-chunk-16-left-128.int8.onnx` | Joiner ONNX file used by the transducer model. | Combines encoder and decoder outputs. Must be compatible with the rest of the model package. |
| `kTokens` | `tokens.txt` | Token vocabulary file used to map model output IDs to text tokens. | Must match the model. Wrong tokens can produce incorrect or unreadable text. |
| `kProvider` | `cpu` | Execution provider passed to Sherpa ONNX. | Controls where inference runs. `cpu` is portable and predictable. Other providers require matching runtime support. |
| `kDecodingMethod` | `greedy_search` | Decoding strategy used by Sherpa ONNX. | `greedy_search` is simple and low latency. More complex decoding can improve accuracy in some setups but may increase CPU and latency. |
| `kSampleRate` | `16000` | Sample rate passed to Sherpa ONNX when accepting waveform samples. | Must match the audio stream rate. A mismatch changes perceived speed/pitch and can badly degrade recognition. |
| `kFeatureDim` | `80` | Feature dimension configured for Sherpa ONNX frontend extraction. | Must match the model. Wrong values can fail initialization or produce bad recognition. |
| `kNumThreads` | `1` | Number of model threads used by Sherpa ONNX. | Can affect CPU usage and inference latency. `1` keeps behavior predictable. More threads may improve throughput but can also add contention. |
| `kAcceptSampleBlockSize` | `320` | Number of float samples batched before sending audio to Sherpa ONNX. At 16 kHz, `320` samples is 20 ms. | Main streaming granularity setting. Smaller blocks can reduce latency but call Sherpa more often. Larger blocks reduce call overhead but delay partial decoding. |

## Builder And Runtime Behavior

| Setting / behavior | Current value | Meaning | Impact |
| --- | ---: | --- | --- |
| `enable_endpoint` | `false` | Sherpa ONNX endpointing is disabled in `SherpaOnnxRecognizerConfigBuilder`. Segment boundaries are driven by the local VAD instead. | Final text is produced when the VAD-driven ASR segment ends and `finish_stream()` is called. Sherpa itself will not decide endpoint boundaries. |
| File validation | encoder, decoder, joiner, tokens | The builder checks that all required model files exist before creating the recognizer. | Fails fast with a clear missing-file error instead of failing later during inference. |
| Recognizer load timing log | enabled | `SherpaOnnxSessionFactory::create()` logs `loading started` and `loading completed elapsed_ms=...`. | Helps measure model startup cost. Does not affect recognition. |
| Partial decode logging | enabled when decode steps run | `SherpaOnnxSession::decode_ready()` logs `partial_text` whenever Sherpa performs at least one decode step. | Shows streaming recognition progress before final text. It can be noisy for long utterances. |
| Final decode logging | enabled | `finish_stream()` logs final decode step count and final text. | Confirms that the ASR stream was closed and final text was produced. |

## Tunable Ranges

These are practical tuning ranges for the current pipeline. Model-bound settings should stay fixed unless the Sherpa ONNX model package is changed and revalidated.

| Setting | Current value | Practical range | Notes |
| --- | ---: | --- | --- |
| `kActivePollInterval` | `20 ms` | `[5, 100]` | Lower values reduce wait latency but wake the worker more often. |
| `kDebugLogConsumedEvery` | `1600` | `[0, 16000]` | `0` disables this log category. At 16 kHz, `1600` is ~100 ms. |
| `kModelDir` | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | N/A | Encoder, decoder, joiner, and tokens must all belong to the same package. |
| `kEncoderModel` | `encoder-epoch-99-avg-1-chunk-16-left-128.int8.onnx` | N/A | Model-bound. Must match decoder, joiner, and tokens. |
| `kDecoderModel` | `decoder-epoch-99-avg-1-chunk-16-left-128.onnx` | N/A | Model-bound. Must match encoder, joiner, and tokens. |
| `kJoinerModel` | `joiner-epoch-99-avg-1-chunk-16-left-128.int8.onnx` | N/A | Model-bound. Must match encoder, decoder, and tokens. |
| `kTokens` | `tokens.txt` | N/A | Model-bound. Wrong tokens can produce incorrect text. |
| `kProvider` | `cpu` | N/A | Keep `cpu` unless CUDA/CoreML/etc. support is known to be available. |
| `kDecodingMethod` | `greedy_search` | N/A | Other decoding methods may require additional config and can increase latency. |
| `kSampleRate` | `16000` | `[16000, 16000]` | Treat as fixed unless capture, VAD, and ASR model are all changed together. |
| `kFeatureDim` | `80` | `[80, 80]` | Model-bound. Must match the model frontend. |
| `kNumThreads` | `1` | `[1, 8]` | More threads can improve throughput or add contention. Measure before increasing. |
| `kAcceptSampleBlockSize` | `320` | `[160, 1600]` | At 16 kHz this is ~10 ms to ~100 ms. Smaller blocks reduce partial-text latency; larger blocks reduce call overhead. |
| `enable_endpoint` | `false` | N/A | Current app expects VAD-driven endpointing. Enabling Sherpa endpointing should be paired with explicit endpoint handling. |

## Most Important Settings For Latency And Text Output

| Case | Recommended adjustment |
| --- | --- |
| Partial text appears too late | Lower `kAcceptSampleBlockSize` or `kActivePollInterval`. |
| CPU usage or logs are too noisy | Increase `kAcceptSampleBlockSize`, increase `kDebugLogConsumedEvery`, or set `kDebugLogConsumedEvery` to `0`. |
| Final text never appears | Check that VAD emits `speech_stop` and that ASR logs `finish_stream done`. Sherpa endpointing is disabled, so the final result depends on VAD segment closure. |
| Recognition quality is poor or words look distorted | Verify `kSampleRate` matches capture sample rate and that encoder, decoder, joiner, and tokens all belong to the same model package. |
| Model startup is slow | Check the `loading completed elapsed_ms=...` log. Provider, thread count, model size, and disk/cache state can affect this time. |
