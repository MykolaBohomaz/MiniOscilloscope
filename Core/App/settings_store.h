/**
 * @file    settings_store.h
 * @brief   Versioned, CRC-protected persistence of settings + calibration in
 *          the last 2 KB flash page (reserved in STM32L432KCUX_FLASH.ld).
 */
#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdbool.h>
#include "calib.h"
#include "scope_settings.h"

#define SETTINGS_FLASH_ADDR   0x0803F800u
#define SETTINGS_FLASH_PAGE   127u
#define SETTINGS_MAGIC        0x53434F50u   /* "SCOP" */
#define SETTINGS_VERSION      1u

/**
 * Called first thing in NMI_Handler. A power loss during a save can leave a
 * double-word with a 2-bit ECC error; reading it raises an NMI. If the error
 * is inside the settings page, the flag is cleared, the load is marked failed
 * and the handler returns true (NMI handled). Other NMIs return false.
 */
bool settings_store_nmi(void);

/** @return true if a valid record was found (outputs untouched otherwise). */
bool settings_load(scope_settings_t *s, range_cal_t cal[RANGE_COUNT]);
bool settings_save(const scope_settings_t *s, const range_cal_t cal[RANGE_COUNT]);

#endif /* SETTINGS_STORE_H */
