/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Low level STM32 GPIO access.
 */

#ifndef _GPIO_H
#define _GPIO_H

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#define GPIO_IDR(x)   ((x)->IDR)
#define GPIO_ODR(x)   ((x)->ODR)
#define GPIO_BSRR(x)  ((x)->BSRR)
#define GPIO_MODER(x) ((x)->MODER)
#define GPIO_PUPDR(x) ((x)->PUPDR)
#define GPIO_CRL(x)   ((x)->CRL)
#define GPIO_CRH(x)   ((x)->CRH)

#include "gpio.h"
#include "main.h"
/* These are the same for both STM32F1 and STM32F4 */
#if defined(STM32F103xE) || defined(STM32F4)
#define USB_DM_Pin (1<<11)
#define USB_DP_Pin (1<<12)
#endif
#define USB_DPDM_Port GPIOA

typedef GPIO_TypeDef *GPIO_TypeDefP;
uint16_t gpio_get(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins);
void gpio_mode_setup(GPIO_TypeDefP GPIOx, uint8_t mode, uint8_t pupd_value,
                     uint16_t GPIO_Pins);
#else
/* libopencm3 */
typedef uint32_t GPIO_TypeDefP;

#include <libopencm3/stm32/gpio.h>
#ifdef STM32F4
#include <libopencm3/stm32/f4/gpio.h>
#else
#include <libopencm3/stm32/f1/gpio.h>
#endif

#ifdef STM32F4
#define A0_GPIO_Port        GPIOB
#define A16_GPIO_Port       GPIOD
#define D0_GPIO_Port        GPIOE
#define CE_GPIO_Port        GPIOC
#define CE_Pin              GPIO2
#define OE_GPIO_Port        GPIOC
#define OE_Pin              GPIO3
#define EE_EN_VCC_GPIO_Port GPIOD
#define EE_EN_VCC_Pin       GPIO6
#define EE_EN_VPP_GPIO_Port GPIOD
#define EE_EN_VPP_Pin       GPIO7
#define BUTTON1_GPIO_Port   GPIOC
#define BUTTON1_GPIO_Pin    GPIO12
#ifdef STM32F4_ONBOARD_LEDS
    #define LED_ALERT_PORT  GPIOD
    #define LED_ALERT_PIN   GPIO14
    #define LED_BUSY_PORT   GPIOD
    #define LED_BUSY_PIN    GPIO13
    #define LED_POWER_PORT  GPIOD
    #define LED_POWER_PIN   GPIO12
#else
    #define LED_ALERT_PORT  GPIOD
    #define LED_ALERT_PIN   GPIO11
    #define LED_BUSY_PORT   GPIOD
    #define LED_BUSY_PIN    GPIO10
    #define LED_POWER_PORT  GPIOD
    #define LED_POWER_PIN   GPIO9
#endif
#else /* STM32F1 */
#define A0_GPIO_Port        GPIOE
#define A16_GPIO_Port       GPIOC
#define D0_GPIO_Port        GPIOD
#define CE_GPIO_Port        GPIOB
#define CE_Pin              GPIO14
#define OE_GPIO_Port        GPIOB
#define OE_Pin              GPIO15
#define EE_EN_VCC_GPIO_Port GPIOB
#define EE_EN_VCC_Pin       GPIO12
#define EE_EN_VPP_GPIO_Port GPIOB
#define EE_EN_VPP_Pin       GPIO13
#define BUTTON1_GPIO_Port   GPIOA
#define BUTTON1_GPIO_Pin    GPIO0
#define CLKBND_Pin          GPIO0
#define CLKBND_Port         GPIOC
#define LED_ALERT_PORT      GPIOA
#define LED_ALERT_PIN       GPIO7
#define LED_BUSY_PORT       GPIOA
#define LED_BUSY_PIN        GPIO6
#define LED_POWER_PORT      GPIOA
#define LED_POWER_PIN       GPIO5
#define USB_PULLUP_PORT     GPIOB
#define USB_PULLUP_PIN      GPIO5
#endif /* STM32F1 */

/* These are the same for both STM32F1 and STM32F4 */
#define USB_DM_Pin          GPIO11
#define USB_DP_Pin          GPIO12
#define USB_DPDM_Port       GPIOA

#endif /* libopencm3 */

void     gpio_setv(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, int value);
void     gpio_mode_set(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, uint value);
void     gpio_init(void);

#endif /* _GPIO_H */

