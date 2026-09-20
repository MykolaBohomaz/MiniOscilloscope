#include <string.h>
#include "unit.h"
#include "crc32.h"
#include "protocol.h"

static void test_crc_check_value(void)
{
    CHECK_EQ_U(crc32_compute("123456789", 9), 0xCBF43926u);
    CHECK_EQ_U(crc32_compute("", 0), 0u);
    /* incremental == one shot */
    uint32_t c = crc32_update(0, "1234", 4);
    c = crc32_update(c, "56789", 5);
    CHECK_EQ_U(c, 0xCBF43926u);
}

/* Golden frame, also asserted by tools/tests/test_protocol.py so the C and
 * Python implementations cannot drift apart. PING seq=7 payload "hi". */
static const uint8_t k_golden_ping[] = {
    0xA5, 0x5A, 0x01, 0x07, 0x02, 0x00, 'h', 'i', 0xCE, 0x5E, 0xFD, 0xFB
};

static void test_golden_frame(void)
{
    uint8_t buf[64];
    const size_t n = proto_encode(buf, sizeof buf, MSG_PING, 7, "hi", 2, proto_crc_sw);
    CHECK_EQ_U(n, sizeof k_golden_ping);
    CHECK(memcmp(buf, k_golden_ping, n) == 0);
}

static int feed_all(proto_parser_t *p, const uint8_t *d, size_t n, proto_frame_t *last)
{
    int frames = 0;
    for (size_t i = 0; i < n; i++) {
        if (proto_parser_feed(p, d[i])) {
            frames++;
            *last = p->frame;
        }
    }
    return frames;
}

static void test_roundtrip_and_resync(void)
{
    uint8_t stream[3 * PROTO_FRAME_MAX];
    size_t n = 0;
    const uint8_t junk[] = { 0x00, 0xA5, 0x13, 0x5A, 0xFF };
    memcpy(stream, junk, sizeof junk);
    n += sizeof junk;
    uint8_t payload[LINK_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof payload; i++) payload[i] = (uint8_t)(i * 7u);
    n += proto_encode(stream + n, sizeof stream - n, MSG_CAPTURE_DATA, 42,
                      payload, LINK_MAX_PAYLOAD, proto_crc_sw);
    stream[n++] = 0xA5;                       /* "A5 A5 5A" must still sync */
    n += proto_encode(stream + n, sizeof stream - n, MSG_GET_STATUS, 3, NULL, 0, proto_crc_sw);

    proto_parser_t p;
    proto_frame_t f;
    proto_parser_init(&p);
    CHECK_EQ_U(feed_all(&p, stream, n, &f), 2);
    CHECK_EQ_U(f.type, MSG_GET_STATUS);
    CHECK_EQ_U(f.seq, 3);
    CHECK_EQ_U(f.len, 0);
    CHECK_EQ_U(p.stats.frames_ok, 2);
    CHECK_EQ_U(p.stats.crc_errors, 0);
}

static void test_corruption_detected(void)
{
    uint8_t buf[64];
    const size_t n = proto_encode(buf, sizeof buf, MSG_SET_TIMEBASE, 1, "\x05", 1, proto_crc_sw);
    proto_parser_t p;
    proto_frame_t f;
    for (size_t bit = 16; bit < n * 8; bit++) {       /* flip every bit after sync */
        uint8_t c[64];
        memcpy(c, buf, n);
        c[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        proto_parser_init(&p);
        const int frames = feed_all(&p, c, n, &f);
        CHECK_EQ_U(frames, 0);
    }
    /* oversize length field */
    const uint8_t bad[] = { 0xA5, 0x5A, 0x01, 0x00, 0xFF, 0xFF };
    proto_parser_init(&p);
    CHECK_EQ_U(feed_all(&p, bad, sizeof bad, &f), 0);
    CHECK_EQ_U(p.stats.len_errors, 1);
}

static void test_encode_bounds(void)
{
    uint8_t small[8];
    CHECK_EQ_U(proto_encode(small, sizeof small, MSG_PING, 0, "abc", 3, proto_crc_sw), 0);
    uint8_t p[4];
    put_u32(p, 0x11223344u);
    CHECK_EQ_U(p[0], 0x44);
    CHECK_EQ_U(get_u32(p), 0x11223344u);
    put_f32(p, 1.5f);
    CHECK(get_f32(p) == 1.5f);
}

int main(void)
{
    RUN(test_crc_check_value);
    RUN(test_golden_frame);
    RUN(test_roundtrip_and_resync);
    RUN(test_corruption_detected);
    RUN(test_encode_bounds);
    UNIT_REPORT();
}
