/**
 * @file    hal_callbacks.c
 * @brief   Every HAL callback the application uses, in one place.
 *
 * ISR rule (roadmap section 6): callbacks only acknowledge hardware, record
 * an index or timestamp, and set a flag. No DSP, formatting, drawing, packet
 * building or blocking calls here - the main loop does all of that.
 *
 *   priority 0  DMA1 Ch1 (ADC)       -> laps / event counters
 *   priority 1  ADC1                 -> overrun flag
 *   priority 2  USART2, DMA1 Ch6/7   -> TX slot done / RX error flag
 *   priority 3  SPI1, DMA1 Ch3       -> release LCD chip-select, clear busy
 *   priority 5  EXTI                 -> button timestamp
 */
#include "main.h"
#include "acq_hw.h"
#include "input.h"
#include "lcd_st75256.h"
#include "link_uart.h"

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        acq_hw_isr_dma_half();
    }
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        acq_hw_isr_dma_full();
    }
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1) {
        acq_hw_isr_error(hadc->ErrorCode);
    }
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        lcd_isr_tx_complete();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        lcd_isr_error();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        link_isr_tx_complete();
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    /* RX bytes are consumed by polling the DMA index in link_poll_rx();
     * nothing to do here beyond acknowledging the event. */
    (void)huart;
    (void)size;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        link_isr_error();
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
    input_isr_exti(pin);
}
