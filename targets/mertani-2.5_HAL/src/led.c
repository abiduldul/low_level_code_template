#include "led.h"

#include "stm32l4xx_hal.h"

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

#define LED_GPIO_PORT           GPIOA
#define LED_GPIO_PIN            GPIO_PIN_8
#define LED_GPIO_CLK_ENABLE()   __HAL_RCC_GPIOA_CLK_ENABLE()

/* Set to 1 if the LED is wired active-low (cathode to the pin). */
#define LED_STATE_ON          GPIO_PIN_SET
#define LED_STATE_OFF         GPIO_PIN_RESET

static uint8_t ledStatus;

void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* The macro already includes the read-back barrier the register version
     * did manually with the 'dummy' variable. */
    LED_GPIO_CLK_ENABLE();

    gpio.Pin   = LED_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;       /* MODER = 01, OTYPER = 0 */
    gpio.Pull  = GPIO_NOPULL;               /* PUPDR = 00             */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;       /* OSPEEDR = 00           */
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);

    /* HAL_GPIO_Init does not touch ODR, so drive a known state and keep
     * ledStatus consistent with the pin. */
    LED_Off();
}

uint8_t LED_GetStatus(void)
{
    return ledStatus;
}

void LED_On(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, LED_STATE_ON);
    ledStatus = 1U;
}

void LED_Off(void)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, LED_STATE_OFF);
    ledStatus = 0U;
}

void LED_Toggle(void)
{
    HAL_GPIO_TogglePin(LED_GPIO_PORT, LED_GPIO_PIN);
    ledStatus = !ledStatus;
}