# Cálculo de tamaño de buffer de audio para micrófono

## Fórmulas

```
samples_por_buffer = sample_rate × duración_ms / 1000
bytes_por_buffer   = samples_por_buffer × canales × bytes_por_sample
```

| Tipo    | bytes_por_sample |
|---------|-----------------|
| int16   | 2               |
| float32 | 4               |

---

## Tabla 1: parámetros

| Parámetro        | Significa                    | Ejemplo       |
|------------------|------------------------------|---------------|
| sample_rate      | muestras por segundo         | 16000 Hz      |
| duración_ms      | cuánto audio guarda el buffer| 20 ms         |
| canales          | mono=1, estéreo=2            | 1             |
| bytes_por_sample | depende del formato          | 2 para int16  |

**Ejemplo:**
```
samples_por_buffer = 16000 × 20 / 1000 = 320
bytes_por_buffer   = 320 × 1 × 2       = 640
```

---

## Tabla 2: mono, 16 kHz, int16

| Duración | Samples por buffer | Bytes por buffer |
|----------|--------------------|------------------|
| 10 ms    | 160                | 320              |
| 20 ms    | 320                | 640              |
| 30 ms    | 480                | 960              |
| 40 ms    | 640                | 1280             |
| 100 ms   | 1600               | 3200             |
| 1000 ms  | 16000              | 32000            |

---

## Tabla 3: mono, 16 kHz, float32

| Duración | Samples por buffer | Bytes por buffer |
|----------|--------------------|------------------|
| 10 ms    | 160                | 640              |
| 20 ms    | 320                | 1280             |
| 30 ms    | 480                | 1920             |
| 40 ms    | 640                | 2560             |
| 100 ms   | 1600               | 6400             |
| 1000 ms  | 16000              | 64000            |

---

## Tabla 4: mono, 8 kHz, int16

| Duración | Samples por buffer | Bytes por buffer |
|----------|--------------------|------------------|
| 10 ms    | 80                 | 160              |
| 20 ms    | 160                | 320              |
| 30 ms    | 240                | 480              |
| 40 ms    | 320                | 640              |
| 100 ms   | 800                | 1600             |
| 1000 ms  | 8000               | 16000            |

---

## Tabla 5: estéreo, 48 kHz, int16

| Duración | Samples por canal | Samples totales | Bytes por buffer |
|----------|-------------------|-----------------|------------------|
| 10 ms    | 480               | 960             | 1920             |
| 20 ms    | 960               | 1920            | 3840             |
| 30 ms    | 1440              | 2880            | 5760             |
| 40 ms    | 1920              | 3840            | 7680             |
| 100 ms   | 4800              | 9600            | 19200            |
| 1000 ms  | 48000             | 96000           | 192000           |

---

## Tabla 6: configuraciones típicas VAD/ASR

| Configuración         | Frame  | Bytes |
|-----------------------|--------|-------|
| mono, 16 kHz, int16   | 10 ms  | 320   |
| mono, 16 kHz, int16   | 20 ms  | 640   |
| mono, 16 kHz, int16   | 30 ms  | 960   |
| mono, 16 kHz, float32 | 20 ms  | 1280  |
| mono, 8 kHz, int16    | 20 ms  | 320   |

---

## Regla rápida

**mono + 16 kHz + int16**

| Duración | Samples | Bytes |
|----------|---------|-------|
| 10 ms    | 160     | 320   |
| 20 ms    | 320     | 640   |
| 30 ms    | 480     | 960   |

**mono + 16 kHz + float32**

| Duración | Samples | Bytes |
|----------|---------|-------|
| 10 ms    | 160     | 640   |
| 20 ms    | 320     | 1280  |
| 30 ms    | 480     | 1920  |
