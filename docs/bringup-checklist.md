# Hardware bring-up checklist (NUCLEO-L432KC)

Work through these steps in order. Each one checks something the following
steps rely on.

## 0. Before power

- [ ] Nucleo solder bridges SB16/SB18 are closed (default): PA5↔PB7 and PA6↔PB6
      are shorted, so **PA5/PA6 must stay unused** (they are analog/high-Z in the
      `.ioc`).
- [ ] Nothing above 3.3 V can reach PA0. There is no front end on the prototype.
- [ ] Backlight (about 75 mA) is driven through a transistor/MOSFET from PA3,
      never directly from the GPIO.

## 1. Firmware alive

- [ ] Build (`cmake --build build` or STM32CubeIDE) and flash.
- [ ] LD3 blinks at about 1 Hz (main loop running, watchdog refreshed).
      Fast 5 Hz blinking means the boot self-check refused to start;
      go to step 2.
- [ ] `python tools/scope.py info` lists firmware 0.1.0 and 16 time bases.

## 2. Configuration self-check

- [ ] `python tools/scope.py status` → `boot_faults: 0`.
      Bits: 0 ADC DMA not circular, 1 ADC clock async, 2 SPI not 8-bit,
      3 SPI TX DMA missing, 4 SYSCLK ≠ 80 MHz, 5 MSI PLL mode off,
      6 ADC calibration/VREFINT failed, 7 hardware CRC ≠ software CRC,
      8 linker script lost the 64 KB RAM / reserved settings page.
- [ ] `vdda_mv` reads 3250–3350 mV (it is derived from VREFINT).

## 3. Acquisition timing

- [ ] Logic analyzer on **D2 (PA12)**: a square wave at `fs / 16384`, e.g.
      305.18 Hz at 5 MS/s (time bases 0–5). Record the measured value (V1).
- [ ] `status` → `adc_overruns: 0`, `dma_events` increasing.
- [ ] **D10 (PA11)** pulses once per processed record. Its width is the
      measurement + decimation time.

## 4. Loopback

- [ ] Jumper **A3 (PA4, DAC)** → **A0 (PA0, ADC)**, plus 1 nF C0G from A0 to GND.
- [ ] `python tools/scope.py gen sine 1000`, then `timebase 6`, then
      `capture --plot`: 4 periods of a 0.4–2.9 V sine.
- [ ] `python tools/scope.py selftest --report docs/validation/V11/report.md`

## 5. Display (ERM19296FSF-1 / ST75256)

Full pin table in [hardware.md](hardware.md). Short version: SCL = module pin 18
→ A1, SDA = pins 15–17 tied together → A6, CSB = pin 10 → D3, A0 = pin 8 → D6,
RSTB = pin 9 → D12, VDD = pin 7 → 3V3, VSS = pin 6 → GND, and **pins 11–14, 19
and 20 to +3V3** (the datasheet requires D[7:4], ERD and RWR tied high in serial
mode). Backlight (pin 5, LEDA) switches on the high side from +3V3 through a
P-channel MOSFET; there is no LEDK pin.

- [ ] Before wiring: confirm the interface straps on the module select 4-wire
      SPI (IF2, IF1, IF0 = L, L, L). They are not on the header.
- [ ] Nothing on screen: this is almost always the straps, the six tie-high
      pins, or contrast. Tune `LCD_VOP_DEFAULT` in `lcd_st75256.h`
      (V0 = 3.6 V + code × 0.04 V; 0x118 = 14.8 V; usable 7–18 V).
- [ ] Image upside-down or mirrored: `0xBC` parameter, MV/MX/MY bits (0x03
      rotates 180°).
- [ ] Each 8-pixel band flipped vertically: swap `0x0C` for `0x08` (data format
      DO bit).
- [ ] Image shifted: set `LCD_COL_OFFSET` / `LCD_PAGE_OFFSET` for where the glass
      sits in the controller's 256 × 162 DDRAM.
- [ ] Backlight stays on at full brightness: the P-FET gate is being pulled low
      — check the 100 kΩ pull-up and `BACKLIGHT_ACTIVE_LOW` in `board.h`.
- [ ] Once stable, raise SPI to /8 (10 MHz, still under the 12.5 MHz the
      datasheet allows) in CubeMX and re-check.
- [ ] `status` → `lcd_frames` increases at 20 FPS or more (V9).

## 6. Controls

- [ ] Encoder A/B = D9/D1 (PA8/PA9), push = D0 (PA10); RUN = D11 (PB5),
      SINGLE = D5 (PB6), MENU = D4 (PB7). All active-low to GND.
- [ ] One encoder detent moves the selected parameter by exactly one step. If
      it moves two, the encoder has 2 counts per detent: change
      `COUNTS_PER_DETENT` in `input.c`.
