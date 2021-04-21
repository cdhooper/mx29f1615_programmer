/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 USART and basic input / output handling.
 */

#include "printf.h"
#include "main.h"
#include "uart.h"
#include <stdbool.h>
#include "timer.h"
#include "irq.h"
#include "usb.h"

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#include <usbd_cdc_if.h>
#define USART_DR(x)   ((x)->DR)
#define USART_SR(x)   ((x)->SR)
#define USART_CR1(x)  ((x)->CR1)
#define USART_DR_MASK USART_DR_DR_Msk
typedef USART_TypeDef * USART_TypeDef_P;
#define nvic_set_priority(irq, pri) \
                      HAL_NVIC_SetPriority(irq, (pri) >> 4, (pri) & 0xf)
#define nvic_enable_irq(irq) HAL_NVIC_EnableIRQ(irq)
#define NVIC_USART1_IRQ USART1_IRQn
#define NVIC_USART3_IRQ USART3_IRQn
#define usart1_isr USART1_IRQHandler
#define usart3_isr USART3_IRQHandler
#else
/* libopencm3 */
#if defined(STM32F1)
#include <libopencm3/stm32/f1/nvic.h>
#elif defined(STM32F4)
#include <libopencm3/stm32/f4/nvic.h>
#endif
#include <libopencm3/stm32/usart.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
typedef uint32_t USART_TypeDef_P;
#endif


#if defined(STM32F1)
/* STM32F1XX on Rev2+ uses PA9 for CONS_TX and PA10 for CONS_RX */
/* STM32F1XX on Rev4  uses PB6 for CONS_TX and PB7 for CONS_RX */
#define CONSOLE_USART       USART1
#define CONSOLE_IRQn        NVIC_USART1_IRQ
#define CONSOLE_IRQHandler  usart1_isr
#elif defined(STM32F4)
/* STM32F407 Discovery on Rev1 uses PA10 for CONS_TX and PA11 for CONS_RX */
#define CONSOLE_USART       USART3
#define CONSOLE_IRQn        NVIC_USART3_IRQ
#define CONSOLE_IRQHandler  usart3_isr
#endif

static volatile uint cons_in_rb_producer; // Console input current writer pos
static uint          cons_in_rb_consumer; // Console input current reader pos
static uint8_t       cons_in_rb[1024];    // Console input ring buffer (FIFO)
static uint8_t       usb_out_buf[256];    // USB output buffer
static uint16_t      usb_out_bufpos = 0;  // USB output buffer position
static bool          uart_console_active = false;

uint8_t last_input_source = 0;

static void uart_wait_send_ready(USART_TypeDef_P usart)
{
    /* Wait until the data has been transferred into the shift register. */
    int count = 0;

    while ((USART_SR(usart) & USART_SR_TXE) == 0)
        if (count++ == 1000)
            break;  // Misconfigured hardware?
}

static void uart_send(USART_TypeDef_P usart, uint16_t data)
{
    USART_DR(usart) = (data & USART_DR_MASK);
}

static void uart_send_blocking(USART_TypeDef_P usart, uint16_t data)
{
    uart_wait_send_ready(usart);
    uart_send(usart, data);
}

int
uart_putchar(int ch)
{
    uart_send_blocking(CONSOLE_USART, (uint16_t) ch);
    return (0);
}

static uint16_t
uart_recv(USART_TypeDef_P usart)
{
    return (USART_DR(usart) & USART_DR_MASK);
}

void
uart_flush(void)
{
    uart_wait_send_ready(CONSOLE_USART);
}

/*
 * cons_rb_put() stores a character in the UART input ring buffer.
 *
 * @param [in]  ch - The character to store in the UART input ring buffer.
 *
 * @return      None.
 */
static void
cons_rb_put(uint ch)
{
    uint new_prod = ((cons_in_rb_producer + 1) % sizeof (cons_in_rb));

    if (new_prod == cons_in_rb_consumer) {
        uart_putchar('%');
        return;  // Would cause ring buffer overflow
    }

    disable_irq();
    cons_in_rb[cons_in_rb_producer] = (uint8_t) ch;
    cons_in_rb_producer = new_prod;
    enable_irq();
}

/*
 * cons_rb_get() returns the next character in the UART input ring buffer.
 *               A value of -1 is returned if there are no characters waiting
 *               to be received in the UART input ring buffer.
 *
 * This function requires no arguments.
 *
 * @return      The next input character.
 * @return      -1 = No input character is pending.
 */
static int
cons_rb_get(void)
{
    uint ch;
    if (cons_in_rb_consumer == cons_in_rb_producer)
        return (-1);  // Ring buffer empty

    uart_console_active = true;
    ch = cons_in_rb[cons_in_rb_consumer];
    cons_in_rb_consumer = (cons_in_rb_consumer + 1) % sizeof (cons_in_rb);
    return (ch);
}

#if 0
/*
 * cons_rb_space() returns a count of the number of characters remaining
 *                 in the UART input ring buffer before the buffer is
 *                 completely full.  A value of 0 means the buffer is
 *                 already full.
 *
 * This function requires no arguments.
 *
 * @return      The number of characters of available space in cons_in_rb.
 */
static uint
cons_rb_space(void)
{
    uint diff = cons_in_rb_consumer - cons_in_rb_producer;
    return (diff + sizeof (cons_in_rb) - 1) % sizeof (cons_in_rb);
}
#endif

/*
 * input_break_pending() returns true if a ^C is pending in the input buffer.
 *
 * This function requires no arguments.
 *
 * @return      1 - break (^C) is pending.
 * @return      0 - no break (^C) is pending.
 */
int
input_break_pending(void)
{
    uint cur;
    uint next;

    for (cur = cons_in_rb_consumer; cur != cons_in_rb_producer; cur = next) {
        next = (cur + 1) % sizeof (cons_in_rb);
        if (cons_in_rb[cur] == 0x03) {  /* ^C is abort key */
            cons_in_rb_consumer = next;
            return (1);
        }
    }

    return (0);
}

void
usb_rb_put(uint ch)
{
    cons_rb_put(ch);
    last_input_source = SOURCE_USB;
}

static void
uart_rb_put(uint ch)
{
    cons_rb_put(ch);
    last_input_source = SOURCE_UART;
}

static void
usb_putchar_flush(void)
{
    if (usb_console_active == 0)
        return;
    if (usb_out_bufpos == 0)
        return;
    if (CDC_Transmit_FS(usb_out_buf, usb_out_bufpos) == USBD_OK)
        usb_out_bufpos = 0;
}

static void
usb_putchar(int ch)
{
    if (usb_out_bufpos < sizeof (usb_out_buf))
        usb_out_buf[usb_out_bufpos++] = ch;
    usb_putchar_flush();
}

static void
usb_putchar_wait(int ch)
{
    if ((usb_console_active == true) &&
        (usb_out_bufpos >= sizeof (usb_out_buf))) {
        /* Buffer is full; need to first force a flush */
        uint64_t timeout = timer_tick_plus_msec(10);
        while (usb_out_bufpos >= sizeof (usb_out_buf)) {
            usb_putchar_flush();
            if (timer_tick_has_elapsed(timeout)) {
                usb_console_active = false;
                return;
            }
        }
    }
    usb_putchar(ch);
}

static int
usb_puts_wait(uint8_t *buf, uint32_t len)
{
    if (usb_console_active == 0)
        return (1);
    if (usb_out_bufpos != 0) {
        /* First flush outstanding text */
        uint64_t timeout = timer_tick_plus_msec(50);
        usb_putchar_flush();
        while (usb_out_bufpos != 0) {
            if (timer_tick_has_elapsed(timeout)) {
                printf("Host Timeout on USB flush\n");
                return (1);
            }
            usb_putchar_flush();
        }
    }
    // XXX: Simplify below loop if both STM32 HAL and opencm3 libraries
    //      support transmitting more than 64 bytes at a time.
    while (len > 0) {
        uint32_t tlen = len;
#ifdef USE_HAL_DRIVER
        if (tlen > 64)
            tlen = 64;
#endif
        if (CDC_Transmit_FS(buf, tlen) != USBD_OK) {
            uint64_t timeout = timer_tick_plus_msec(50);
            while (CDC_Transmit_FS(buf, tlen) != USBD_OK) {
                if (timer_tick_has_elapsed(timeout)) {
                    printf("Host Timeout on USB send\n");
                    return (1);
                }
            }
        }
        len -= tlen;
        buf += tlen;
    }
    return (0);
}

int
puts_binary(void *buf, uint32_t len)
{
    uint8_t *ptr = buf;
    if (last_input_source == SOURCE_UART) {
        while (len-- > 0)
            uart_putchar(*(ptr++));
        return (0);
    } else {
        return (usb_puts_wait(ptr, len));
    }
}

int
putchar(int ch)
{
    static int last_putc = 0;
    if ((ch == '\n') && (last_putc != '\r') && (last_putc != '\n')) {
        uart_putchar('\r');  // Always do CRLF
        usb_putchar_wait('\r');
    }
    last_putc = ch;

    usb_putchar_wait(ch);
    return (uart_putchar(ch));
}

int
puts(const char *str)
{
    while (*str != '\0')
        if (putchar((uint8_t) *(str++)))
            return (1);
    return (putchar('\n'));
}

int
getchar(void)
{
    usb_putchar_flush();  // Ensure USB output is flushed
    usb_poll();

    return (cons_rb_get());
}

void
CONSOLE_IRQHandler(void)
{
    while (USART_SR(CONSOLE_USART) & (USART_SR_RXNE | USART_SR_ORE))
        uart_rb_put(uart_recv(CONSOLE_USART));
}

#ifdef USE_HAL_DRIVER
void
uart_init_irq(void)
{
    HAL_NVIC_SetPriority(CONSOLE_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(CONSOLE_IRQn);
    CONSOLE_USART->CR1 |= USART_CR1_RXNEIE;
}

void
uart_init(void)
{
    uart_init_irq();
}

#else

static void
uart_init_irq(void)
{
    nvic_set_priority(CONSOLE_IRQn, 0);
    nvic_enable_irq(CONSOLE_IRQn);

    USART_CR1(CONSOLE_USART) |= USART_CR1_RXNEIE;
}

void
uart_init(void)
{
#ifdef STM32F4
    /* Rev1 */
    rcc_periph_clock_enable(RCC_USART3);
    rcc_periph_clock_enable(RCC_GPIOC);

    /* USART3 will use PC10 as TX and PC11 as RX */
    gpio_set_af(GPIOC, GPIO_AF7, GPIO10);  // PC10 AltFunc7 = USART3 TX
    gpio_set_af(GPIOC, GPIO_AF7, GPIO11);  // PC11 AltFunc7 = USART3 RX
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO10);
    gpio_mode_setup(GPIOC, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11);
#elif defined(STM32F103xE)
    /* Rev2 */
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_USART1);

    rcc_periph_clock_enable(RCC_GPIOA);
    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9); // CONS_TX
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, GPIO10);          // CONS_RX
#else
    /* Rev3+ */
    rcc_periph_clock_enable(RCC_AFIO);
    rcc_periph_clock_enable(RCC_USART1);
    rcc_periph_clock_enable(RCC_GPIOB);
    AFIO_MAPR |= AFIO_MAPR_USART1_REMAP;
    gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO6); // CONS_TX
    gpio_set_mode(GPIOB, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT, GPIO7);           // CONS_RX

#endif

    /* Setup UART parameters. */
    usart_set_baudrate(CONSOLE_USART, 115200);
    usart_set_databits(CONSOLE_USART, 8);
    usart_set_stopbits(CONSOLE_USART, USART_STOPBITS_1);
    usart_set_mode(CONSOLE_USART, USART_MODE_TX_RX);
    usart_set_parity(CONSOLE_USART, USART_PARITY_NONE);
    usart_set_flow_control(CONSOLE_USART, USART_FLOWCONTROL_NONE);
    usart_enable(CONSOLE_USART);

#ifdef UART_DEBUG
    for (int y = 0; y < 10; y++) {
        for (int x = 0; x < 26; x++) {
            uart_putchar('a' + x);
        }
    }
    uart_putchar('\r');
    uart_putchar('\n');
#endif

    uart_init_irq();
}
#endif
