# PC protocol (v1)

Binary, little-endian, framed packets over USART2 (ST-LINK virtual COM port,
921600 Bd 8N1) on the Nucleo prototype. The same framing is meant to run over
USB CDC on the custom PCB. Reference implementations:
[`Core/App/protocol.c`](../Core/App/protocol.c) (firmware) and
[`tools/scope_proto.py`](../tools/scope_proto.py) (host). Both are tested against the
same golden frame, so they cannot drift apart silently.

## Frame

| offset | size | field |
|---|---|---|
| 0 | 1 | `0xA5` sync |
| 1 | 1 | `0x5A` sync |
| 2 | 1 | message type |
| 3 | 1 | sequence number (replies echo the request's) |
| 4 | 2 | payload length `N` (≤ 1024) |
| 6 | N | payload |
| 6+N | 4 | CRC-32 (zlib / IEEE 802.3) over bytes `2 .. 6+N-1` |

The receiver hunts for `A5 5A`, rejects oversize lengths and CRC failures
(counted in `STATUS`), and resynchronises on the next sync pattern. On target the
CRC is computed by the STM32 CRC unit set up to match zlib (reflected
input/output, init `0xFFFFFFFF`, final XOR). A boot self-test compares it with the
software implementation.

## Host → device

| type | name | payload | reply |
|---|---|---|---|
| `0x01` | PING | any bytes (≤ 1024) | `PONG` with the same bytes |
| `0x02` | GET_INFO | – | `INFO` |
| `0x03` | GET_STATUS | – | `STATUS` |
| `0x10` | SET_RUN | `u8` 0 = stop, 1 = run, 2 = single | `ACK` |
| `0x11` | SET_TIMEBASE | `u8` index into the timebase table | `ACK` |
| `0x12` | SET_TRIGGER | `u16 level`, `u16 hyst`, `u8 edge` (0 rise, 1 fall), `u8 mode` (0 auto, 1 normal, 2 single), `u8 pretrig%` (10–90) | `ACK` |
| `0x14` | SET_SIGGEN | `u8 wave` (0 off, 1 sine, 2 square, 3 triangle), `u8 freq` (0 = 100 Hz, 1 = 1 kHz, 2 = 10 kHz) | `ACK` |
| `0x20` | GET_CAPTURE | `u8 flags`, bit0 = send the record that is frozen now (after SINGLE/STOP) | `CAPTURE_HDR` + `CAPTURE_DATA`… or `ACK(busy)` |
| `0x21` | GET_MEAS | – | `MEAS` |
| `0x30` | CAL_GET | `u8 range` | `CAL` |
| `0x31` | CAL_SET | `u8 range`, `f32 gain`, `f32 offset_mv` | `ACK` |
| `0x32` | CAL_SAVE | – (settings + calibration to flash) | `ACK` |
| `0x3F` | RESET_STATS | – | `ACK` |

`ACK` payload: `u8 command`, `u8 status` (0 ok, 1 bad length, 2 bad argument,
3 unknown, 4 busy, 5 failed).

## Device → host

**INFO `0x81`**: `u8 fw_major, fw_minor, fw_patch, protocol`, `u32 timer_clk_hz`,
`u32 buffer_len`, `u32 record_max`, `u16 lcd_w, lcd_h`, `u8 n_ranges`,
`u8 n_timebases`, `u16 capture_chunk`. Then for each timebase:
`u32 ns_per_div`, `u32 period_cycles`, `u16 record_len`, `u8 sampling_time_code`.
Then `u32 uid[3]`.

**STATUS `0x82`** (96 bytes): `u8 acq_state, mode, running, timebase`,
`u16 trig_level, trig_hyst`, `u8 edge, pretrig, range, gen_wave`, `f32 vdda_mv`,
then 20 × `u32`: uptime_ms, records, triggers, auto_triggers, search_skips,
records_discarded, max_stop_overshoot, adc_overruns, dma_events, lcd_frames,
tx_packets, tx_dropped, rx_frames_ok, rx_crc_errors, rx_len_errors, loop_max_us,
loop_count, boot_faults, tx_queue_hwm, adc_errors.

**MEAS `0x83`**: `f32 vpp_mv, vavg_mv, vrms_mv, freq_hz, duty_pct`, `u8 flags`
(bit0 valid, bit1 periodic, bit2 clipped), `u8 reserved`, `u16 min_code, max_code`.

**CAPTURE_HDR `0x90`** (50 bytes):

| field | type | meaning |
|---|---|---|
| id | u16 | capture counter; chunks carry the same id |
| n | u32 | samples in the record |
| trig_index | u32 | sample index of the trigger point (= pre-trigger length) |
| trig_frac | f32 | sub-sample trigger position, in (-1, 0] |
| timer_clk | u32 | 80 000 000 |
| period_cycles | u32 | sample period in timer clocks → `fs = timer_clk / period_cycles` |
| ns_per_div | u32 | display timebase |
| smp | u8 | ADC sampling-time code (0 = 2.5 cycles … 7 = 640.5 cycles) |
| range | u8 | input range id |
| flags | u8 | bit0 auto-triggered (no real edge), bit1 falling edge |
| mode | u8 | trigger mode |
| trig_level | u16 | raw code |
| chunk | u16 | samples per `CAPTURE_DATA` packet |
| vdda_mv | f32 | measured analog supply (= ADC reference) |
| gain, offset_mv | f32, f32 | calibration: `V_in = gain · (code · vdda/4095 − offset)` |
| crc | u32 | CRC-32 over all `n` samples as little-endian `u16` |

**CAPTURE_DATA `0x91`**: `u16 id`, `u32 first_sample`, `u16 count`, `count × u16`
raw 12-bit codes.

A capture is always one complete, frozen record: acquisition stays stopped until
the last chunk is queued, so a requested capture is never overwritten mid-transfer.
Live data (future decimated stream) would be sent only when the TX queue has room
and dropped otherwise (`tx_dropped`).

**CAL `0x84`**: `u8 range`, `f32 gain`, `f32 offset_mv`. **LOG `0x9F`**: UTF-8 text
(boot banner, errors). **PONG `0x8F`**: echo of PING.

## Throughput

At 921600 Bd (≈ 92 kB/s of payload before framing), a 15 238-sample record
(30.5 KB) takes about 0.35 s. That is why the link is for commands, frozen captures
and processed data. It is not meant for continuous raw streaming: 5 MS/s × 2 B is
10 MB/s, far more than full-speed USB can carry.
