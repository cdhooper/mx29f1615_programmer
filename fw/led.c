/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 LED control
 */

#include "board.h"
#include "main.h"
#include <stdint.h>
#include "led.h"
#include "gpio.h"

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#define LED_ALERT_PORT LED_ALERT_GPIO_Port
#define LED_ALERT_PIN  LED_ALERT_Pin
#define LED_BUSY_PORT  LED_BUSY_GPIO_Port
#define LED_BUSY_PIN   LED_BUSY_Pin
#define LED_POWER_PORT LED_POWER_GPIO_Port
#define LED_POWER_PIN  LED_POWER_Pin
#define gpio_setv      HAL_GPIO_WritePin
#else
/* libopencm3 */
#include <libopencm3/stm32/gpio.h>
#endif

void
led_alert(int turn_on)
{
    gpio_setv(LED_ALERT_PORT, LED_ALERT_PIN, turn_on);
}

void
led_busy(int turn_on)
{
    gpio_setv(LED_BUSY_PORT, LED_BUSY_PIN, turn_on);
}

void
led_power(int turn_on)
{
    gpio_setv(LED_POWER_PORT, LED_POWER_PIN, turn_on);
}

void
led_init(void)
{
    /* Enable Power, Busy, and Alert LEDs */
#ifndef USE_HAL_DRIVER
    /* libopencm3 */
#ifdef STM32F4
    gpio_mode_setup(LED_ALERT_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    LED_ALERT_PIN | LED_BUSY_PIN | LED_POWER_PIN);
#else
    /* Enable Power, Busy, and Alert LEDs */
    gpio_set_mode(LED_ALERT_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  LED_ALERT_PIN | LED_BUSY_PIN | LED_POWER_PIN);
#endif
#endif
}
