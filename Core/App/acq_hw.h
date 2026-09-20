/**
 * @file    acq_hw.h
 * @brief   STM32 side of acquisition: TIM6 -> ADC1 (PA0) -> DMA1 Ch1 circular.
 */
#ifndef ACQ_HW_H
#define ACQ_HW_H

#include <stdbool.h>
#include <stdint.h>
#include "acq_engine.h"
#include "scope_config.h"
#include "timebase.h"

extern uint16_t g_adc_buf[ACQ_BUF_LEN];
extern const acq_hw_ops_t g_acq_hw_ops;

typedef struct {
    uint32_t dma_events;      /**< DMA half + full transfer interrupts     */
    uint32_t adc_overruns;
    uint32_t adc_errors;      /**< other ADC / DMA errors                   */
    uint16_t vrefint_raw;
} acq_hw_stats_t;

extern volatile acq_hw_stats_t g_acq_hw_stats;

/** Boot: ADC self-calibration, VREFINT measurement. ADC must be disabled. */
bool acq_hw_init(void);

/**
 * Safe (re)configuration (handoff 8.3): stop TIM6, stop ADC+DMA, set the
 * sampling time and TIM6 divider, restart ADC+DMA waiting for triggers.
 * The sample clock stays OFF; acq_arm() starts it.
 */
bool acq_hw_configure(const timebase_t *tb);

/** Measured analog supply (VREF+ = VDDA on this package), mV. */
float acq_hw_vdda_mv(void);

/** Returns and clears a pending "overrun / DMA error" request for restart. */
bool acq_hw_take_fault(void);

/* ISR hooks (called from hal_callbacks.c only) */
void acq_hw_isr_dma_half(void);
void acq_hw_isr_dma_full(void);
void acq_hw_isr_error(uint32_t hal_error_code);

#endif /* ACQ_HW_H */
