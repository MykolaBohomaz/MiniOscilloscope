/**
 * @file    protocol.h
 * @brief   Binary framing shared by the firmware and tools/scope (Python).
 *
 *   offset  size  field
 *   0       1     0xA5  sync
 *   1       1     0x5A  sync
 *   2       1     type  (see msg_type_t)
 *   3       1     seq   (echoed in replies; free-running for unsolicited msgs)
 *   4       2     len   payload length, little-endian, <= LINK_MAX_PAYLOAD
 *   6       len   payload (little-endian fields, no padding)
 *   6+len   4     CRC-32 (zlib) over bytes [2, 6+len), little-endian
 *
 * The full specification (payload layouts) is in docs/protocol.md.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "scope_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PROTO_SYNC0        0xA5u
#define PROTO_SYNC1        0x5Au
#define PROTO_HDR_LEN      6u
#define PROTO_CRC_LEN      4u
#define PROTO_OVERHEAD     (PROTO_HDR_LEN + PROTO_CRC_LEN)
#define PROTO_FRAME_MAX    (PROTO_OVERHEAD + LINK_MAX_PAYLOAD)

typedef enum {
    /* host -> device */
    MSG_PING          = 0x01,
    MSG_GET_INFO      = 0x02,
    MSG_GET_STATUS    = 0x03,
    MSG_SET_RUN       = 0x10,  /* u8 run: 0 stop, 1 run, 2 single            */
    MSG_SET_TIMEBASE  = 0x11,  /* u8 index                                   */
    MSG_SET_TRIGGER   = 0x12,  /* u16 level, u16 hyst, u8 edge, u8 mode, u8 pre% */
    MSG_SET_SIGGEN    = 0x14,  /* u8 wave, u8 freq index                      */
    MSG_GET_CAPTURE   = 0x20,  /* u8 flags (bit0: take the current record)    */
    MSG_GET_MEAS      = 0x21,
    MSG_CAL_GET       = 0x30,  /* u8 range                                    */
    MSG_CAL_SET       = 0x31,  /* u8 range, f32 gain, f32 offset_mv           */
    MSG_CAL_SAVE      = 0x32,
    MSG_RESET_STATS   = 0x3F,

    /* device -> host */
    MSG_ACK           = 0x80,  /* u8 cmd, u8 status                           */
    MSG_INFO          = 0x81,
    MSG_STATUS        = 0x82,
    MSG_MEAS          = 0x83,
    MSG_CAL           = 0x84,
    MSG_PONG          = 0x8F,  /* echo of MSG_PING payload                   */
    MSG_CAPTURE_HDR   = 0x90,
    MSG_CAPTURE_DATA  = 0x91,
    MSG_LOG           = 0x9F,
} msg_type_t;

typedef enum {
    ACK_OK = 0,
    ACK_BAD_LEN = 1,
    ACK_BAD_ARG = 2,
    ACK_UNKNOWN = 3,
    ACK_BUSY = 4,
    ACK_FAILED = 5,
} ack_status_t;

/* ---- decoder --------------------------------------------------------------- */

typedef struct {
    uint8_t  type;
    uint8_t  seq;
    uint16_t len;
    uint8_t  payload[LINK_MAX_PAYLOAD];
} proto_frame_t;

typedef struct {
    uint32_t frames_ok;
    uint32_t crc_errors;
    uint32_t len_errors;
    uint32_t sync_skips;    /**< bytes discarded while hunting for sync */
} proto_rx_stats_t;

typedef struct {
    uint8_t          state;
    uint16_t         idx;
    uint8_t          hdr[4];
    uint8_t          crc[4];
    proto_frame_t    frame;
    proto_rx_stats_t stats;
} proto_parser_t;

void proto_parser_init(proto_parser_t *p);

/** Feed one byte. @return true when p->frame holds a complete, CRC-valid frame. */
bool proto_parser_feed(proto_parser_t *p, uint8_t byte);

/* ---- encoder --------------------------------------------------------------- */

typedef uint32_t (*proto_crc_fn)(const uint8_t *data, size_t len);

/** Software CRC, suitable as a proto_crc_fn. */
uint32_t proto_crc_sw(const uint8_t *data, size_t len);

/**
 * Write the header for a frame with @p len payload bytes at dst[0..5]; the
 * caller writes the payload at dst + PROTO_HDR_LEN, then calls proto_finish.
 */
void proto_begin(uint8_t *dst, uint8_t type, uint8_t seq, uint16_t len);

/** Append the CRC. @return total frame length. */
size_t proto_finish(uint8_t *dst, uint16_t len, proto_crc_fn crc);

/** One-shot encode. @return frame length, or 0 if it does not fit in @p cap. */
size_t proto_encode(uint8_t *dst, size_t cap, uint8_t type, uint8_t seq,
                    const void *payload, uint16_t len, proto_crc_fn crc);

/* ---- little-endian field helpers ------------------------------------------- */

static inline uint8_t *put_u8(uint8_t *p, uint8_t v)   { *p = v; return p + 1; }
static inline uint8_t *put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2;
}
static inline uint8_t *put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); return p + 4;
}
static inline uint8_t *put_f32(uint8_t *p, float f)
{
    uint32_t v; memcpy(&v, &f, 4); return put_u32(p, v);
}
static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static inline uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline float get_f32(const uint8_t *p)
{
    uint32_t v = get_u32(p); float f; memcpy(&f, &v, 4); return f;
}

#ifdef __cplusplus
}
#endif
#endif /* PROTOCOL_H */
