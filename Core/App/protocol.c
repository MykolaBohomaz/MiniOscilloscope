/**
 * @file    protocol.c
 */
#include "protocol.h"
#include "crc32.h"

enum { ST_SYNC0 = 0, ST_SYNC1, ST_HDR, ST_PAYLOAD, ST_CRC };

void proto_parser_init(proto_parser_t *p)
{
    memset(p, 0, sizeof *p);
    p->state = ST_SYNC0;
}

uint32_t proto_crc_sw(const uint8_t *data, size_t len)
{
    return crc32_compute(data, len);
}

static bool finish_frame(proto_parser_t *p)
{
    uint32_t crc = crc32_update(0u, p->hdr, sizeof p->hdr);
    crc = crc32_update(crc, p->frame.payload, p->frame.len);
    p->state = ST_SYNC0;
    if (crc != get_u32(p->crc)) {
        p->stats.crc_errors++;
        return false;
    }
    p->stats.frames_ok++;
    return true;
}

bool proto_parser_feed(proto_parser_t *p, uint8_t b)
{
    switch (p->state) {
    case ST_SYNC0:
        if (b == PROTO_SYNC0) {
            p->state = ST_SYNC1;
        } else {
            p->stats.sync_skips++;
        }
        return false;

    case ST_SYNC1:
        if (b == PROTO_SYNC1) {
            p->state = ST_HDR;
            p->idx = 0u;
        } else if (b != PROTO_SYNC0) {  /* "A5 A5 5A" must still sync */
            p->state = ST_SYNC0;
            p->stats.sync_skips++;
        }
        return false;

    case ST_HDR:
        p->hdr[p->idx++] = b;
        if (p->idx == sizeof p->hdr) {
            p->frame.type = p->hdr[0];
            p->frame.seq = p->hdr[1];
            p->frame.len = get_u16(&p->hdr[2]);
            p->idx = 0u;
            if (p->frame.len > LINK_MAX_PAYLOAD) {
                p->stats.len_errors++;
                p->state = ST_SYNC0;
            } else {
                p->state = (p->frame.len == 0u) ? ST_CRC : ST_PAYLOAD;
            }
        }
        return false;

    case ST_PAYLOAD:
        p->frame.payload[p->idx++] = b;
        if (p->idx == p->frame.len) {
            p->idx = 0u;
            p->state = ST_CRC;
        }
        return false;

    case ST_CRC:
        p->crc[p->idx++] = b;
        if (p->idx == sizeof p->crc) {
            return finish_frame(p);
        }
        return false;

    default:
        p->state = ST_SYNC0;
        return false;
    }
}

void proto_begin(uint8_t *dst, uint8_t type, uint8_t seq, uint16_t len)
{
    dst[0] = PROTO_SYNC0;
    dst[1] = PROTO_SYNC1;
    dst[2] = type;
    dst[3] = seq;
    put_u16(&dst[4], len);
}

size_t proto_finish(uint8_t *dst, uint16_t len, proto_crc_fn crc)
{
    const uint32_t c = crc(&dst[2], (size_t)4u + len);
    put_u32(&dst[PROTO_HDR_LEN + len], c);
    return (size_t)PROTO_OVERHEAD + len;
}

size_t proto_encode(uint8_t *dst, size_t cap, uint8_t type, uint8_t seq,
                    const void *payload, uint16_t len, proto_crc_fn crc)
{
    if (len > LINK_MAX_PAYLOAD || cap < (size_t)PROTO_OVERHEAD + len) {
        return 0u;
    }
    proto_begin(dst, type, seq, len);
    if (len != 0u) {
        memcpy(&dst[PROTO_HDR_LEN], payload, len);
    }
    return proto_finish(dst, len, crc);
}
