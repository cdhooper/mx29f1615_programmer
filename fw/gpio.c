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

#include "main.h"
#include "printf.h"
#include "uart.h"
#include "gpio.h"
#include "mx29f1615.h"

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#else
/* libopencm3 */
#ifdef STM32F4
#include <libopencm3/stm32/f4/rcc.h>
#else
#include <libopencm3/stm32/f1/rcc.h>
#endif
#endif /* libopencm3 */

#undef DEBUG_GPIO

#ifdef STM32F1
/**
 * spread8to32() will spread an 8-bit value to odd bits of a 32-bit value
 *
 * This is useful for STM32 registers where the combination of four
 * sequential bits will represent settings for a single GPIO pin.
 *
 * Algorithm
 *     00000000000000000000000011111111  Initial data
 *     00000000000011110000000000001111  (0x000000f0 << 12) | 0x0000000f
 *     00000011000000110000001100000011  (0x000c000c << 6) | 0x00030003
 *     00010001000100010001000100010001  (0x02020202 << 3) | 0x02020202
 */
static uint32_t
spread8to32(uint32_t v)
{
    v = ((v & 0x000000f0) << 12) | (v & 0x0000000f);
    v = ((v & 0x000c000c) << 6) | (v & 0x00030003);
    v = ((v & 0x22222222) << 3) | (v & 0x11111111);
    return (v);
}

#else

/**
 * spread16to32() will spread a 16-bit value to odd bits of a 32-bit value
 *
 * This is useful for STM32 registers where the combination of two
 * sequential bits will represent a mode for a single GPIO pin.
 *
 * Algorithm
 *     00000000000000001111111111111111  Initial data
 *     00000000111111110000000011111111  (0x0000ff00 << 8) | 0x000000ff
 *     00001111000011110000111100001111  (0x00f000f0 << 4) | 0x000f000f
 *     00110011001100110011001100110011  (0x0c0c0c0c << 2) | 0x03030303
 *     01010101010101010101010101010101  (0x22222222 << 1) | 0x11111111
 */
static uint32_t
spread16to32(uint32_t v)
{
    v = ((v & 0x0000ff00) << 8) | (v & 0x000000ff);
    v = ((v & 0x00f000f0) << 4) | (v & 0x000f000f);
    v = ((v & 0x0c0c0c0c) << 2) | (v & 0x03030303);
    v = ((v & 0x22222222) << 1) | (v & 0x11111111);
    return (v);
}
#endif


static void
gpio_set_1(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins)
{
    GPIO_BSRR(GPIOx) = GPIO_Pins;
}

static void
gpio_set_0(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins)
{
    GPIO_BSRR(GPIOx) = GPIO_Pins << 16;
}

void
gpio_setv(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, int value)
{
    if (value == 0)
        gpio_set_0(GPIOx, GPIO_Pins);
    else
        gpio_set_1(GPIOx, GPIO_Pins);
}

#ifdef USE_HAL_DRIVER
uint16_t
gpio_get(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins)
{
    return (GPIO_IDR(GPIOx) & GPIO_Pins);
}

#ifdef STM32F4
/* Only for HAL DRIVER on STM32F4 */
void gpio_mode_setup(GPIO_TypeDefP GPIOx, uint8_t mode, uint8_t pupd_value,
                     uint16_t GPIO_Pins)
{
    uint32_t moder = GPIO_MODER(GPIOx);
    uint32_t pupd  = GPIO_PUPDR(GPIOx);
    GPIO_MODER(GPIOx) = moder;
    GPIO_PUPDR(GPIOx) = pupd;
}
#endif
#endif


void
gpio_mode_set(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, uint value)
{
#ifdef DEBUG_GPIO
    char ch;
    switch ((uintptr_t)GPIOx) {
        case (uintptr_t)GPIOA:
            ch = 'A';
            break;
        case (uintptr_t)GPIOB:
            ch = 'B';
            break;
        case (uintptr_t)GPIOC:
            ch = 'C';
            break;
        case (uintptr_t)GPIOD:
            ch = 'D';
            break;
        case (uintptr_t)GPIOE:
            ch = 'E';
            break;
        case (uintptr_t)GPIOF:
            ch = 'F';
            break;
        default:
            ch = '?';
            break;
    }
    printf(" GPIO%c ", ch);
#endif
#ifdef STM32F1
    if (GPIO_Pins & 0xff) {
        uint32_t pins   = GPIO_Pins & 0xff;
        uint32_t spread = spread8to32(pins);
        uint32_t mask   = spread * 0xf;
        uint32_t newval;
        uint32_t temp;

        if (value == 0)
            value = 0x08;  // Input with pull-up / pull-down

        newval = spread * (value & 0xf);
        temp = (GPIO_CRL(GPIOx) & ~mask) | newval;

#ifdef DEBUG_GPIO
        printf("CRL v=%02x p=%04x sp=%08lx mask=%08lx %08lx^%08lx=%08lx\n",
               value, GPIO_Pins, spread, mask, GPIO_CRL(GPIOx), temp, GPIO_CRL(GPIOx) ^ temp);
#endif
        GPIO_CRL(GPIOx) = temp;
    }
    if (GPIO_Pins & 0xff00) {
        uint32_t pins   = (GPIO_Pins >> 8) & 0xff;
        uint32_t spread = spread8to32(pins);
        uint32_t mask   = spread * 0xf;
        uint32_t newval;
        uint32_t temp;

        if (value == 0)
            value = 0x08;  // Input with pull-up

        newval = spread * (value & 0xf);
        temp   = (GPIO_CRH(GPIOx) & ~mask) | newval;

#ifdef DEBUG_GPIO
        printf("CRH v=%02x p=%04x sp=%08lx mask=%08lx %08lx^%08lx=%08lx\n",
               value, GPIO_Pins, spread, mask, GPIO_CRH(GPIOx), temp, GPIO_CRH(GPIOx) ^ temp);
#endif
        GPIO_CRH(GPIOx) = temp;
    }

#else  /* STM32F407 */
    uint32_t spread = spread16to32(GPIO_Pins);
    uint32_t mask   = spread * 0x3;
    uint32_t newval = spread * value;
    GPIO_MODER(GPIOx) = (GPIO_MODER(GPIOx) & ~mask) | newval;
#endif
}

#ifndef USE_HAL_DRIVER
void gpio_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_GPIOC);
    rcc_periph_clock_enable(RCC_GPIOD);
    rcc_periph_clock_enable(RCC_GPIOE);
#ifdef STM32F4
    rcc_periph_clock_enable(RCC_GPIOH);

    /* Enable Power, Busy, and Alert LEDs and turn them on */
    gpio_set(LED_POWER_PORT, LED_POWER_PIN | LED_BUSY_PIN | LED_ALERT_PIN);
    gpio_mode_setup(LED_POWER_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    LED_POWER_PIN | LED_BUSY_PIN | LED_ALERT_PIN);

    /* Enable EN_VCC and EN_VPP as outputs, default off */
    gpio_setv(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, 1);
    gpio_mode_setup(EE_EN_VCC_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    EE_EN_VCC_Pin);

    gpio_setv(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, 0);
    gpio_mode_setup(EE_EN_VPP_GPIO_Port, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                    EE_EN_VPP_Pin);
#else
    rcc_periph_clock_enable(RCC_AFIO);

    /* Enable Power, Busy, and Alert LEDs and turn them on */
    gpio_set(LED_POWER_PORT, LED_POWER_PIN | LED_BUSY_PIN | LED_ALERT_PIN);
    gpio_set_mode(LED_POWER_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  LED_POWER_PIN | LED_BUSY_PIN | LED_ALERT_PIN);

    /* Drive HSE boundary trace */
    gpio_set(CLKBND_Port, CLKBND_Pin);
    gpio_mode_set(CLKBND_Port, CLKBND_Pin, GPIO_MODE_OUTPUT_2_MHZ);

    /* Enable EN_VCC and EN_VPP as outputs, default off */
    gpio_setv(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, 1);
    gpio_mode_set(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, GPIO_MODE_OUTPUT_10_MHZ);

    gpio_setv(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, 0);
    gpio_mode_set(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, GPIO_MODE_OUTPUT_10_MHZ);

#if 0
    /* Enable PROM_OE and PROM_CE as inputs, pulled down (down is ODR bit=0) */
    gpio_mode_set(CE_GPIO_Port, CE_Pin, GPIO_MODE_INPUT);
    gpio_mode_set(OE_GPIO_Port, OE_Pin, GPIO_MODE_INPUT);
#endif

    /* Inputs */
    gpio_mode_set(BUTTON1_GPIO_Port, BUTTON1_GPIO_Pin, GPIO_MODE_INPUT);
#endif
    mx_disable();
}
#endif
