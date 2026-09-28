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

### Header pin numbers and pins to keep free

Physical pins, from the Nano connector table of the Nucleo-32 user manual
(UM1956). CN3 is the D-side, CN4 the A-side; pin 1 of each is marked on the
silkscreen.

| CN3 | signal | | CN4 | signal |
|---|---|---|---|---|
| 1 | D1 / PA9 — encoder B | | 1 | VIN |
| 2 | D0 / PA10 — encoder push | | 2 | **GND — analog ground** |
| 3 | RESET | | 3 | RESET |
| 4 | **GND — digital ground** | | 4 | +5 V |
| 5 | D2 / PA12 — DBG2 | | 5 | A7 / PA2 — ✗ VCP TX |
| 6 | D3 / PB0 — LCD CS | | 6 | A6 / PA7 — LCD SDA |
| 7 | D4 / PB7 — MENU | | 7 | A5 / PA6 — ✗ tied to D5 by SB16 |
| 8 | D5 / PB6 — SINGLE | | 8 | A4 / PA5 — ✗ tied to D4 by SB18 |
| 9 | D6 / PB1 — LCD A0 | | 9 | A3 / PA4 — DAC test out |
| 10 | D7 / PC14 — ✗ LSE crystal | | 10 | A2 / PA3 — backlight PWM |
| 11 | D8 / PC15 — ✗ LSE crystal | | 11 | A1 / PA1 — LCD SCL |
| 12 | D9 / PA8 — encoder A | | 12 | **A0 / PA0 — scope input** |
| 13 | D10 / PA11 — DBG1 | | 13 | AREF |
| 14 | D11 / PB5 — RUN | | 14 | +3V3 — LCD VDD |
| 15 | D12 / PB4 — LCD RST | | 15 | D13 / PB3 — LD3 |

Never connect anything to A4, A5 (shorted to the MENU and SINGLE buttons by the
default solder bridges), D7, D8 (the 32.768 kHz crystal that disciplines the
sample clock) or A7 (the ST-LINK virtual COM port's TX line).

## Display module: ERM19296FSF-1 (ST75256)

20-pin header, from section 4.1 of the module datasheet. Pins 11–18 are
DB7…DB0 in that order.

| Module pin | Name | Connect to |
|---|---|---|
| 1–4 | SI / SO / SCLK / CS# | optional font chip only — leave open, or tie pin 4 to VDD if a font chip is fitted |
| 5 | LEDA | backlight +, switched from +3V3 (see below) |
| 6 | VSS | CN3-4 (digital ground), own wire |
| 7 | VDD | CN4-14 (+3V3), 100 nF + 10 µF at the module |
| 8 | A0 (RS) | CN3-9 (D6, PB1) |
| 9 | RSTB | CN3-15 (D12, PB4) |
| 10 | CSB | CN3-6 (D3, PB0) |
| 11–14 | DB7…DB4 | **+3V3** (datasheet: fix D[7:4] high in serial mode) |
| 15–17 | DB3…DB1 | tied together = SDA → CN4-6 (A6, PA7) |
| 18 | DB0 | SCL → CN4-11 (A1, PA1) |
| 19 | ERD | **+3V3** (unused in serial mode) |
| 20 | RWR | **+3V3** (unused in serial mode) |

Electrical limits from the module datasheet: VDD 2.8–3.6 V (3.3 typ),
VIH ≥ 0.7·VDD, backlight LEDA–VSS **3.3 V typ**, ILED 70 mA typ / 80 mA max
(100 mA absolute). Duty 1/96, bias 1/11 — these are the values the init
sequence programs.

Interface selection (IF2, IF1, IF0 = L, L, L for 4-wire SPI) is not on the
header, so it is strapped on the module itself: check the jumpers on the back
before assuming SPI is selected.

**Backlight drive.** The header has no LEDK pin: the backlight cathode is tied
to VSS inside the module, so it must be switched on the high side. A P-channel
MOSFET from +3V3 to LEDA, gate to PA3 through ~100 Ω with a 100 kΩ pull-up to
+3V3, keeps it off while PA3 is still an input at reset. A P-FET conducts with
a low gate, so the PWM is inverted in firmware
(`BACKLIGHT_ACTIVE_LOW` in [`board.h`](../Core/App/board.h)). Do not run the
backlight from +5 V: 3.3 V is the rated supply. A 10–22 Ω series resistor is
optional insurance.

**Serial timing** (ST75256 section 14.3): tSCYC ≥ 80 ns → SCL ≤ 12.5 MHz, and
SDA is latched on the rising edge of SCL (SPI mode 0). The firmware starts at
5 MHz (prescaler /16); /8 = 10 MHz is still inside the spec.

## Grounding

Every GND pin on the Nucleo is the same net, but the *wires* are not. Use two
rails joined at exactly one place, the board itself:

- **Analog rail → CN4-2**, the GND pin next to A0: the 1 nF at PA0, the probe's
  ground clip, the DAC loopback return. Keep the 100 Ω + 1 nF loop within a few
  millimetres of PA0 — at 2.5-cycle sampling the ADC draws a sharp charge kick,
  and a long return path turns it into gain error.
- **Digital rail → CN3-4**: module VSS, button and encoder commons, the MOSFET
  source. The backlight's 70 mA at 20 kHz returns through the module's VSS pin,
  so give VSS its own wire and keep it away from the analog rail.

The 3.3 V rail is also the ADC reference (VREF+ = VDDA on this package), so a
load step on it moves every reading: decouple at the module, and budget the
current (a USB-powered Nucleo allows 300 mA for the board and everything on it).

**The probe ground is the PC's ground.** It runs through the ST-LINK USB cable
to the host and, through the host's charger, possibly to mains earth. Clip it
to anything that is not at that potential and the current returns through the
USB cable. Battery-powered or same-ground circuits only — never a
mains-referenced node, with or without the front end, which is not isolated
either.

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
