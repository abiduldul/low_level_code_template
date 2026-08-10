#include "nvm.h"
#include "stm32l4p5xx.h"
#include <string.h>

#define FLASH_BASE_ADDR         0x08000000UL    /**< Start of flash memory      */
#define FLASH_PAGE_SIZE         4096U           /**< Bytes per page (4 KB)      */
#define FLASH_BANK_PAGES        128U            /**< Pages per bank (1MB Dual Bank) */
#define FLASH_TOTAL_PAGES       256U            /**< Total pages (both banks)   */
#define FLASH_DWORD_SIZE        8U              /**< Minimum write unit (bytes) */

#define FLASH_KEY1              0x45670123UL
#define FLASH_KEY2              0xCDEF89ABUL
 
#define FLASH_OPTKEY1           0x08192A3BUL
#define FLASH_OPTKEY2           0x4C5D6E7FUL

#define NVM_SIZE                28672
// Now accurately calculates the end of the 1MB flash space
#define NVM_START_ADDR          (FLASH_BASE_ADDR + ((FLASH_PAGE_SIZE * FLASH_TOTAL_PAGES) - NVM_SIZE))

/* Force 64-bit alignment to prevent Cortex-M4 unaligned access faults */
static uint64_t flash_tmp_aligned[FLASH_PAGE_SIZE / 8];
#define flash_tmp ((uint8_t*)flash_tmp_aligned)

static void flash_clear_errors(void);
static void flash_lock(void);
static uint8_t flash_unlock(void);
static uint8_t flash_wait_ready(void);
static uint8_t flash_erase_page(uint8_t page);
static uint8_t flash_check_errors(void);

static inline uint32_t flash_page_to_address(uint8_t page) {
    return (page * FLASH_PAGE_SIZE) + FLASH_BASE_ADDR;
}
static inline uint8_t flash_address_to_page(uint32_t address) {
    return (address - FLASH_BASE_ADDR) / FLASH_PAGE_SIZE;
}

void NVM_Init() {
    flash_clear_errors();
}

uint8_t memcpy_flash(volatile uint32_t* dst, const uint32_t* src, uint16_t length) {
    if(length > FLASH_PAGE_SIZE) return 1;

    uint8_t status;
    size_t dwords = length / FLASH_DWORD_SIZE;
    
    uint32_t destAddr = (uint32_t)dst;

    for(size_t i = 0; i < dwords; i++) {
        while(FLASH->SR & FLASH_SR_BSY);
        flash_clear_errors();

        // Extract values beforehand to minimize delays while interrupts are disabled
        uint32_t data0 = src[0];
        uint32_t data1 = src[1];

        // CRITICAL: Disable IRQ. If an interrupt executes code from flash 
        // between these two writes, the sequence breaks and sets PGSERR.
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        
        // CRITICAL: Explicit 32-bit volatile writes separated by an ISB.
        // This prevents the compiler from merging this into a 64-bit STRD instruction.
        *(__IO uint32_t*)destAddr = data0;
        __ISB(); 
        *(__IO uint32_t*)(destAddr + 4) = data1;
        
        __set_PRIMASK(primask);

        status = flash_wait_ready();
        if(status != 0) return status;

        if(FLASH->SR & FLASH_SR_EOP) {
            FLASH->SR = FLASH_SR_EOP;
        }

        destAddr += 8;
        src += 2;
    }

    return 0;
}

uint8_t NVM_Write(uint32_t address, const uint8_t* data, uint16_t length) {
    if(address + length > NVM_SIZE) return 1;

    address += NVM_START_ADDR;
    uint8_t status = 0;

    uint32_t firstPage = flash_address_to_page(address);
    uint32_t lastPage = flash_address_to_page(address + length - 1);

    if(flash_unlock() != 0) {
        return 2;
    }

    while(FLASH->SR & FLASH_SR_BSY);
    flash_clear_errors();

    for(uint32_t i = firstPage; i <= lastPage; i++) {
        uint32_t pageAddr = flash_page_to_address(i);
        uint32_t delta = address - pageAddr;
        uint32_t lengthCopy;

        if(address + length > pageAddr + FLASH_PAGE_SIZE) {
            lengthCopy = pageAddr + FLASH_PAGE_SIZE - address;
        } else {
            lengthCopy = length;
        }

        memcpy(flash_tmp, (const void*)pageAddr, FLASH_PAGE_SIZE);
        memcpy(flash_tmp + delta, data, lengthCopy);

        if(flash_erase_page(i) != 0) {
            status = 3;
            goto cleanup;
        }

        FLASH->CR |= FLASH_CR_PG;

        if(memcpy_flash((volatile uint32_t*)pageAddr, 
                        (const uint32_t*)flash_tmp_aligned, FLASH_PAGE_SIZE) != 0) {
            status = 4;
            goto cleanup;
        }

        FLASH->CR &= ~FLASH_CR_PG;

        address = pageAddr + FLASH_PAGE_SIZE;
        data += lengthCopy;
        length -= lengthCopy;
    }
    
cleanup:
    FLASH->CR &= ~FLASH_CR_PG;
    flash_lock();

    // Invalidate caches to prevent reading stale memory on Cortex-M4
    if (FLASH->ACR & FLASH_ACR_DCEN) {
        FLASH->ACR &= ~FLASH_ACR_DCEN;
        FLASH->ACR |= FLASH_ACR_DCRST;
        FLASH->ACR &= ~FLASH_ACR_DCRST;
        FLASH->ACR |= FLASH_ACR_DCEN;
    }
    if (FLASH->ACR & FLASH_ACR_ICEN) {
        FLASH->ACR &= ~FLASH_ACR_ICEN;
        FLASH->ACR |= FLASH_ACR_ICRST;
        FLASH->ACR &= ~FLASH_ACR_ICRST;
        FLASH->ACR |= FLASH_ACR_ICEN;
    }

    return status;
}

uint8_t NVM_Read(uint32_t address, uint8_t* data, uint16_t length) {
    if(address + length > NVM_SIZE) return 1;

    address += NVM_START_ADDR;
    memcpy(data, (const void*)address, length);

    return 0;
}

static void flash_clear_errors() {
    FLASH->SR = FLASH_SR_PROGERR
              | FLASH_SR_PGAERR
              | FLASH_SR_PGSERR
              | FLASH_SR_WRPERR
              | FLASH_SR_MISERR
              | FLASH_SR_FASTERR
              | FLASH_SR_OPERR
              | FLASH_SR_EOP;
}

static void flash_lock() {
    FLASH->CR |= FLASH_CR_LOCK;
}

static uint8_t flash_unlock() {
    if(!(FLASH->CR & FLASH_CR_LOCK)) return 0;

    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;

    if(FLASH->CR & FLASH_CR_LOCK) return 1;

    return 0;
}

static uint8_t flash_wait_ready() {
    uint32_t timeout = 6000000;

    while(FLASH->SR & FLASH_SR_BSY) {
        if(timeout-- == 0) return 5;
    }

    return flash_check_errors();
}

static uint8_t flash_erase_page(uint8_t page) {
    while(FLASH->SR & FLASH_SR_BSY);
    flash_clear_errors();

    uint32_t cr = FLASH->CR;
    cr &= ~(FLASH_CR_PNB_Msk | FLASH_CR_BKER | FLASH_CR_MER1 | FLASH_CR_MER2 | FLASH_CR_PG);

    // BKER bit dynamically applies Bank 2 when page exceeds 127
    if (page >= FLASH_BANK_PAGES) {
        cr |= FLASH_CR_BKER;
        page -= FLASH_BANK_PAGES;
    }

    cr |= ((page << FLASH_CR_PNB_Pos) & FLASH_CR_PNB_Msk);
    cr |= FLASH_CR_PER;
    
    FLASH->CR = cr;
    FLASH->CR |= FLASH_CR_STRT;

    uint8_t status = flash_wait_ready();

    FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB_Msk | FLASH_CR_BKER);

    return status;
}

static uint8_t flash_check_errors(void) {
    uint32_t sr = FLASH->SR;
 
    if (sr & (FLASH_SR_PROGERR | FLASH_SR_PGAERR | FLASH_SR_PGSERR | FLASH_SR_FASTERR)) {
        return sr; // Returns the exact register for easier debugging (e.g. 168)
    }
    if (sr & FLASH_SR_WRPERR) {
        return sr;
    }
    if (sr & (FLASH_SR_MISERR | FLASH_SR_OPERR)) {
        return sr;
    }
 
    return 0;
}