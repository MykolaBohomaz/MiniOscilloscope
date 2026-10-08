# Measured results

Produced by `tools/bench.py`. Raw values in `results.json`.

## Input-referred noise (gnd)

Condition: A0 shorted to GND, backlight on

| timebase | sample rate | mean code | RMS codes | RMS mV | p-p codes | clipped |
|---|---|---|---|---|---|---|
| 0 | 5.0000 MS/s | 0.5 | 0.75 | 0.608 | 3 | 59 % |
| 5 | 5.0000 MS/s | 0.9 | 0.82 | 0.661 | 4 | 39 % |
| 7 | 1.9048 MS/s | 0.1 | 0.40 | 0.324 | 3 | 89 % |
| 11 | 0.0959 MS/s | 0.2 | 0.43 | 0.348 | 3 | 88 % |
| 13 | 0.0192 MS/s | 0.3 | 2.99 | 2.412 | 99 | 88 % |

## Input-referred noise (midscale)

Condition: 2k/1k divider from 3V3, ~1.1 V, backlight on

| timebase | sample rate | mean code | RMS codes | RMS mV | p-p codes | clipped |
|---|---|---|---|---|---|---|
| 0 | 5.0000 MS/s | 1326.7 | 1.46 | 1.176 | 10 | 0 % |
| 5 | 5.0000 MS/s | 1326.3 | 1.47 | 1.187 | 13 | 0 % |
| 7 | 1.9048 MS/s | 1356.3 | 1.35 | 1.090 | 22 | 0 % |
| 11 | 0.0959 MS/s | 1357.2 | 1.46 | 1.178 | 20 | 0 % |
| 13 | 0.0192 MS/s | 1357.1 | 15.06 | 12.147 | 615 | 0 % |

## Firmware timing (GPIO probes on a bench oscilloscope)

- instrument: Tektronix MDO3014, 20 ms/div, 10M points, 50 MS/s, measurement statistics
- probes: CH1 = LCD CS (PB0, Nucleo CN3-6), CH2 = DBG1 (PA11, Nucleo CN3-13); 1 kHz DAC sine on A0, AUTO trigger
- measured 2026-10-08 18:18:19

| quantity | result | derived from |
|---|---|---|
| Display frame period | **40.01 ms = 25.0 FPS (sigma 31 us)** | CS period |
| Full-frame transfer, 2304 B over SPI1 DMA @ 5 MHz | **3.71 ms (theory 3.69 ms)** | 40.01 ms period - 36.30 ms CS-high width |
| Record processing, timebase 0 (5 us/div, 200 samples) | **0.55 ms** | DBG1 period 1.000 ms - low width 0.4485 ms |
| Record processing, timebase 6 (500 us/div, 15238 samples) | **15.7 ms** | DBG1 period 20.46 ms - low width 4.74 ms |
| Processing cost per sample | **~1.0 us (~81 CPU cycles) + ~0.35 ms fixed** | slope between the two timebases |
| Waveform update rate, timebase 0 | **1000 records/s (every cycle of a 1 kHz input)** | DBG1 period |
| Waveform update rate, timebase 6 | **49 records/s, 23 % live time** | 1 / 20.46 ms; 4.74 / 20.46 |
| Capture-to-screen latency | **not measured yet** | Delay CH2 falling -> CH1 falling, forward; add 15.7 + 3.71 ms |

Record processing dominates dead time at slow timebases; sampling is stopped while it runs, so no data is lost.
