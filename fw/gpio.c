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

#include "board.h"
#include "main.h"
#include "printf.h"
#include "uart.h"
#include "gpio.h"
#include "timer.h"
#include "utils.h"
#include "mx29f1615.h"
#include <string.h>

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

#define ARRAY_SIZE(x) (int)((sizeof (x) / sizeof ((x)[0])))

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


/*
 * gpio_set_1
 * ----------
 * Drives the specified GPIO bits to 1 values without affecting other bits.
 */
static void
gpio_set_1(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins)
{
    GPIO_BSRR(GPIOx) = GPIO_Pins;
}

/*
 * gpio_set_0
 * ----------
 * Drives the specified GPIO bits to 0 values without affecting other bits.
 */
static void
gpio_set_0(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins)
{
    GPIO_BSRR(GPIOx) = GPIO_Pins << 16;
}

/*
 * gpio_setv
 * ---------
 * Sets the specified GPIO bits to 0 or 1 values without affecting other bits.
 */
void
gpio_setv(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, int value)
{
    if (value == 0)
        gpio_set_0(GPIOx, GPIO_Pins);
    else
        gpio_set_1(GPIOx, GPIO_Pins);
}

/*
 * gpio_getv
 * ---------
 * Gets the current output values (not input values) of the specified GPIO
 * port and pins.
 */
static uint
gpio_getv(uint32_t GPIOx, uint pin)
{
#ifdef STM32F1
    return (GPIO_ODR(GPIOx) & BIT(pin));
#endif
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

/*
 * gpio_setmode
 * ------------
 * Sets the complex input/output mode of the GPIO.
 *
 * STM32F1: value specifies the GPIO mode and configuration
 * 0x0 0000: Analog Input
 * 0x4 0100: Floating input (reset state)
 * 0x8 1000: Input with pull-up / pull-down
 * 0xc 1100: Reserved
 * 0x1 0001: Output 10 MHz, Push-Pull
 * 0x5 0101: Output 10 MHz, Open-Drain
 * 0x9 1001: Output 10 MHz, Alt function Push-Pull
 * 0xd 1101: Output 10 MHz, Alt function Open-Drain
 * 0x2 0010: Output 2 MHz, Push-Pull
 * 0x6 0110: Output 2 MHz, Open-Drain
 * 0xa 1010: Output 2 MHz, Alt function Push-Pull
 * 0xe 1110: Output 2 MHz, Alt function Open-Drain
 * 0x3 0011: Output 50 MHz, Push-Pull
 * 0x7 0111: Output 50 MHz, Open-Drain
 * 0xb 1011: Output 50 MHz, Alt function Push-Pull
 * 0xf 1111: Output 50 MHz, Alt function Open-Drain
 */
void
gpio_setmode(GPIO_TypeDefP GPIOx, uint16_t GPIO_Pins, uint value)
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

        newval = spread * (value & 0xf);
        temp = (GPIO_CRL(GPIOx) & ~mask) | newval;

#ifdef DEBUG_GPIO
        printf("CRL v=%02x p=%04x sp=%08lx mask=%08lx %08lx^%08lx=%08lx\n",
               value, GPIO_Pins, spread, mask, GPIO_CRL(GPIOx), temp,
               GPIO_CRL(GPIOx) ^ temp);
#endif
        GPIO_CRL(GPIOx) = temp;
    }
    if (GPIO_Pins & 0xff00) {
        uint32_t pins   = (GPIO_Pins >> 8) & 0xff;
        uint32_t spread = spread8to32(pins);
        uint32_t mask   = spread * 0xf;
        uint32_t newval;
        uint32_t temp;

        newval = spread * (value & 0xf);
        temp   = (GPIO_CRH(GPIOx) & ~mask) | newval;

#ifdef DEBUG_GPIO
        printf("CRH v=%02x p=%04x sp=%08lx mask=%08lx %08lx^%08lx=%08lx\n",
               value, GPIO_Pins, spread, mask, GPIO_CRH(GPIOx), temp,
               GPIO_CRH(GPIOx) ^ temp);
#endif
        GPIO_CRH(GPIOx) = temp;
    }

#else  /* STM32F407 */
    uint32_t spread = spread16to32(GPIO_Pins);
    uint32_t mask   = spread * 0x3;
    uint32_t newval = spread * value;
    GPIO_MODER(GPIOx) = (GPIO_MODER(GPIOx) & ~mask) | newval;
    // XXX: Macros need to be implemented for STM32F4
#endif
}

/*
 * gpio_getmode
 * ------------
 * Get the input/output mode of the specified GPIO pins.
 */
static uint
gpio_getmode(uint32_t GPIOx, uint pin)
{
#ifdef STM32F1
    if (pin < 8) {
        uint shift = pin * 4;
        return ((GPIO_CRL(GPIOx) >> shift) & 0xf);
    } else {
        uint shift = (pin - 8) * 4;
        return ((GPIO_CRH(GPIOx) >> shift) & 0xf);
    }
#endif
}

/*
 * gpio_num_to_gpio
 * ----------------
 * Convert the specified GPIO number to its respective port address.
 */
static uint32_t
gpio_num_to_gpio(uint num)
{
    static const uint32_t gpios[] = {
        GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF
    };
    return (gpios[num]);
}

#ifdef STM32F1
static const char * const gpio_mode_short[] = {
    "A", "O1", "O2", "O5",      // AnalogI, Output {10, 2, 50} MHz
    "I", "OD1", "OD2", "OD5",   // Input, Output Open Drain
    "PUD", "AO1", "AO2", "AO5", // Input Pull Up/Down, AF Output
    "Rsv", "AD1", "AD2", "AD5", // Reserved, AF OpenDrain
};

static const char * const gpio_mode_long[] = {
    "Analog Input", "O10 Output 10MHz", "O2 Output 2MHz", "O5 Output 50MHz",
    "Input", "OD10 Open Drain 10MHz",
        "OD2 Open Drain 2MHz", "OD5 Open Drain 50MHz",
    "PUD", "AO10 AltFunc Output 10MHz",
        "AO2 AltFunc Output 2MHz", "AO5 AltFunc Output 50MHz",
    "Rsv", "AD1 AltFunc Open Drain 10MHz",
        "AD2 AltFunc Open Drain 2MHz", "AD5 AltFunc Open Drain 50MHz",
};
#endif

/*
 * gpio_show
 * ---------
 * Display current values and input/output state of GPIOs.
 */
void
gpio_show(int whichport, int whichpin)
{
    int port;
    int pin;
    uint mode;
    uint print_all = (whichport < 0) && (whichpin < 0);

    if (print_all) {
        printf("EEPROM A0-A15=PE0-PE5 A16-A19=PC6-PC9\n"
               "EEPROM D0-D15=PD0-PD15  CE=PB14 OE=PB15\n"
               "Misc   AbortButton=PA0 EN_VCC=PB12 EN_VPP=PB13\n"
               "Sense  V3P3=PA1 V10DAC=PA2 V10=PA3 V10DAC=PA4 V5=PC4 "
                    "V5CL=PC5\n"
               "LED    Power=PA5 Busy=PA6 Alert=PA7\n"
               "USB    V5=PA9 DM=PA11 DP=PA12\n");
        printf("MODE  ");
        for (pin = 0; pin < 16; pin++)
            printf("%4d", pin);
        printf("\n");
    }

    for (port = 0; port < 5; port++) {
        uint32_t gpio = gpio_num_to_gpio(port);
        if ((whichport >= 0) && (port != whichport))
            continue;
        if (print_all)
            printf("GPIO%c ", 'A' + port);
        for (pin = 0; pin < 16; pin++) {
            const char *mode_txt;
            if ((whichpin >= 0) && (pin != whichpin))
                continue;
            mode = gpio_getmode(gpio, pin);
#ifdef STM32F1
            if (print_all) {
                mode_txt = gpio_mode_short[mode];
                if (mode == GPIO_SETMODE_INPUT_PULLUPDOWN)
                    mode_txt = gpio_getv(gpio, pin) ? "PU" : "PD";
            } else {
                mode_txt = gpio_mode_long[mode];
                if (mode == GPIO_SETMODE_INPUT_PULLUPDOWN)
                    mode_txt = gpio_getv(gpio, pin) ? "Input PU" : "Input PD";
            }
#endif
            /* Pull-up or pull down depending on output register state */
            if (print_all) {
                printf("%4s", mode_txt);
            } else {
                char mode_extra[8];
                uint pinstate = !!(gpio_get(gpio, BIT(pin)));
                mode_extra[0] = '\0';
                if ((gpio_getmode(gpio, pin) & 3) != 0) {
                    /* Output */
                    uint outval = !!gpio_getv(gpio, pin);
                    if (outval != pinstate)
                        sprintf(mode_extra, "=%d>", outval);
                }
                printf("P%c%d=%s (%s%d)\n",
                        'A' + port, pin, mode_txt, mode_extra, pinstate);
            }
        }
        if (print_all)
            printf("\n");
    }

    if (!print_all)
        return;

    printf("\nState ");
    for (pin = 0; pin < 16; pin++)
        printf("%4d", pin);
    printf("\n");

    for (port = 0; port < 5; port++) {
        uint32_t gpio = gpio_num_to_gpio(port);
        printf("GPIO%c ", 'A' + port);
        for (pin = 0; pin < 16; pin++) {
            uint pinstate = !!(gpio_get(gpio, BIT(pin)));
            if ((gpio_getmode(gpio, pin) & 3) != 0) {
                /* Not in an input mode */
                uint outval = !!gpio_getv(gpio, pin);
                if (outval != pinstate) {
                    printf(" %d>%d", outval, pinstate);
                    continue;
                }
            }
            printf("%4d", pinstate);
        }
        printf("\n");
    }
}

/*
 * gpio_assign
 * -----------
 * Assign a GPIO input/output state or output value according to the
 * user-specified string.
 */
void
gpio_assign(int whichport, int whichpin, const char *assign)
{
    uint mode;
    uint gpio;
    uint pins;
    if (*assign == '?') {
        printf("Valid modes:");
        for (mode = 0; mode < ARRAY_SIZE(gpio_mode_short); mode++)
            printf(" %s", gpio_mode_short[mode]);
        printf(" 0 1 A I O PU PD\n");
        return;
    }
    gpio = gpio_num_to_gpio(whichport);
    pins = BIT(whichpin);
    for (mode = 0; mode < ARRAY_SIZE(gpio_mode_short); mode++) {
        if (strcasecmp(gpio_mode_short[mode], assign) == 0) {
            gpio_setmode(gpio, pins, mode);
            return;
        }
    }
    switch (*assign) {
        case 'a':
        case 'A':
            if (assign[1] == '\0') {
                gpio_setmode(gpio, pins, GPIO_SETMODE_INPUT_ANALOG);
                return;
            }
            break;
        case 'i':
        case 'I':
            if (assign[1] == '\0') {
                gpio_setmode(gpio, pins, GPIO_SETMODE_INPUT);
                return;
            }
            break;
        case 'o':
        case 'O':
            if (assign[1] == '\0') {
                gpio_setmode(gpio, pins, GPIO_SETMODE_OUTPUT_PPULL_2);
                return;
            }
            break;
        case '0':
            if (assign[1] == '\0') {
                gpio_setv(gpio, pins, 0);
                mode = gpio_getmode(gpio, whichpin);
                if ((mode & 3) == 0) {
                    /* Currently an input mode -- default to 2MHz Output */
                    gpio_setmode(gpio, pins, GPIO_SETMODE_OUTPUT_PPULL_2);
                }
                return;
            }
            break;
        case '1':
            if (assign[1] == '\0') {
                gpio_setv(gpio, pins, 1);
                if ((mode & 3) == 0) {
                    /* Currently an input mode -- default to 2MHz Output */
                    gpio_setmode(gpio, pins, GPIO_SETMODE_OUTPUT_PPULL_2);
                }
                return;
            }
            break;
        case 'p':
        case 'P':
            if (assign[2] == '\0') {
                switch (assign[1]) {
                    case 'u':
                    case 'U':
                        gpio_setmode(gpio, pins, GPIO_SETMODE_INPUT_PULLUPDOWN);
                        gpio_setv(gpio, pins, 1);
                        return;
                    case 'd':
                    case 'D':
                        gpio_setmode(gpio, pins, GPIO_SETMODE_INPUT_PULLUPDOWN);
                        gpio_setv(gpio, pins, 0);
                        return;
                    default:
                        break;
                }
            }
            break;
        default:
            break;
    }

    printf("Invalid mode %s for GPIO\n", assign);
}

#ifndef USE_HAL_DRIVER
/*
 * gpio_init
 * ---------
 * Initialize most board GPIO states.
 */
void
gpio_init(void)
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
    gpio_setmode(CLKBND_Port, CLKBND_Pin, GPIO_MODE_OUTPUT_2_MHZ);

    /* Enable EN_VCC and EN_VPP as outputs, default off */
    gpio_setv(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, 1);
    gpio_setmode(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, GPIO_MODE_OUTPUT_10_MHZ);

    gpio_setv(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, 0);
    gpio_setmode(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, GPIO_MODE_OUTPUT_10_MHZ);

#if 0
    /* Enable PROM_OE and PROM_CE as inputs, pulled down (down is ODR bit=0) */
    gpio_setmode(CE_GPIO_Port, CE_Pin, GPIO_MODE_INPUT);
    gpio_setmode(OE_GPIO_Port, OE_Pin, GPIO_MODE_INPUT);
#endif

    /* Inputs */
    gpio_setmode(BUTTON1_GPIO_Port, BUTTON1_GPIO_Pin, GPIO_MODE_INPUT);
#endif
    mx_disable();
}
#endif
