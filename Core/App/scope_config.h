/**
 * @file    scope_config.h
 * @brief   Compile-time configuration shared by every firmware module.
 *
 * This header is intentionally free of HAL / CMSIS includes so that the
 * portable modules (trigger, acquisition engine, measurements, protocol,
 * graphics) can be compiled and unit-tested on a desktop host.
 */
#ifndef SCOPE_CONFIG_H
#define SCOPE_CONFIG_H

#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Firmware identity                                                          */
/* ------------------------------------------------------------------------- */
#define FW_VERSION_MAJOR        0u
#define FW_VERSION_MINOR        1u
#define FW_VERSION_PATCH        0u
#define PROTOCOL_VERSION        1u

/* ------------------------------------------------------------------------- */
/* Clocking (must match SystemClock_Config / .ioc)                            */
/* ------------------------------------------------------------------------- */
/** TIM6 kernel clock = APB1 timer clock = SYSCLK (MSI 4 MHz locked to LSE, PLL N=40 R=2). */
#define SCOPE_TIMER_CLK_HZ      80000000u
/** ADC kernel clock in synchronous mode HCLK/1. */
#define SCOPE_ADC_CLK_HZ        80000000u
/** Conversion time of a 12-bit SAR conversion, in ADC cycles, excluding sampling. */
#define SCOPE_ADC_CONV_CYCLES_X2 25u   /* 12.5 cycles, stored x2 to stay integer */

/* ------------------------------------------------------------------------- */
/* Acquisition                                                                */
/* ------------------------------------------------------------------------- */
/** Circular DMA buffer length in samples (uint16_t). 16384 x 2 B = 32 KB. */
#define ACQ_BUF_LEN             16384u
/**
 * Guard band between the maximum record length and the buffer length.
 * It absorbs the latency between "post-trigger count reached" being detected
 * in the main loop and TIM6 actually being stopped. If the writer overshoots
 * by more than this, the record is discarded (never displayed corrupted).
 */
#define ACQ_GUARD_SAMPLES       1024u
#define ACQ_RECORD_MAX          (ACQ_BUF_LEN - ACQ_GUARD_SAMPLES)
#define ADC_FULL_SCALE          4095u

/* ------------------------------------------------------------------------- */
/* Display: ERM19296FSF-1 (ST75256), 192 x 96, 1 bpp                          */
/* ------------------------------------------------------------------------- */
#define LCD_WIDTH               192u
#define LCD_HEIGHT              96u
#define LCD_PAGES               (LCD_HEIGHT / 8u)
#define LCD_FB_BYTES            (LCD_WIDTH * LCD_PAGES)   /* 2304 */

/* Screen layout (pixels) */
#define UI_STATUS_Y             0u      /* 8 px status line               */
#define UI_PLOT_X               0u
#define UI_PLOT_Y               10u     /* waveform viewport top          */
#define UI_PLOT_W               LCD_WIDTH
#define UI_PLOT_H               64u     /* 4 vertical divisions x 16 px   */
#define UI_HDIVS                8u      /* 24 px per horizontal division  */
#define UI_VDIVS                4u
#define UI_MEAS_Y0              77u     /* two text rows of measurements  */
#define UI_MEAS_Y1              87u

/** Target display refresh period (ms). 40 ms -> 25 FPS. */
#define UI_FRAME_PERIOD_MS      40u

/* ------------------------------------------------------------------------- */
/* PC link                                                                    */
/* ------------------------------------------------------------------------- */
#define LINK_RX_RING_LEN        512u
#define LINK_MAX_PAYLOAD        1024u
#define LINK_TX_SLOTS           3u
/** Samples per CAPTURE_DATA packet (2 bytes each + 8-byte chunk header). */
#define LINK_CAPTURE_CHUNK      500u

#endif /* SCOPE_CONFIG_H */
