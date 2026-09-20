# Hardware

## Prototype: NUCLEO-L432KC

| Pin | Header | Function | Notes |
|---|---|---|---|
| PA0 | A0 | ADC1_IN5 scope input | The only fast ADC channel on the 32-pin package. **0–3.3 V only; no protection** |
| PA1 | A1 | SPI1_SCK → LCD | Medium slew rate to limit coupling into the input |
| PA7 | A6 | SPI1_MOSI → LCD | |
| PB0 | D3 | LCD CS | Software chip-select |
| PB1 | D6 | LCD A0 (data/command) | |
| PB4 | D12 | LCD reset | Held low until `lcd_init()` |
| PA3 | A2 | TIM2_CH4 backlight PWM, 20 kHz | Drives a transistor/MOSFET gate |
| PA4 | A3 | DAC1_OUT1 test signal | Jumper to A0 for loopback tests |
| PA8 / PA9 | D9 / D1 | TIM1 encoder A / B | Pull-ups, input filter |
| PA10 | D0 | Encoder push | EXTI, falling edge |
| PB5 / PB6 / PB7 | D11 / D5 / D4 | RUN / SINGLE / MENU | EXTI, falling edge, pull-ups |
| PA11 / PA12 | D10 / D2 | DBG1 / DBG2 timing probes | Become USB D−/D+ on the custom PCB |
| PB3 | D13 | LD3 status LED | |
| PA2 / PA15 | – | USART2 TX / RX → ST-LINK VCP | 921600 Bd |
| PC14 / PC15 | – | 32.768 kHz LSE | Disciplines MSI → accurate sample clock |

Board-level constraints that drove this pinout (SB16/SB18 shorts, VREF+ = VDDA,
PA0 being the only fast channel) are covered in the configuration handoff and in
[bringup-checklist.md](bringup-checklist.md).

## Clock tree

LSE 32.768 kHz → MSI 4 MHz in PLL mode (locked to LSE) → PLL (M = 1, N = 40,
R = 2) → **SYSCLK = HCLK = PCLK1 = PCLK2 = 80 MHz**. TIM6 (sample clock), TIM7
(DAC clock) and the ADC (synchronous HCLK/1) all run from the same 80 MHz, so
the sample rate is exactly `80 MHz / D` with LSE-crystal accuracy.

## Planned front end (Rev A PCB, not built yet)

```
BNC ─ current-limit R chain ─ low-C TVS ─ attenuator (1 MΩ : 39.2 k / 24.9 k to 1.65 V)
    ─ clamp ─ RRIO unity buffer (≥ 20 MHz GBW, ≥ 20 V/µs) ─ 33–100 Ω ─┬─ PA0
                                                                  1–2.2 nF C0G
```

| Range | High arm | Low arm to 1.65 V | Nominal gain | ADC swing at ±full scale |
|---|---|---|---|---|
| ±30 V | 1.00 MΩ | 39.2 kΩ | 26.5 : 1 | ≈ 0.52 – 2.78 V |
| ±50 V | 1.00 MΩ | 24.9 kΩ | 41.2 : 1 | ≈ 0.44 – 2.86 V |

The nominal coefficients are in [`Core/App/calib.c`](../Core/App/calib.c). The real
ones come from `scope.py cal set` + `cal save`.

**Safety:** this front end is designed for low-energy electronics signals only.
It is not a mains-rated or CAT-rated instrument.
