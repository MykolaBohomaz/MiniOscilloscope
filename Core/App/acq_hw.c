/**
 * @file    acq_hw.c
 *
 * Absolute write position
 * -----------------------
 * The DMA only exposes CNDTR (samples remaining in the current lap). The
 * absolute sample count is laps * N + (N - CNDTR), where laps is incremented
 * by the transfer-complete ISR. Reading both is racy: CNDTR reloads to N in
 * hardware before the TC interrupt runs. The read is therefore done with
 * interrupts masked; if TCIF1 is pending (ISR not yet run) and the position is
 * already in the first half of a new lap, one lap is added. This keeps the
 * position monotonic, which the acquisition engine relies on.
 */
#include "acq_hw.h"
#include "adc.h"
#include "board.h"
#include "tim.h"
#include "calib.h"

uint16_t g_adc_buf[ACQ_BUF_LEN] __attribute__((aligned(4)));
volatile acq_hw_stats_t g_acq_hw_stats;

static volatile uint32_t s_laps;
static volatile bool     s_fault;
static float             s_vdda_mv = 3300.0f;

static const uint32_t k_hal_smp[SMP_COUNT] = {
    ADC_SAMPLETIME_2CYCLES_5,  ADC_SAMPLETIME_6CYCLES_5,
    ADC_SAMPLETIME_12CYCLES_5, ADC_SAMPLETIME_24CYCLES_5,
    ADC_SAMPLETIME_47CYCLES_5, ADC_SAMPLETIME_92CYCLES_5,
    ADC_SAMPLETIME_247CYCLES_5, ADC_SAMPLETIME_640CYCLES_5,
};

/* ------------------------------------------------------------------------ */

static uint32_t hw_write_pos(void *ctx)
{
    (void)ctx;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t laps = s_laps;
    const uint32_t pos = ACQ_BUF_LEN - (uint32_t)__HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
    if ((DMA1->ISR & DMA_ISR_TCIF1) != 0u && pos < ACQ_BUF_LEN / 2u) {
        laps++;   /* wrapped in hardware, ISR still pending */
    }
    __set_PRIMASK(primask);
    return laps * ACQ_BUF_LEN + (pos % ACQ_BUF_LEN);
}

static void hw_set_sampling(void *ctx, bool run)
{
    (void)ctx;
    /* Direct register access: this is on the stop-latency critical path. */
    if (run) {
        TIM6->CR1 |= TIM_CR1_CEN;
    } else {
        TIM6->CR1 &= ~TIM_CR1_CEN;
    }
}

const acq_hw_ops_t g_acq_hw_ops = {
    .write_pos = hw_write_pos,
    .set_sampling = hw_set_sampling,
    .ctx = NULL,
};

/* ------------------------------------------------------------------------ */

static bool adc_select_channel(uint32_t channel, uint32_t smp)
{
    ADC_ChannelConfTypeDef c = {0};
    c.Channel = channel;
    c.Rank = ADC_REGULAR_RANK_1;
    c.SamplingTime = smp;
    c.SingleDiff = ADC_SINGLE_ENDED;
    c.OffsetNumber = ADC_OFFSET_NONE;
    c.Offset = 0;
    return HAL_ADC_ConfigChannel(&hadc1, &c) == HAL_OK;
}

/** One-off software-triggered VREFINT measurement (ADC must be stopped). */
static uint16_t measure_vrefint(void)
{
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK ||
        !adc_select_channel(ADC_CHANNEL_VREFINT, ADC_SAMPLETIME_640CYCLES_5)) {
        return 0u;
    }
    HAL_Delay(1);                         /* VREFINT buffer start-up */

    uint32_t acc = 0u;
    const uint32_t n = 16u;
    for (uint32_t i = 0; i < n; i++) {
        if (HAL_ADC_Start(&hadc1) != HAL_OK ||
            HAL_ADC_PollForConversion(&hadc1, 5u) != HAL_OK) {
            return 0u;
        }
        acc += HAL_ADC_GetValue(&hadc1);
    }
    (void)HAL_ADC_Stop(&hadc1);

    /* Restore the acquisition configuration (TIM6 TRGO, circular DMA). */
    LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_NONE);
    hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
    hadc1.Init.DMAContinuousRequests = ENABLE;
    if (HAL_ADC_Init(&hadc1) != HAL_OK) {
        return 0u;
    }
    return (uint16_t)(acc / n);
}

bool acq_hw_init(void)
{
    hw_set_sampling(NULL, false);
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK) {
        return false;
    }
    const uint16_t v = measure_vrefint();
    g_acq_hw_stats.vrefint_raw = v;
    if (v != 0u) {
        s_vdda_mv = calib_vdda_mv(v, *VREFINT_CAL_ADDR);
    }
    return v != 0u;
}

bool acq_hw_configure(const timebase_t *tb)
{
    if (!tb_is_valid(tb)) {
        return false;
    }
    /* 1. stop the sample clock, 2. stop ADC + DMA */
    hw_set_sampling(NULL, false);
    (void)HAL_ADC_Stop_DMA(&hadc1);
    /* HAL never clears these once set; left alone, every later DMA lap would
     * go to the error callback instead of the lap counter. */
    CLEAR_BIT(hadc1.State, HAL_ADC_STATE_ERROR_DMA | HAL_ADC_STATE_ERROR_INTERNAL);
    hadc1.ErrorCode = HAL_ADC_ERROR_NONE;

    /* 3. sampling time (only legal while the ADC is stopped) */
    if (!adc_select_channel(ADC_CHANNEL_5, k_hal_smp[tb->smp])) {
        return false;
    }

    /* 4. TIM6 divider; UG loads PSC/ARR now. UG also fires TRGO, which is
     *    harmless because the ADC is stopped. */
    __HAL_TIM_SET_PRESCALER(&htim6, tb->psc);
    __HAL_TIM_SET_AUTORELOAD(&htim6, tb->arr);
    __HAL_TIM_SET_COUNTER(&htim6, 0u);
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0u;

    /* 5. restart DMA from the top of the buffer; the ADC now waits for TRGO */
    s_laps = 0u;
    s_fault = false;
    return HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_buf, ACQ_BUF_LEN) == HAL_OK;
}

float acq_hw_vdda_mv(void)
{
    return s_vdda_mv;
}

bool acq_hw_take_fault(void)
{
    if (!s_fault) {
        return false;
    }
    s_fault = false;
    return true;
}

/* ---- ISR hooks: bookkeeping only ------------------------------------------ */

void acq_hw_isr_dma_half(void)
{
    g_acq_hw_stats.dma_events++;
    DBG2_TOGGLE();
}

void acq_hw_isr_dma_full(void)
{
    s_laps++;
    g_acq_hw_stats.dma_events++;
    DBG2_TOGGLE();
}

void acq_hw_isr_error(uint32_t hal_error_code)
{
    if ((hal_error_code & HAL_ADC_ERROR_OVR) != 0u) {
        g_acq_hw_stats.adc_overruns++;
    } else {
        g_acq_hw_stats.adc_errors++;
    }
    s_fault = true;
}
