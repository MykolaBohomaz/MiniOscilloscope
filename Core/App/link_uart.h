/**
 * @file    link_uart.h
 * @brief   Packet transport over USART2 (ST-LINK virtual COM port).
 *
 * RX: circular DMA into a ring; the main loop reads the DMA write index and
 *     feeds new bytes to the protocol parser (no per-byte interrupts).
 * TX: a small queue of pre-framed packets, sent one at a time with DMA.
 *     Producers never block: if no slot is free the caller decides whether
 *     to drop (live data) or retry later (capture chunks).
 *
 * On the custom PCB the same interface is backed by USB CDC (roadmap 11.2).
 */
#ifndef LINK_UART_H
#define LINK_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "protocol.h"

typedef struct {
    uint32_t tx_packets;
    uint32_t tx_dropped;     /**< send refused: queue full           */
    uint32_t tx_queue_hwm;   /**< queue high-water mark              */
    uint32_t rx_bytes;
    uint32_t rx_restarts;    /**< RX DMA restarted after UART error  */
    uint32_t hw_crc_mismatch;/**< boot self-test of the CRC unit     */
} link_stats_t;

extern link_stats_t g_link_stats;

void link_init(void);

/** CRC-32 (zlib) computed by the STM32 CRC unit; proto_crc_fn compatible. */
uint32_t link_crc_hw(const uint8_t *data, size_t len);
/** Same, over two segments (e.g. a wrapped region of the ADC ring). */
uint32_t link_crc_hw_2seg(const uint8_t *a, size_t na, const uint8_t *b, size_t nb);

/** Pump RX bytes into the parser; calls @p on_frame for each valid frame. */
void link_poll_rx(proto_parser_t *parser, void (*on_frame)(const proto_frame_t *f));

/** Start the next queued TX packet if the UART is idle. */
void link_poll_tx(void);

/** Free TX slots right now. */
uint32_t link_tx_free(void);

/**
 * Reserve a slot for a packet with @p len payload bytes.
 * @return pointer to the payload area, or NULL if the queue is full.
 * Must be followed by link_tx_commit() before the next begin.
 */
uint8_t *link_tx_begin(uint8_t type, uint8_t seq, uint16_t len);
void     link_tx_commit(void);

/** Convenience: frame and queue a complete payload. */
bool link_send(uint8_t type, uint8_t seq, const void *payload, uint16_t len);

/* ISR hooks */
void link_isr_tx_complete(void);
void link_isr_error(void);

#endif /* LINK_UART_H */
