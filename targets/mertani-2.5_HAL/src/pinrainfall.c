#include "pinrainfall.h"

#include "stm32l4xx_hal.h"

#define RAINFALL_USE_PC3        0   

#if RAINFALL_USE_PC3
  #define RAINFALL_PORT         GPIOC
  #define RAINFALL_PIN          GPIO_PIN_3
  #define RAINFALL_IRQn         EXTI3_IRQn
  #define RAINFALL_IRQHandler   EXTI3_IRQHandler
  #define RAINFALL_CLK_ENABLE() __HAL_RCC_GPIOC_CLK_ENABLE()
#else
  #define RAINFALL_PORT         GPIOB
  #define RAINFALL_PIN          GPIO_PIN_15
  #define RAINFALL_IRQn         EXTI15_10_IRQn
  #define RAINFALL_IRQHandler   EXTI15_10_IRQHandler
  #define RAINFALL_CLK_ENABLE() __HAL_RCC_GPIOB_CLK_ENABLE()
#endif

/* Reed switch di tipping bucket adalah kontak mekanis: dia memantul.
 * Satu jungkitan bisa menghasilkan 5-10 tepi dalam beberapa milidetik.
 * Tanpa debounce, 1 mm hujan tercatat jadi 10 mm.
 *
 * 150 ms aman: jungkitan tercepat pada hujan sangat deras masih di atas
 * 200 ms, sementara pantulan reed switch selesai di bawah 20 ms. */
#define RAINFALL_DEBOUNCE_MS    150U

/* ------------------------------------------------------------------------- */
/* State                                                                     */
/* ------------------------------------------------------------------------- */

static void (*p_tip_cb)(uint32_t tip_ms);

static volatile uint32_t last_edge_ms;

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void PinRainfall_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    RAINFALL_CLK_ENABLE();

    /* Pull-up internal + deteksi tepi turun.
     *
     * Reed switch tipping bucket biasanya menyambung ke ground saat
     * menjungkit. Dengan pull-up, jalur diam di 3,3 V dan turun ke 0 V
     * saat kontak menutup - itulah tepi yang dihitung.
     *
     * Kalau papan sudah punya pull-up eksternal, GPIO_PULLUP di sini
     * tidak merugikan (dua pull-up paralel hanya memperkuat). Kalau
     * ternyata papan memakai pull-DOWN dan switch menyambung ke 3,3 V,
     * ganti ke GPIO_MODE_IT_RISING dan GPIO_PULLDOWN. */
    gpio.Pin  = RAINFALL_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(RAINFALL_PORT, &gpio);

    last_edge_ms = 0U;

    /* Prioritas 7: lebih rendah dari UART (6), karena menghitung tip
     * jauh kurang mendesak daripada tidak kehilangan byte serial. */
    HAL_NVIC_SetPriority(RAINFALL_IRQn, 7, 0);
    HAL_NVIC_EnableIRQ(RAINFALL_IRQn);
}

void PinRainfall_SetInterrupt_Callback(void (*callback)(uint32_t tip_ms))
{
    p_tip_cb = callback;
}

/* ------------------------------------------------------------------------- */
/* Interrupt                                                                 */
/*                                                                           */
/* Hapus RAINFALL_IRQHandler kalau stm32l4xx_it.c sudah mendefinisikannya,   */
/* dan taruh panggilan HAL_GPIO_EXTI_IRQHandler() di versi yang sudah ada.   */
/* ------------------------------------------------------------------------- */

void RAINFALL_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(RAINFALL_PIN);
}

/* Hook __weak bersama untuk SEMUA pin EXTI. Kalau nanti ada modul lain
 * yang butuh EXTI, pindahkan fungsi ini ke system.c sebagai dispatcher
 * dan panggil PinRainfall_EXTI_Callback() dari sana - pola yang sama
 * dengan HAL_UARTEx_RxEventCallback. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now;

    if (GPIO_Pin != RAINFALL_PIN) {
        return;
    }

    now = HAL_GetTick();

    /* Debounce. Pengurangan unsigned menangani wrap HAL_GetTick() dengan
     * benar tanpa perlu diperlakukan khusus. */
    if ((uint32_t)(now - last_edge_ms) < RAINFALL_DEBOUNCE_MS) {
        return;
    }

    last_edge_ms = now;

    if (p_tip_cb != NULL) {
        p_tip_cb(now);
    }
}