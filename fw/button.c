/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * abort button handling.
 */

#include <stdint.h>
#include "board.h"
#include "button.h"
#include "main.h"
#include "gpio.h"

#ifdef STM32F1
#define BUTTON_PRESSED_STATE 1
#else
#define BUTTON_PRESSED_STATE 0
#endif

static bool abort_pressed = false;

/**
 * button_poll() polls the abort button for state changes.
 *
 * @param [in]  None.
 * @return      None.
 * @globals     abort_pressed.
 */
static void
button_poll(void)
{
    if (gpio_get(BUTTON1_GPIO_Port, BUTTON1_GPIO_Pin) == BUTTON_PRESSED_STATE)
        abort_pressed = true;
    else
        abort_pressed = false;
}

/*
 * is_abort_button_pressed() turns reports whether the abort button was
 *                           pressed since the last time it was called.
 *                           Note that this function implements an edge
 *                           detector, so that if the button is held,
 *                           subsequent calls while the button is held
 *                           will return false instead of true.
 * @param [in]  None.
 * @return      true  - button was pressed.
 * @return      false - button was not pressed.
 */
int
is_abort_button_pressed(void)
{
    static int was_pressed = false;
    bool       pressed;

    button_poll();

    pressed = abort_pressed;
    abort_pressed = false;

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
button_init(void)
{
#ifdef STM32F4
    /* Use STM32 internal pull-up */
    gpio_mode_setup(BUTTON1_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                    BUTTON1_GPIO_Pin);
#else
    /* Board has external pull-down */
    gpio_setmode(BUTTON1_GPIO_Port, BUTTON1_GPIO_Pin, GPIO_SETMODE_INPUT);
#endif
}
