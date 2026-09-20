/**
 * @file    settings_store.c
 *
 * Record layout (padded to a multiple of 8 bytes for double-word programming):
 *   magic | version | size | settings | calibration[RANGE_COUNT] | crc32
 * The CRC covers everything before it; a blank (0xFF) or partially written
 * page fails the check and defaults are used.
 */
#include "settings_store.h"
#include "crc32.h"
#include "main.h"
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t         magic;
    uint16_t         version;
    uint16_t         size;
    scope_settings_t settings;
    range_cal_t      cal[RANGE_COUNT];
    uint32_t         crc;
} settings_record_t;

#define RECORD_WORDS ((sizeof(settings_record_t) + 7u) / 8u)

static volatile bool s_ecc_fault;

bool settings_store_nmi(void)
{
    const uint32_t eccr = FLASH->ECCR;
    if ((eccr & FLASH_ECCR_ECCD) == 0u) {
        return false;
    }
    const uint32_t addr = FLASH_BASE + (eccr & FLASH_ECCR_ADDR_ECC);
    if (addr < SETTINGS_FLASH_ADDR || addr >= SETTINGS_FLASH_ADDR + FLASH_PAGE_SIZE) {
        return false;
    }
    FLASH->ECCR = eccr | FLASH_ECCR_ECCD;      /* write 1 to clear */
    s_ecc_fault = true;
    return true;
}

/** Erase the settings page. Flash must already be unlocked. */
static bool erase_page_unlocked(void)
{
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
    FLASH_EraseInitTypeDef e = {0};
    uint32_t bad_page = 0u;
    e.TypeErase = FLASH_TYPEERASE_PAGES;
    e.Banks = FLASH_BANK_1;
    e.Page = SETTINGS_FLASH_PAGE;
    e.NbPages = 1u;
    return HAL_FLASHEx_Erase(&e, &bad_page) == HAL_OK;
}

static void erase_page(void)
{
    if (HAL_FLASH_Unlock() == HAL_OK) {
        (void)erase_page_unlocked();
        (void)HAL_FLASH_Lock();
    }
}

bool settings_load(scope_settings_t *s, range_cal_t cal[RANGE_COUNT])
{
    settings_record_t r;
    s_ecc_fault = false;
    memcpy(&r, (const void *)SETTINGS_FLASH_ADDR, sizeof r);
    if (s_ecc_fault) {
        erase_page();              /* torn write: start clean next time */
        return false;
    }
    if (r.magic != SETTINGS_MAGIC || r.version != SETTINGS_VERSION ||
        r.size != sizeof r ||
        r.crc != crc32_compute(&r, offsetof(settings_record_t, crc))) {
        return false;
    }
    *s = r.settings;
    scope_settings_sanitize(s);
    memcpy(cal, r.cal, sizeof r.cal);
    return true;
}

bool settings_save(const scope_settings_t *s, const range_cal_t cal[RANGE_COUNT])
{
    uint64_t words[RECORD_WORDS];
    settings_record_t *r = (settings_record_t *)(void *)words;
    memset(words, 0xFF, sizeof words);
    r->magic = SETTINGS_MAGIC;
    r->version = SETTINGS_VERSION;
    r->size = (uint16_t)sizeof *r;
    r->settings = *s;
    memcpy(r->cal, cal, sizeof r->cal);
    r->crc = crc32_compute(r, offsetof(settings_record_t, crc));

    /* Erase takes ~22 ms during which the CPU and all ISRs stall (code runs
     * from the same flash bank). The caller stops sampling first; the 0.5 s
     * watchdog is not at risk. */
    bool ok = (HAL_FLASH_Unlock() == HAL_OK);
    if (ok) {
        ok = erase_page_unlocked();
        for (uint32_t i = 0; ok && i < RECORD_WORDS; i++) {
            ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                                    SETTINGS_FLASH_ADDR + 8u * i, words[i]) == HAL_OK);
        }
        (void)HAL_FLASH_Lock();
    }
    return ok;
}
