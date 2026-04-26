# Benchmarks

Fuente: `build/logs/asr_logs.txt`

| Componente | Modelo / ruta | EP | Metrica | Valor | Notas |
|---|---|---|---|---:|---|
| Sherpa ONNX ASR | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | `cpu` | Tiempo de carga ORT / recognizer | `8767 ms` (`8.767 s`) | Extraido de `[SherpaOnnxSession] loading completed elapsed_ms=8767 ... provider=cpu`. |
| Sherpa ONNX ASR | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | `openvino` | Tiempo de carga ORT / recognizer (`greedy_search`) | `8026 ms` (`8.026 s`) | EMA gain run 2026-04-22. Rango observado: 6648–9949 ms segun estado del sistema. |
| Sherpa ONNX ASR | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | `openvino` | Tiempo de carga ORT / recognizer (`modified_beam_search`, `max_active_paths=4`) | `8074 ms` (`8.074 s`) | Hotwords run 2026-04-22. |
| Silero VAD ONNX | `../models/vad/silero_vad.onnx` | CPU implicito | Tiempo de carga ORT | N/D | El log solo muestra `initialized`; no imprime `elapsed_ms` para la creacion de `Ort::Session`. |
| Sherpa ONNX ASR | `models/asr/sherpa-onnx-streaming-zipformer-en-2023-06-26` | `cpu` | Tiempo real de inferencia | N/D | El log registra `decode_steps`, `accept_calls` y samples, pero no tiempo wall-clock de `Decode()`. |
| Silero VAD ONNX | `../models/vad/silero_vad.onnx` | CPU implicito | Tiempo real de inferencia | N/D | El log registra ventanas/probabilidades, pero no tiempo wall-clock de `Ort::Session::Run()`. |
| Sherpa ONNX ASR | Segmento 1 | `cpu` | Audio procesado / llamadas / decode steps | `2.562 s` / `129` / `7` | `total_samples=40984`, `accept_calls=129`, `decode_steps_so_far=7`. |
| Sherpa ONNX ASR | Segmento 2 | `cpu` | Audio procesado / llamadas / decode steps | `9.628 s` / `482` / `29` | `total_samples=154048`, `accept_calls=482`, `decode_steps_so_far=29`. |
| Sherpa ONNX ASR | Segmento 3 | `cpu` | Audio procesado / llamadas / decode steps | `14.528 s` / `727` / `44` | `total_samples=232447`, `accept_calls=727`, `decode_steps_so_far=44`. |
| Sherpa ONNX ASR | Segmento 4 | `cpu` | Audio procesado / llamadas / decode steps | `7.840 s` / `392` / `24` | `total_samples=125439`, `accept_calls=392`, `decode_steps_so_far=24`. |
| Sherpa ONNX ASR | Total segmentos (cpu baseline) | `cpu` | Audio procesado / llamadas / decode steps | `34.557 s` / `1730` / `104` | Suma de los 4 segmentos finalizados. |
| Silero VAD ONNX | Total run (cpu baseline) | CPU implicito | Ventanas procesadas / audio cubierto | `1220` / `39.040 s` | Cada ventana usa `512` samples a `16 kHz`. |
| Sherpa ONNX ASR | Total segmentos (openvino EMA gain) | `openvino` | Audio procesado / llamadas / decode steps | `31.91 s` / `N/D` / `98` | 3 segmentos finalizados (seg1=10.39s/32 steps, seg2=8.80s/27 steps, seg3=12.72s/39 steps) + 1 vacio. `greedy_search`. |
| Silero VAD ONNX | Total run (openvino EMA gain) | CPU implicito | Ventanas procesadas / audio cubierto | `1411` / `45.152 s` | Run 2026-04-22. |
| Sherpa ONNX ASR | Segmento 1 (hotwords + beam search) | `openvino` | Audio procesado / llamadas / decode steps | `8.324 s` / `417` / `25` | `total_samples=133184`, `accept_calls=417`, `decode_steps=25`. `modified_beam_search`, `max_active_paths=4`. |
| Sherpa ONNX ASR | Segmento 2 (hotwords + beam search) | `openvino` | Audio procesado / llamadas / decode steps | `6.420 s` / `321` / `19` | `total_samples=102719`, `accept_calls=321`, `decode_steps=19`. |
| Sherpa ONNX ASR | Segmento 3 (hotwords + beam search) | `openvino` | Audio procesado / llamadas / decode steps | `21.900 s` / `1095` / `68` | `total_samples=350399`, `accept_calls=1095`, `decode_steps=68` (67 streaming + 1 final flush). |
| Sherpa ONNX ASR | Total segmentos (hotwords + beam search) | `openvino` | Audio procesado / llamadas / decode steps | `36.644 s` / `1833` / `112` | 3 segmentos. `modified_beam_search`, `max_active_paths=4`, `hotwords_score=1.5`. ~`327 ms` de audio por decode step. |
