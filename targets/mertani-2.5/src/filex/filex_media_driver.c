#include "fx_api.h"
#include "tx_api.h"
#include "stm32l4xx_hal.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "log.h"

#define SECTOR_SIZE             512U
#define SD_TIMEOUT_MS           5000U
#define SD_READY_TIMEOUT_MS     2000U

/* FileX internal helper: parses an MBR and reports where the partition starts. */
extern UINT _fx_partition_offset_calculate(void *partition_sector, UINT partition,
                                           ULONG *partition_start, ULONG *partition_size);

static SD_HandleTypeDef hsd1;
static uint8_t          sd_initialised = 0;

/*
 * Where the FAT boot record actually lives.
 * 0 for a card formatted with no MBR (superfloppy), or the partition LBA for a
 * card with a partition table. Resolved during FX_DRIVER_BOOT_READ.
 */
static ULONG sd_partition_start = 0;

/*
 * HAL_SD_ReadBlocks/WriteBlocks move data through the FIFO as 32-bit words, so
 * the buffer must be 4-byte aligned. FileX sometimes hands the driver a pointer
 * straight into the caller's buffer and that pointer is not guaranteed aligned.
 */
static uint32_t sd_bounce[SECTOR_SIZE / 4];

#define SD_IS_ALIGNED(p)   ((((uintptr_t)(p)) & 3U) == 0U)

static uint8_t sd_wait_ready(uint32_t timeout_ms);
static uint8_t sd_hardware_init(void);
static uint8_t sd_read(uint8_t *dst, uint32_t sector, uint32_t count);
static uint8_t sd_write(const uint8_t *src, uint32_t sector, uint32_t count);
static uint8_t sd_verify_rw(void);
static void sdmmc_clock_init(void);

VOID filex_media_driver(FX_MEDIA *media_ptr)
{
    ULONG part_start = 0;
    ULONG part_size  = 0;

    switch (media_ptr->fx_media_driver_request) {

    case FX_DRIVER_INIT:
        sdmmc_clock_init();
        LOG_INFO("1. Memulai Inisialisasi SDMMC via HAL...");

        if (sd_hardware_init() != 0) {
            LOG_ERROR("Gagal Inisialisasi Hardware SD Card!");
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        LOG_INFO("SDMMC Hardware & Handshake Kartu Berhasil!");
        media_ptr->fx_media_driver_write_protect      = FX_FALSE;
        media_ptr->fx_media_driver_free_sector_update = FX_FALSE;
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_BOOT_READ:
        if (!sd_initialised) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        sd_partition_start = 0;

        if (sd_read((uint8_t *)media_ptr->fx_media_driver_buffer, 0, 1) != 0) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        /* Is sector 0 an MBR rather than a boot record? */
        if (_fx_partition_offset_calculate(media_ptr->fx_media_driver_buffer, 0,
                                           &part_start, &part_size) == FX_SUCCESS) {
            if (part_start != 0) {
                LOG_INFO("MBR terdeteksi, partisi mulai @sector %lu (%lu sector)",
                         (unsigned long)part_start, (unsigned long)part_size);

                sd_partition_start = part_start;

                if (sd_read((uint8_t *)media_ptr->fx_media_driver_buffer,
                            part_start, 1) != 0) {
                    media_ptr->fx_media_driver_status = FX_IO_ERROR;
                    break;
                }
            }
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_BOOT_WRITE:
        if (!sd_initialised) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        if (sd_write((const uint8_t *)media_ptr->fx_media_driver_buffer,
                     sd_partition_start, 1) != 0) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_READ:
        if (!sd_initialised) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        if (sd_read((uint8_t *)media_ptr->fx_media_driver_buffer,
                    media_ptr->fx_media_driver_logical_sector + sd_partition_start,
                    media_ptr->fx_media_driver_sectors) != 0) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_WRITE:
        if (!sd_initialised) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        if (sd_write((const uint8_t *)media_ptr->fx_media_driver_buffer,
                     media_ptr->fx_media_driver_logical_sector + sd_partition_start,
                     media_ptr->fx_media_driver_sectors) != 0) {
            media_ptr->fx_media_driver_status = FX_IO_ERROR;
            break;
        }

        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_FLUSH:
        media_ptr->fx_media_driver_status =
            (sd_initialised && sd_wait_ready(SD_READY_TIMEOUT_MS) == 0)
                ? FX_SUCCESS : FX_IO_ERROR;
        break;

    case FX_DRIVER_UNINIT:
        if (sd_initialised) {
            HAL_SD_DeInit(&hsd1);
            sd_initialised = 0;
        }
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    case FX_DRIVER_ABORT:
        /* Stop anything in flight. Polling mode means there usually isn't
           anything, but this is free and correct if you ever move to DMA. */
        if (sd_initialised) {
            HAL_SD_Abort(&hsd1);
        }
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;

    case FX_DRIVER_RELEASE_SECTORS:
        /* Advisory only -- a no-op is a valid implementation. Hook TRIM here
           later if you ever want it. */
        media_ptr->fx_media_driver_status = FX_SUCCESS;
        break;
    default:
        media_ptr->fx_media_driver_status = FX_IO_ERROR;
        break;
    }
}



static void sdmmc_clock_init(void)
{
    /* Turn on the 48 MHz internal RC. */
    RCC->CRRCR |= RCC_CRRCR_HSI48ON;
    while (!(RCC->CRRCR & RCC_CRRCR_HSI48RDY));

    /* 48 MHz clock domain source = HSI48 (CLK48SEL = 00). */
    RCC->CCIPR &= ~RCC_CCIPR_CLK48SEL;

    /* SDMMC1 kernel clock = the 48 MHz domain (SDMMCSEL = 0). */
    RCC->CCIPR2 &= ~RCC_CCIPR2_SDMMCSEL;
}

/*============================================================================*
 * Sector I/O with alignment handling
 *============================================================================*/

static uint8_t sd_read(uint8_t *dst, uint32_t sector, uint32_t count)
{
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
        LOG_ERROR("Read: kartu tidak siap.");
        return 1;
    }

    if (SD_IS_ALIGNED(dst)) {
        if (HAL_SD_ReadBlocks(&hsd1, dst, sector, count, SD_TIMEOUT_MS) != HAL_OK) {
            LOG_ERROR("Read gagal @sector %lu, error = 0x%08lX",
                      (unsigned long)sector,
                      (unsigned long)HAL_SD_GetError(&hsd1));
            return 1;
        }
        return 0;
    }

    while (count--) {
        if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
            return 1;
        }

        if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)sd_bounce, sector, 1,
                              SD_TIMEOUT_MS) != HAL_OK) {
            LOG_ERROR("Read (bounce) gagal @sector %lu, error = 0x%08lX",
                      (unsigned long)sector,
                      (unsigned long)HAL_SD_GetError(&hsd1));
            return 1;
        }

        memcpy(dst, sd_bounce, SECTOR_SIZE);
        dst += SECTOR_SIZE;
        sector++;
    }

    return 0;
}

static uint8_t sd_write(const uint8_t *src, uint32_t sector, uint32_t count)
{
    if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
        LOG_ERROR("Write: kartu tidak siap.");
        return 1;
    }

    if (SD_IS_ALIGNED(src)) {
        if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)src, sector, count,
                               SD_TIMEOUT_MS) != HAL_OK) {
            LOG_ERROR("Write gagal @sector %lu, error = 0x%08lX",
                      (unsigned long)sector,
                      (unsigned long)HAL_SD_GetError(&hsd1));
            return 1;
        }

        return (sd_wait_ready(SD_READY_TIMEOUT_MS) == 0) ? 0 : 1;
    }

    while (count--) {
        if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
            return 1;
        }

        memcpy(sd_bounce, src, SECTOR_SIZE);

        if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)sd_bounce, sector, 1,
                               SD_TIMEOUT_MS) != HAL_OK) {
            LOG_ERROR("Write (bounce) gagal @sector %lu, error = 0x%08lX",
                      (unsigned long)sector,
                      (unsigned long)HAL_SD_GetError(&hsd1));
            return 1;
        }

        if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
            return 1;
        }

        src += SECTOR_SIZE;
        sector++;
    }

    return 0;
}


#define SD_TEST_SECTOR   100000UL

static uint8_t sd_verify_rw(void)
{
    static uint32_t pattern[SECTOR_SIZE / 4];
    static uint32_t readback[SECTOR_SIZE / 4];
    uint32_t i;

    for (i = 0; i < (SECTOR_SIZE / 4); i++) {
        pattern[i] = 0xA5A50000UL | i;
    }

    memset(readback, 0, sizeof(readback));

    LOG_INFO("Verify: tulis pola ke sector %lu...", SD_TEST_SECTOR);

    if (sd_write((const uint8_t *)pattern, SD_TEST_SECTOR, 1) != 0) {
        LOG_ERROR("Verify: WRITE gagal.");
        return 1;
    }

    if (sd_read((uint8_t *)readback, SD_TEST_SECTOR, 1) != 0) {
        LOG_ERROR("Verify: READ gagal.");
        return 1;
    }

    for (i = 0; i < (SECTOR_SIZE / 4); i++) {
        if (readback[i] != pattern[i]) {
            LOG_ERROR("Verify: MISMATCH @word %lu, tulis 0x%08lX baca 0x%08lX",
                      (unsigned long)i,
                      (unsigned long)pattern[i],
                      (unsigned long)readback[i]);
            return 1;
        }
    }

    LOG_INFO("Verify: OK, write benar-benar tersimpan.");
    return 0;
}

/*============================================================================*
 * MSP
 *============================================================================*/

void HAL_SD_MspInit(SD_HandleTypeDef *hsd)
{
    GPIO_InitTypeDef         gpio = {0};
    RCC_PeriphCLKInitTypeDef periph = {0};

    if (hsd->Instance != SDMMC1) {
        return;
    }

    __HAL_RCC_HSI48_ENABLE();
    uint32_t guard = 0;
    while (!__HAL_RCC_GET_FLAG(RCC_FLAG_HSI48RDY)) {
        if (++guard > 1000000U) {
            LOG_ERROR("HSI48 tidak pernah ready!");
            return;
        }
    }

    periph.PeriphClockSelection = RCC_PERIPHCLK_SDMMC1;
    periph.Sdmmc1ClockSelection = RCC_SDMMC1CLKSOURCE_HSI48;
    if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK) {
        LOG_ERROR("Gagal konfigurasi kernel clock SDMMC1!");
        return;
    }

    __HAL_RCC_SDMMC1_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    gpio.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDMMC1;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin  = GPIO_PIN_12;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &gpio);

    gpio.Pin  = GPIO_PIN_2;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOD, &gpio);
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd)
{
    if (hsd->Instance != SDMMC1) {
        return;
    }

    __HAL_RCC_SDMMC1_CLK_DISABLE();

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 |
                           GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
}

/*============================================================================*
 * Bring-up
 *============================================================================*/

static uint8_t sd_hardware_init(void)
{
    if (sd_initialised) {
        HAL_SD_DeInit(&hsd1);
        sd_initialised = 0;
    }

    memset(&hsd1, 0, sizeof(hsd1));

    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_1B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    hsd1.Init.ClockDiv            = 6;

    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        LOG_ERROR("HAL_SD_Init gagal, error = 0x%08lX",
                  (unsigned long)HAL_SD_GetError(&hsd1));
        return 1;
    }

    if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK) {
        LOG_ERROR("Gagal pindah ke 4-bit bus, error = 0x%08lX",
                  (unsigned long)HAL_SD_GetError(&hsd1));
        return 1;
    }

    if (sd_wait_ready(SD_READY_TIMEOUT_MS) != 0) {
        LOG_ERROR("Kartu tidak masuk TRANSFER state setelah init.");
        return 1;
    }

    LOG_INFO("SD Card OK: tipe=%lu, blok=%lu x %lu bytes",
             (unsigned long)hsd1.SdCard.CardType,
             (unsigned long)hsd1.SdCard.BlockNbr,
             (unsigned long)hsd1.SdCard.BlockSize);

    sd_initialised = 1;

    /* Comment this line out once the filesystem mounts reliably. */
    sd_verify_rw();

    return 0;
}

static uint8_t sd_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER) {
            return 0;
        }
    }

    return 1;
}

ULONG SD_GetSectorCount(void)
{
    return sd_initialised ? hsd1.SdCard.BlockNbr : 0;
}