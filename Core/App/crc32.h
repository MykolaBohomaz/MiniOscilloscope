/**
 * @file    crc32.h
 * @brief   CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF).
 *
 * Bit-exact with zlib.crc32() / binascii.crc32() in Python, which is what the
 * host utility uses. Check value: crc32("123456789") == 0xCBF43926.
 *
 * On target, the STM32 CRC peripheral is configured to produce the same value
 * (see link_uart.c); this software version is used on the host, in unit tests,
 * and as a boot-time cross-check of the hardware unit.
 */
#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start / continue a CRC. Pass 0 as @p crc for a fresh computation. */
uint32_t crc32_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t crc32_compute(const void *data, size_t len)
{
    return crc32_update(0u, data, len);
}

#ifdef __cplusplus
}
#endif
#endif /* CRC32_H */
