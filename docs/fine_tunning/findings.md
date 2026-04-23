# ASR Fine-Tuning Findings

Last updated: 2026-04-22

This document tracks the tuning tests performed on the live microphone ASR
pipeline. The benchmark text used for comparison is the same English speech
used during the manual tests.

## Benchmark Speech

The following fixed text is read aloud for every test run (69 words):

```
HELLO GOOD MORNING THIS IS A MICROPHONE TEST FOR THE VOICE ACTIVITY
DETECTOR AND A SPEECH RECOGNITION SYSTEM I WILL SPEAK SLOWLY AT FIRST
THEN I WILL SPEAK A LITTLE FASTER THE BEGINNING AND THE END OF THE
SPEECH SHALL BE DETECTED CORRECTLY I WILL PAUSE FOR A MOMENT AFTER THE
PAUSE I AM CONTINUING THE SAME SENTENCE WITH MORE WORDS TO VERIFY THAT
EVERYTHING IS OKAY THANK YOU
```

## Evaluation Method

| Item | Method |
| --- | --- |
| Final ASR text | Extracted from `[ASRProcessor] text="..."` log lines. |
| Normalization | Uppercase text, punctuation removed, repeated spaces collapsed. |
| ASR token normalization | `A S R` is treated as `ASR` for comparison. |
| Coverage | Correct expected words recovered / expected words. |
| Precision | Correct expected words recovered / recognized words. |
| WER approximation | `(substitutions + deletions + insertions) / expected words`. |
| Expected words | `69` |

## Current Settings

### Raw Input Audio

| Setting | Current value |
| --- | ---: |
| Sample rate | `16000 Hz` |
| Frame duration | `20 ms` |
| Frames per buffer | `320` |
| Buffered audio | `20 s` |
| Stored sample capacity | `320000` |
| Memory reserved | `3.66 MiB` |
| Current test drops | `0` |

### Preprocessing (Gain)

| Setting | Current value |
| --- | ---: |
| Target peak amplitude | `0.3` |
| Gain mode | EMA smoothed |
| EMA alpha | `0.08` (time constant ≈ 240 ms) |
| Max gain | `40.0` |
| Noise gate threshold | `0.005` |

### VAD

| Setting | Current value |
| --- | ---: |
| Model | `models/vad/silero_vad.onnx` |
| Window samples | `512` |
| Speech threshold | `0.50` |
| Silence threshold | `0.38` |
| Speech start windows | `1` |
| Speech stop windows | `12` |
| Approximate stop duration | `384 ms` |

### ASR

| Setting | Current value |
| --- | ---: |
| Model directory | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` |
| Encoder | `encoder-epoch-99-avg-1-chunk-16-left-128.onnx` |
| Decoder | `decoder-epoch-99-avg-1-chunk-16-left-128.onnx` |
| Joiner | `joiner-epoch-99-avg-1-chunk-16-left-128.onnx` |
| Model precision | Full precision encoder, decoder, and joiner |
| Provider | `openvino` |
| Decoding method | `greedy_search` |
| Sample rate | `16000 Hz` |
| Feature dim | `80` |
| Threads | `1` |
| Accept sample block size | `320 samples` / `20 ms` |
| Current Sherpa load time | `8026 ms` |

## Test Results History

| Run | Provider | Main settings | ASR model | Segments | Drops | Coverage | Precision | WER approx. | Notes |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| Frame 50 ms test | cpu | `frame=50 ms` | INT8 encoder/joiner | `9` | `0` | `62.7%` | `71.2%` | `40.0%` | Produced 2 empty segments. Larger input frame did not improve coverage. |
| Baseline 20 ms | cpu | `frame=20 ms`, `silence=0.35`, `start=2`, `stop=6` | INT8 encoder/joiner | `8` | `0` | `65.3%` | `66.2%` | `40.0%` | Produced 1 empty segment. |
| Aggressive silence | cpu | `silence=0.50`, `start=1`, `stop=12` | INT8 encoder/joiner | N/A | `0` | `45.3%` | `47.2%` | `60.0%` | Too aggressive. It cut speech too often and lost coverage. |
| Best observed live coverage | cpu | `silence=0.37`, `start=1`, `stop=12` | INT8 encoder/joiner | `2` | `0` | `74.7%` | `69.1%` | `34.7%` | Best overall coverage and WER so far with INT8. Two long ASR segments. |
| Full precision first test | cpu | `silence=0.37`, `start=1`, `stop=12` | Full precision | `10` | `0` | `69.3%` | `69.3%` | `37.3%` | Improved some domain terms, but VAD segmentation became worse in this run. |
| Lower silence threshold | cpu | `silence=0.27`, `start=1`, `stop=12` | Full precision | `7` | `0` | `64.0%` | `69.6%` | `40.0%` | Fewer segments than full precision `0.37`, but lower coverage. Captured `AFTER THE PAUSE` better. |
| Higher stop windows | cpu | `silence=0.32`, `start=1`, `stop=14` | Full precision | `9` | `0` | `57.3%` | `65.2%` | `45.3%` | Worse than `silence=0.27`. Raising silence threshold made VAD more aggressive. |
| Previous current test | cpu | `silence=0.38`, `start=1`, `stop=12` | Full precision | `4` | `0` | `60.0%` | `64.3%` | `45.3%` | Fewer segments, but recognition accuracy did not improve. |
| EMA gain + OpenVINO | openvino | `silence=0.38`, `start=1`, `stop=12` | Full precision | `3` | `0` | `72.5%` | `83.3%` | `31.9%` | EMA gain smoothing (alpha=0.08, max=40). Last section of speech not captured due to segment boundary edge case. |
| **Boundary fix + EMA + OpenVINO** | **openvino** | **`silence=0.38`, `start=1`, `stop=12`** | **Full precision** | **`1`** | **`0`** | **`85.5%`** | **`83.1%`** | **`18.8%`** | **Best WER so far. Single segment covering full speech (34.40s). Fixed `last_segment_stop_sample_id_` using `next_sample_id_to_consume_` instead of `read_handle_.sample_id`.** |
| Hotwords + beam search | openvino | `silence=0.38`, `start=1`, `stop=12`, `modified_beam_search`, `max_active_paths=4`, `hotwords_score=1.5` | Full precision | `3` | `0` | `78.3%` | `75.0%` | `26.1%` | VAD split into 3 segments this run (natural pause triggered cut). `SPEECH RECOGNITION SYSTEM` correctly recognized for first time. `OKAY→OCCAY` (1 error vs 2 previously). `MICROPHONE TEST` and `VOICE ACTIVITY DETECTOR` still failing. Overall WER worse due to segmentation, not hotwords. |

## Current Test Segment Breakdown

Run: **Hotwords + beam search** — `silence=0.38`, `start=1`, `stop=12`, `modified_beam_search`, `max_active_paths=4`, `hotwords_score=1.5`, full precision, OpenVINO.

| Segment | Duration | Text |
| ---: | ---: | --- |
| 1 | `8.32 s` | `HALLO GOOD MORNING THIS IS A MIGRATE PHONE DEATH FOR THE BOYS I DEBUTED DETECTOR` |
| 2 | `6.42 s` | `AN AT SPEECH RECOGNITION SYSTEM I WILL SPEAK SLOWLY AT FIRST` |
| 3 | `21.90 s` | `THEN I WILL SPEAK A LITTLE FASTER THE BEGINNING AND THE END OF THE BIT SHALL BE THE TECHNICORRECTLY I WILL PAUSE FOR A MOMENT AND AFTER THEYRE BOTH I AM CONTINUED THE SAME SENTENCE WITH MORE WORTH TO VERIFY THAT EVERYTHING IS OCCAY THANK YOU` |

**Word-level errors (Levenshtein alignment, 15 sub + 0 del + 3 ins = 18 total):**

| Expected | Recognized | Type |
| --- | --- | --- |
| HELLO | HALLO | Substitution |
| — | MIGRATE | Insertion |
| MICROPHONE | PHONE | Substitution |
| TEST | DEATH | Substitution |
| — | BOYS | Insertion |
| VOICE | I | Substitution |
| ACTIVITY | DEBUTED | Substitution |
| AND | AN | Substitution |
| A *(in "AND A SPEECH")* | AT | Substitution |
| SPEECH *(in "OF THE SPEECH")* | BIT | Substitution |
| DETECTED | THE | Substitution |
| CORRECTLY | TECHNICORRECTLY | Substitution |
| — | AND *(before AFTER)* | Insertion |
| THE *(in "AFTER THE PAUSE")* | THEYRE | Substitution |
| PAUSE *(second)* | BOTH | Substitution |
| CONTINUING | CONTINUED | Substitution |
| WORDS | WORTH | Substitution |
| OKAY | OCCAY | Substitution |

**Previous best (Boundary fix + EMA + OpenVINO) word-level errors for reference:**

| Expected | Recognized | Type |
| --- | --- | --- |
| HELLO | HALLO | Substitution |
| A *(in "THIS IS A MICROPHONE")* | — | Deletion |
| MICROPHONE | MIGRA | Substitution |
| TEST | FONTES | Substitution |
| VOICE | BOYS | Substitution |
| A *(in "AND A SPEECH")* | THE | Substitution |
| SPEECH | SPITCH | Substitution |
| PAUSE *(in "I WILL PAUSE")* | PASS | Substitution |
| — | I WILL *(extra before AM)* | 2 Insertions |
| CONTINUING | CONTINUE | Substitution |
| — | O *(before K)* | Insertion |
| OKAY | K | Substitution |

## Findings

| Finding | Evidence | Impact |
| --- | --- | --- |
| Audio capture is stable. | All runs report `0` dropped samples. | Current failures are not explained by memory pressure or capture drops. |
| Single-segment coverage is the strongest predictor of WER. | Best WER `18.8%` (1 segment) vs `26.1%` (3 segments) with identical VAD settings. | VAD producing 1 segment is more important than any model or decoding change. |
| The best measured run is `silence=0.38`, `start=1`, `stop=12` with full precision on OpenVINO + boundary fix. | Coverage `85.5%`, Precision `83.1%`, WER `18.8%`. | Single segment, full speech captured. |
| Fixing the segment boundary edge case had the largest single impact. | WER dropped from `31.9%` to `18.8%` with no other change. 12 previously lost words recovered. | `last_segment_stop_sample_id_` must use `next_sample_id_to_consume_`, not `read_handle_.sample_id`. |
| EMA gain smoothing significantly reduced peak gain. | Previous peak gain: 600+; current peak gain: ≤ 40. | Cleaner signal during silence; precision went from 64.3% to 83%+ across full-precision runs. |
| Full precision did not improve global live-test WER until gain and boundary were fixed. | CPU full precision runs ranged `37.3%`–`45.3%` WER; OpenVINO + EMA + boundary fix achieved `18.8%`. | Two separate bugs were masking the model precision benefit. |
| Hotwords fixed `SPEECH RECOGNITION SYSTEM` (first correct recognition). | `modified_beam_search` + `hotwords.txt`, score `1.5`. Previously output as `SPITCH RECOGNITION SYSTEM`, `THE SPITCH RECOGNITION SYSTEM`, etc. | Hotword mechanism works; score `1.5` is enough for this phrase. |
| Hotword score `1.5` is insufficient for `MICROPHONE TEST` and `VOICE ACTIVITY DETECTOR`. | Both still wrong after enabling hotwords. Acoustic confusion is too strong. | Need higher score (`2.0`–`3.0`) or additional phonetically-similar variants in `hotwords.txt`. |
| `OKAY` improved from 2 errors (`O K`) to 1 error (`OCCAY`) with beam search. | `O K` = 1 sub + 1 ins; `OCCAY` = 1 sub. | Slight improvement; hotword didn't fully fix it at score `1.5`. |
| Beam search changed some previously-correct outputs to wrong ones. | `DETECTED CORRECTLY` → `THE TECHNICORRECTLY`; `PAUSE` → `BOTH`; `WORDS` → `WORTH`. | Beam search is not strictly better than greedy for all phrases; wider search introduces new errors. |
| VAD segmentation varies between runs with identical settings. | `silence=0.38`, `stop=12` produced 1 segment in one run and 3 in the next. | WER comparisons between runs must account for segment count; single-segment runs are more comparable. |
| `MICROPHONE TEST` is never recognized correctly. | `MIGRA FONTES`, `MIGRATE PHONE DEATH`, etc. across all runs. | Strongest hotword candidate; score increase or phonetic variant needed. |
| Buffer health is excellent with EMA gain. | `lag_ms` ≤ 11 ms in steady state; `headroom_ms` ≈ 19,988 ms; zero `sample_overwritten` events. | No data loss risk at current speech durations. |

## Recommended Next Experiments

| Priority | Experiment | Expected result |
| ---: | --- | --- |
| 1 | Increase `kHotwordsScore` to `2.5`–`3.0` | Overcome acoustic confusion for `MICROPHONE TEST` and `VOICE ACTIVITY DETECTOR`; current `1.5` is not enough. |
| 2 | Add phonetic variants to `hotwords.txt` (e.g. `MIGRATE PHONE`, `BOYS ACTIVITY`) | Give the hotword trie more paths to match the acoustically-confused outputs and boost them toward the target. |
| 3 | Re-run hotwords test when VAD produces a single segment, to get a fair comparison against `18.8%` baseline | Isolate the hotword contribution from VAD variability. |
| 4 | `silence=0.38`, `start=1`, `stop=16` | Longer silence window may reduce the chance of mid-speech cuts and keep single-segment behavior across runs. |
| 5 | Test `max_active_paths=8` once hotword improvements are confirmed | Check if a wider beam search further improves accuracy on hard phrases. |

## Notes

- `speech_stop_windows=12` corresponds to about `384 ms` because `12 * 512 / 16000 = 0.384 s`.
- `speech_stop_windows=16` corresponds to about `512 ms`.
- Sherpa ONNX endpointing is currently disabled; segment finalization is driven by local VAD `SPEECH_STOP` events.
- Decoding changed from `greedy_search` to `modified_beam_search` on 2026-04-22, required for hotwords.
- EMA gain smoothing introduced 2026-04-22: `alpha=0.08`, `max_gain=40.0`, `noise_gate=0.005`. Per-chunk peak normalization replaced with smoothed gain held across silent frames.
- Expected word count corrected from `75` to `69` on 2026-04-22 after fixing the benchmark speech text.
