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

/* Values for gpio_setmode() */
#ifdef STM32F1
#define GPIO_SETMODE_ANALOG              0x0  // Analog Input
#define GPIO_SETMODE_INPUT               0x4  // Floating input (reset state)
#define GPIO_SETMODE_INPUT_PULLUPDOWN    0x8  // Input with pull-up / pull-down
#define GPIO_SETMODE_OUTPUT_10           0x1  // 10 MHz, Push-Pull
#define GPIO_SETMODE_OUTPUT_ODRAIN_10    0x5  // 10 MHz, Open-Drain
#define GPIO_SETMODE_OUTPUT_AF_PPULL_10  0x9  // 10 MHz, Alt func. Push-Pull
#define GPIO_SETMODE_OUTPUT_AF_ODRAIN_10 0xd  // 10 MHz, Alt func. Open-Drain
#define GPIO_SETMODE_OUTPUT_2            0x2  // 2 MHz, Push-Pull
#define GPIO_SETMODE_OUTPUT_ODRAIN_2     0x6  // 2 MHz, Open-Drain
#define GPIO_SETMODE_OUTPUT_AF_PPULL_2   0xa  // 2 MHz, Alt func. Push-Pull
#define GPIO_SETMODE_OUTPUT_AF_ODRAIN_2  0xe  // 2 MHz, Alt func. Open-Drain
#define GPIO_SETMODE_OUTPUT_50           0x3  // 50 MHz, Push-Pull
#define GPIO_SETMODE_OUTPUT_ODRAIN_50    0x7  // 50 MHz, Open-Drain
#define GPIO_SETMODE_OUTPUT_AF_PPULL_50  0xb  // 50 MHz, Alt func. Push-Pull
#define GPIO_SETMODE_OUTPUT_AF_ODRAIN_50 0xf  // 50 MHz, Alt func. Open-Drain
#endif
#ifdef STM32F4
/*
 * gpio_setmode() and gpio_getmode() definitions
 *
 * Bit values below are based on:
 *      AltFunc  Unused  MODER OTYPER  PUPDR OSPEEDR
 *         xxxx  x       xx    x       xx    xx
 *   Upper-Byte  -----Upper-Nibble---  -Lower-Nibble-
 *
 * MODER          OTYPER          PUPDR           OSPEEDR
 * 00 = Input     0=Push-pull     00=None         00=2 MHz
 * 01 = Output    1=Open-drain    01=Pull-up      01=25 MHz
 * 10 = AltFunc                   10=Pull-down    10=50 MHz
 * 11 = Analog                    11=Rsvd         11=100 MHz
 */
#define GPIO_SETMODE_INPUT               0x00  // Floating input (reset state)
#define GPIO_SETMODE_INPUT_PU            0x04  // Input, pull-up
#define GPIO_SETMODE_INPUT_PD            0x08  // Input, pull-down
#define GPIO_SETMODE_OUTPUT_2            0x20  // Output, push-pull, 2 Mhz
#define GPIO_SETMODE_OUTPUT_25           0x21  // Output, push-pull, 25 Mhz
#define GPIO_SETMODE_OUTPUT_50           0x22  // Output, push-pull, 50 Mhz
#define GPIO_SETMODE_OUTPUT_100          0x23  // Output, push-pull, 100 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_2     0x30  // Output, open-drain, 2 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_25    0x31  // Output, open-drain, 25 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_50    0x32  // Output, open-drain, 50 Mhz
#define GPIO_SETMODE_OUTPUT_ODRAIN_100   0x33  // Output, open-drain, 100 Mhz
#define GPIO_SETMODE_ALTFUNC_2           0x40  // Alt Func, push-pull, 2 Mhz
#define GPIO_SETMODE_ALTFUNC_25          0x41  // Alt Func, push-pull, 25 Mhz
#define GPIO_SETMODE_ALTFUNC_50          0x42  // Alt Func, push-pull, 50 Mhz
#define GPIO_SETMODE_ALTFUNC_100         0x43  // Alt Func, push-pull, 100 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_2    0x50  // Alt Func, open-drain, 2 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_25   0x51  // Alt Func, open-drain, 25 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_50   0x52  // Alt Func, open-drain, 50 Mhz
#define GPIO_SETMODE_ALTFUNC_ODRAIN_100  0x53  // Alt Func, open-drain, 100 Mhz
#define GPIO_SETMODE_ANALOG              0x60  // Analog

/* gpio_setmode() and gpio_getmode() or masks */
#define GPIO_SETMODE_SPEED_2             0x00  // Speed 2 MHz (or mask)
#define GPIO_SETMODE_SPEED_25            0x01  // Speed 25 MHz (or mask)
#define GPIO_SETMODE_SPEED_50            0x02  // Speed 50 MHz (or mask)
#define GPIO_SETMODE_SPEED_100           0x03  // Speed 100 MHz (or mask)
#define GPIO_SETMODE_PU                  0x04  // Pull-up (or mask)
#define GPIO_SETMODE_PD                  0x08  // Pull-down (or mask)
#define GPIO_SETMODE_OUTPUT              0x20  // Output
#define GPIO_SETMODE_OUTPUT_ODRAIN       0x30  // Output, open-drain
#define GPIO_SETMODE_AF_AF0             0x040  // Alt Function AF0 (or mask)
#define GPIO_SETMODE_AF_AF1             0x140  // Alt Function AF1 (or mask)
#define GPIO_SETMODE_AF_AF2             0x240  // Alt Function AF2 (or mask)
#define GPIO_SETMODE_AF_AF3             0x340  // Alt Function AF3 (or mask)
#define GPIO_SETMODE_AF_AF4             0x440  // Alt Function AF4 (or mask)
#define GPIO_SETMODE_AF_AF5             0x540  // Alt Function AF5 (or mask)
#define GPIO_SETMODE_AF_AF6             0x640  // Alt Function AF6 (or mask)
#define GPIO_SETMODE_AF_AF7             0x740  // Alt Function AF7 (or mask)
#define GPIO_SETMODE_AF_AF8             0x840  // Alt Function AF8 (or mask)
#define GPIO_SETMODE_AF_AF9             0x940  // Alt Function AF9 (or mask)
#define GPIO_SETMODE_AF_AF10            0xa40  // Alt Function AF10 (or mask)
#define GPIO_SETMODE_AF_AF11            0xb40  // Alt Function AF11 (or mask)
#define GPIO_SETMODE_AF_AF12            0xc40  // Alt Function AF12 (or mask)
#define GPIO_SETMODE_AF_AF13            0xd40  // Alt Function AF13 (or mask)
#define GPIO_SETMODE_AF_AF14            0xe40  // Alt Function AF14 (or mask)
#define GPIO_SETMODE_AF_AF15            0xf40  // Alt Function AF15 (or mask)
#endif

#define NUM_GPIO_BANKS 6

void gpio_setv(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, int value);
void gpio_setmode(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, uint value);
void gpio_init(void);
void gpio_show(int whichport, int pins);
void gpio_assign(int whichport, int pins, const char *assign);
uint gpio_name_match(const char **name, uint16_t pins[NUM_GPIO_BANKS]);
char *gpio_to_str(uint32_t port, uint16_t pin);

#endif /* _GPIO_H */

