/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Main routines for MX29F1615 programmer.
 */

#include "printf.h"
#include "uart.h"
#include "main.h"
#include "adc.h"
#include "stdbool.h"
#include "usb_device.h"
#include "mx29f1615.h"
#include "readline.h"
#include "cmdline.h"
#include "timer.h"
#include "utils.h"
#include "version.h"

#ifdef STM32F1XX
#include "stm32f1xx_hal_flash_ex.h"
#endif

uint32_t rcc_pclk2_frequency = 0;

static bool button1_pressed = false;
static bool button2_pressed = false;

static void
poll_buttons(void)
{
#ifdef STM32F1XX
    if (HAL_GPIO_ReadPin(BUTTON1_GPIO_Port, BUTTON1_Pin) == 1)
        button1_pressed = true;
#else
    if (HAL_GPIO_ReadPin(BUTTON1_GPIO_Port, BUTTON1_Pin) == 0)
        button1_pressed = true;

    if (HAL_GPIO_ReadPin(BUTTON2_GPIO_Port, BUTTON2_Pin) == 0)
        button2_pressed = true;
#endif
}

int
is_abort_button_pressed(void)
{
    static bool was_pressed = false;
    bool        pressed;

    poll_buttons();
    pressed = button1_pressed;
    button1_pressed = false;

    if (was_pressed) {
        if (pressed == false)
            was_pressed = false;  // no longer pressed
        return (false);
    }
    if (pressed)
        was_pressed = true;
    return (pressed);
}

static bool
is_button2_pressed(void)
{
    static bool was_pressed = false;
    bool        pressed;

    poll_buttons();
    pressed = button2_pressed;
    button2_pressed = false;

    if (was_pressed) {
        if (pressed == false)
            was_pressed = false;  // no longer pressed
        return (false);
    }
    if (pressed)
        was_pressed = true;
    return (pressed);
}

void
mx_main(void)
{
    /* Power LED on */
    HAL_GPIO_WritePin(LED_POWER_GPIO_Port, LED_POWER_Pin, 1);

    uart_init();

    printf("\r\nMX29F1615 programmer %s\n", version_str);
    timer_init();

#ifdef DO_PRINTF_TEST
    printf_test();
#endif
    rl_initialize();  // Enable command editing and history
    using_history();

    adc_init();
    identify_cpu();

    rcc_pclk2_frequency = HAL_RCC_GetPCLK2Freq();
    printf("    HCLK=%ld MHz  PCLK1=%ld MHz  PCLK2=%ld MHz\n",
           HAL_RCC_GetHCLKFreq() / 1000000,
           HAL_RCC_GetPCLK1Freq() / 1000000, rcc_pclk2_frequency / 1000000);

    while (1) {
        poll_buttons();

        if (is_abort_button_pressed()) {
            uint16_t data;
            uint32_t addr;
            uint32_t base;
            char     buf[64];
            printf("Button1 pressed\n");
            mx_enable();
            HAL_Delay(1);
            printf("ID=%08lx\n", mx_id());
            printf("Status=%04x %s\n", mx_status_read(buf, sizeof (buf)), buf);
            mx_read_mode();
            for (base = 0; base < 1024 * 1024; base += 64 * 1024) {
                printf("%06lx:", base);
                for (addr = 0; addr < 8; addr++) {
                    mx_read(base + addr, &data, 1);
                    printf(" %04x", data);
                }
                printf("\n");
            }
            mx_disable();
            printf("\n");
        }
        if (is_button2_pressed()) {
            printf("Button2 pressed\n");
        }
        mx_poll();
        adc_poll(true, false);
        cmdline();
    }
}
