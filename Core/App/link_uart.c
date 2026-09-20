/**
 * @file    link_uart.c
 */
#include "link_uart.h"
#include "crc.h"
#include "crc32.h"
#include "usart.h"
#include <string.h>

link_stats_t g_link_stats;

static uint8_t  s_rx_ring[LINK_RX_RING_LEN];
static uint16_t s_rx_tail;

typedef struct {
    uint16_t len;
    uint8_t  data[PROTO_FRAME_MAX];
} tx_slot_t;

static tx_slot_t s_slots[LINK_TX_SLOTS];
static uint8_t   s_head;         /* next slot to fill           */
static uint8_t   s_tail;         /* slot being / to be sent     */
static uint8_t   s_count;        /* committed slots             */
static bool      s_reserved;     /* begin() called, not committed */
static uint16_t  s_reserved_len;
static volatile bool s_tx_busy;
static volatile bool s_tx_done;
static volatile bool s_rx_restart;
static uint32_t      s_tx_start_ms;

/* A packet of at most PROTO_FRAME_MAX bytes takes ~11 ms at 921600 Bd; if the
 * TX-complete interrupt has not arrived after this long, abort and move on. */
#define TX_TIMEOUT_MS 250u

/* ---- hardware CRC ---------------------------------------------------------- */

static void crc_unit_config(void)
{
    /* Reflected in/out + init 0xFFFFFFFF == zlib CRC-32 before the final XOR. */
    hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
    hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
    hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_BYTE;
    hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
    hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
    if (HAL_CRC_Init(&hcrc) != HAL_OK) {
        Error_Handler();
    }
}

uint32_t link_crc_hw(const uint8_t *data, size_t len)
{
    return ~HAL_CRC_Calculate(&hcrc, (uint32_t *)(uintptr_t)data, (uint32_t)len);
}

uint32_t link_crc_hw_2seg(const uint8_t *a, size_t na, const uint8_t *b, size_t nb)
{
    uint32_t c = HAL_CRC_Calculate(&hcrc, (uint32_t *)(uintptr_t)a, (uint32_t)na);
    if (nb != 0u) {
        c = HAL_CRC_Accumulate(&hcrc, (uint32_t *)(uintptr_t)b, (uint32_t)nb);
    }
    return ~c;
}

/* ---- init -------------------------------------------------------------------- */

static void rx_start(void)
{
    s_rx_tail = 0u;
    (void)HAL_UARTEx_ReceiveToIdle_DMA(&huart2, s_rx_ring, LINK_RX_RING_LEN);
    /* The main loop polls the DMA index; the half-transfer IRQ adds nothing. */
    __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
}

void link_init(void)
{
    crc_unit_config();
    static const char check[] = "123456789";
    if (link_crc_hw((const uint8_t *)check, 9u) != crc32_compute(check, 9u)) {
        g_link_stats.hw_crc_mismatch++;
    }
    rx_start();
}

/* ---- RX ------------------------------------------------------------------ */

void link_poll_rx(proto_parser_t *parser, void (*on_frame)(const proto_frame_t *f))
{
    if (s_rx_restart) {
        s_rx_restart = false;
        (void)HAL_UART_AbortReceive(&huart2);
        g_link_stats.rx_restarts++;
        rx_start();
        return;
    }
    const uint16_t head = (uint16_t)((LINK_RX_RING_LEN -
                          __HAL_DMA_GET_COUNTER(huart2.hdmarx)) % LINK_RX_RING_LEN);
    while (s_rx_tail != head) {
        const uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) % LINK_RX_RING_LEN);
        g_link_stats.rx_bytes++;
        if (proto_parser_feed(parser, b)) {
            on_frame(&parser->frame);
        }
    }
}

/* ---- TX ------------------------------------------------------------------ */

uint32_t link_tx_free(void)
{
    return LINK_TX_SLOTS - s_count - (s_reserved ? 1u : 0u);
}

uint8_t *link_tx_begin(uint8_t type, uint8_t seq, uint16_t len)
{
    if (s_reserved || len > LINK_MAX_PAYLOAD || link_tx_free() == 0u) {
        g_link_stats.tx_dropped++;
        return NULL;
    }
    tx_slot_t *s = &s_slots[s_head];
    proto_begin(s->data, type, seq, len);
    s_reserved = true;
    s_reserved_len = len;
    return &s->data[PROTO_HDR_LEN];
}

void link_tx_commit(void)
{
    if (!s_reserved) {
        return;
    }
    tx_slot_t *s = &s_slots[s_head];
    s->len = (uint16_t)proto_finish(s->data, s_reserved_len, link_crc_hw);
    s_reserved = false;
    s_head = (uint8_t)((s_head + 1u) % LINK_TX_SLOTS);
    s_count++;
    if (s_count > g_link_stats.tx_queue_hwm) {
        g_link_stats.tx_queue_hwm = s_count;
    }
}

bool link_send(uint8_t type, uint8_t seq, const void *payload, uint16_t len)
{
    uint8_t *p = link_tx_begin(type, seq, len);
    if (p == NULL) {
        return false;
    }
    if (len != 0u) {
        memcpy(p, payload, len);
    }
    link_tx_commit();
    return true;
}

void link_poll_tx(void)
{
    if (s_tx_done) {                     /* retire the slot the ISR finished */
        s_tx_done = false;
        s_tail = (uint8_t)((s_tail + 1u) % LINK_TX_SLOTS);
        s_count--;
        s_tx_busy = false;
        g_link_stats.tx_packets++;
    }
    if (s_tx_busy && (HAL_GetTick() - s_tx_start_ms) > TX_TIMEOUT_MS) {
        (void)HAL_UART_AbortTransmit(&huart2);
        g_link_stats.tx_dropped++;
        s_tx_done = true;                /* retire it on the next pass */
        return;
    }
    if (!s_tx_busy && s_count > 0u) {
        tx_slot_t *s = &s_slots[s_tail];
        s_tx_busy = true;
        s_tx_start_ms = HAL_GetTick();
        if (HAL_UART_Transmit_DMA(&huart2, s->data, s->len) != HAL_OK) {
            s_tx_busy = false;           /* retry next pass */
        }
    }
}

void link_isr_tx_complete(void)
{
    s_tx_done = true;
}

void link_isr_error(void)
{
    s_rx_restart = true;
}
