/**
 ******************************************************************************
 * nvm.c - Non-volatile storage in the last 28 KB of flash (STM32L4P5)
 *
 * HAL version (converted from the register-level implementation).
 *
 * Requirements:
 *   - HAL_FLASH_MODULE_ENABLED in stm32l4xx_hal_conf.h
 *   - stm32l4xx_hal_flash.c, _flash_ex.c, _flash_ramfunc.c in the build
 *   - HAL_Init() called and SysTick running: HAL's FLASH_WaitForLastOperation()
 *     times out using HAL_GetTick(). See the note on interrupts below.
 *
 * IMPORTANT - dual bank: this assumes the DBANK option bit is SET, giving
 * 2 x 128 pages of 4 KB. With DBANK = 0 the L4P5 has 8 KB pages and every
 * page number here is wrong. HAL's FLASH_PAGE_SIZE macro is the 4 KB
 * (dual-bank) value, so a build-time check is included below.
 ******************************************************************************
 */

#include "nvm.h"

#include "stm32l4xx_hal.h"

#include <string.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Geometry                                                                  */
/* ------------------------------------------------------------------------- */

/* FLASH_BASE and FLASH_PAGE_SIZE come from CMSIS / HAL - do not redefine. */
#define NVM_FLASH_BASE_ADDR     FLASH_BASE          /* 0x08000000 */

#ifdef FLASH_PAGE_NB
  #define NVM_PAGES_PER_BANK    FLASH_PAGE_NB       /* 128 in dual-bank mode */
#else
  #define NVM_PAGES_PER_BANK    128U
#endif

#define NVM_TOTAL_PAGES         (NVM_PAGES_PER_BANK * 2U)   /* 256 = 1 MB */
#define NVM_DWORD_SIZE          8U                  /* min write unit       */

#define NVM_SIZE                28672U              /* 7 pages              */
#define NVM_START_ADDR          (NVM_FLASH_BASE_ADDR + \
                                 ((FLASH_PAGE_SIZE * NVM_TOTAL_PAGES) - NVM_SIZE))

/* NVM must start on a page boundary or the erase/copy logic misbehaves. */
_Static_assert((NVM_SIZE % FLASH_PAGE_SIZE) == 0U,
               "NVM_SIZE must be a whole number of flash pages");

/* Page-sized scratch buffer, 64-bit aligned for HAL_FLASH_Program. */
static uint64_t flash_tmp_aligned[FLASH_PAGE_SIZE / 8];
#define flash_tmp ((uint8_t *)flash_tmp_aligned)

static uint32_t nvm_last_error;     /* HAL_FLASH_GetError() of last failure */

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static inline uint32_t flash_page_to_address(uint32_t page)
{
    return (page * FLASH_PAGE_SIZE) + NVM_FLASH_BASE_ADDR;
}

static inline uint32_t flash_address_to_page(uint32_t address)
{
    return (address - NVM_FLASH_BASE_ADDR) / FLASH_PAGE_SIZE;
}

/* HAL's FLASH_FlushCaches() is static inside stm32l4xx_hal_flash_ex.c, so we
 * keep our own copy. HAL_FLASHEx_Erase already flushes after an erase; this
 * covers the programming phase. */
static void nvm_flush_caches(void)
{
    if (READ_BIT(FLASH->ACR, FLASH_ACR_DCEN) != 0U) {
        __HAL_FLASH_DATA_CACHE_DISABLE();
        __HAL_FLASH_DATA_CACHE_RESET();
        __HAL_FLASH_DATA_CACHE_ENABLE();
    }
    if (READ_BIT(FLASH->ACR, FLASH_ACR_ICEN) != 0U) {
        __HAL_FLASH_INSTRUCTION_CACHE_DISABLE();
        __HAL_FLASH_INSTRUCTION_CACHE_RESET();
        __HAL_FLASH_INSTRUCTION_CACHE_ENABLE();
    }
}

/* Erase one absolute page (0..255), picking the bank automatically. */
static uint8_t flash_erase_page(uint32_t page)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0xFFFFFFFFU;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.NbPages   = 1U;

    if (page >= NVM_PAGES_PER_BANK) {
        erase.Banks = FLASH_BANK_2;
        erase.Page  = page - NVM_PAGES_PER_BANK;
    } else {
        erase.Banks = FLASH_BANK_1;
        erase.Page  = page;
    }

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        nvm_last_error = HAL_FLASH_GetError();
        return 3U;
    }

    return 0U;
}

/* Program one full page from the 64-bit aligned scratch buffer. */
static uint8_t flash_program_page(uint32_t page_addr, const uint64_t *src)
{
    for (uint32_t i = 0U; i < (FLASH_PAGE_SIZE / NVM_DWORD_SIZE); i++) {
        /* HAL_FLASH_Program does everything the old memcpy_flash() did by
         * hand: waits for BSY, sets PG, issues the two 32-bit writes with an
         * __ISB() between them (so the compiler cannot emit STRD), waits for
         * completion, clears EOP and clears PG. */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              page_addr + (i * NVM_DWORD_SIZE),
                              src[i]) != HAL_OK) {
            nvm_last_error = HAL_FLASH_GetError();
            return 4U;
        }
    }

    return 0U;
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void NVM_Init(void)
{
    /* HAL clears the error flags itself at the start of each operation
     * (FLASH_WaitForLastOperation), so there is nothing to do here. Kept for
     * API compatibility with the register version. */
    nvm_last_error = 0U;
}

uint8_t NVM_Write(uint32_t address, const uint8_t *data, uint16_t length)
{
    uint8_t status = 0U;

    if ((data == NULL) || (length == 0U)) {
        return 1U;
    }
    if (((uint32_t)address + (uint32_t)length) > NVM_SIZE) {
        return 1U;
    }

    address += NVM_START_ADDR;

    uint32_t firstPage = flash_address_to_page(address);
    uint32_t lastPage  = flash_address_to_page(address + length - 1U);

    if (HAL_FLASH_Unlock() != HAL_OK) {
        return 2U;
    }

    for (uint32_t i = firstPage; i <= lastPage; i++) {
        uint32_t pageAddr = flash_page_to_address(i);
        uint32_t delta    = address - pageAddr;
        uint32_t lengthCopy;

        if ((address + length) > (pageAddr + FLASH_PAGE_SIZE)) {
            lengthCopy = pageAddr + FLASH_PAGE_SIZE - address;
        } else {
            lengthCopy = length;
        }

        /* Read-modify-write the whole page through the scratch buffer. */
        memcpy(flash_tmp, (const void *)pageAddr, FLASH_PAGE_SIZE);
        memcpy(flash_tmp + delta, data, lengthCopy);

        status = flash_erase_page(i);
        if (status != 0U) {
            goto cleanup;
        }

        status = flash_program_page(pageAddr, flash_tmp_aligned);
        if (status != 0U) {
            goto cleanup;
        }

        address = pageAddr + FLASH_PAGE_SIZE;
        data   += lengthCopy;
        length -= (uint16_t)lengthCopy;
    }

cleanup:
    HAL_FLASH_Lock();
    nvm_flush_caches();

    return status;
}

uint8_t NVM_Read(uint32_t address, uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (((uint32_t)address + (uint32_t)length) > NVM_SIZE)) {
        return 1U;
    }

    memcpy(data, (const void *)(NVM_START_ADDR + address), length);

    return 0U;
}

/* HAL_FLASH_ERROR_* bitmask from the last failed operation, 0 if none. */
uint32_t NVM_GetLastError(void)
{
    return nvm_last_error;
}