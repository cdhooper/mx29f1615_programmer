/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * main routine.
 */

#include "printf.h"
#include <stdint.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include "board.h"
#include "main.h"
#include "clock.h"
#include "uart.h"
#include "usb.h"
#include "mx29f1615.h"
#include "led.h"
#include "gpio.h"
#include "adc.h"
#include "button.h"
#include "cmdline.h"
#include "readline.h"
#include "timer.h"
#include "utils.h"
#include "version.h"

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#else
/* libopencm3 */
#endif

#if BOARD_REV >= 2
#define BUTTON_PRESSED_STATE 1
#else
#define BUTTON_PRESSED_STATE 0
#endif

static void
reset_everything(void)
{
    RCC_APB1ENR  = 0;  // Disable all peripheral clocks
    RCC_APB1RSTR = 0xffffffff;  // Reset APB1
    RCC_APB2RSTR = 0xffffffff;  // Reset APB2
    RCC_APB1RSTR = 0x00000000;  // Release APB1 reset
    RCC_APB2RSTR = 0x00000000;  // Release APB2 reset
}

void
main_poll(void)
{
    usb_poll();
    mx_poll();
    adc_poll(true, false);
}

int
main(void)
{
    reset_check();
    reset_everything();
    clock_init();
    timer_init();
    timer_delay_msec(500);
    led_init();
    gpio_init();
    led_alert(0); // Turn off Alert LED
    uart_init();

    rl_initialize();  // Enable command editing and history
    using_history();

    adc_init();
    button_init();

    printf("\r\nMX29F1615 programmer %s\n", version_str);
    identify_cpu();
    show_reset_reason();

    usb_startup();

    led_busy(0);

    while (1) {
        main_poll();
        cmdline();
    }

    return (0);
}
