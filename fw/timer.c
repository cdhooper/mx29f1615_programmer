/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 timer and timing handling.
 */

#include "printf.h"
#include "board.h"
#include "main.h"
#include <stdbool.h>
#include "timer.h"
#include "clock.h"

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#define TIM_ARR(x)   ((x)->ARR)
#define TIM_CNT(x)   ((x)->CNT)
#define TIM_DIER(x)  ((x)->DIER)
#define TIM_DR(x)    ((x)->DR)
#define TIM_SR(x)    ((x)->SR)
#define TIM_CR1(x)   ((x)->CR1)
#define TIM_CR2(x)   ((x)->CR2)
#define TIM_SMCR(x)  ((x)->SMCR)
#define RCC_APB1ENR  (RCC->APB1ENR)
#define RCC_APB1RSTR (RCC->APB1RSTR)
#define nvic_set_priority(irq, pri) \
                             HAL_NVIC_SetPriority(irq, (pri) >> 4, (pri) & 0xf)
#define nvic_enable_irq(irq) HAL_NVIC_EnableIRQ(irq)
#define tim2_isr TIM2_IRQHandler
#define NVIC_TIM2_IRQ TIM2_IRQn
#define TIM_CR1_CKD_CK_INT_MASK TIM_CR1_CKD_Msk
#define TIM_CR1_CMS_MASK TIM_CR1_CMS_Msk
#define TIM_CR1_DIR_DOWN TIM_CR1_DIR_Msk
#define TIM_CR2_MMS_MASK TIM_CR2_MMS_Msk
#define TIM_CR2_MMS_UPDATE TIM_CR2_MMS_1
#define TIM_SMCR_TS_ITR0 0
#define TIM_SMCR_TS_ITR1 TIM_SMCR_TS_0
#define TIM_SMCR_TS_ITR2 TIM_SMCR_TS_1
#define TIM_SMCR_TS_ITR3 (TIM_SMCR_TS_0 | TIM_SMCR_TS_1)
#define TIM_SMCR_SMS_ECM1 (TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2)
#else
/* libopencm3 */
#include <libopencm3/stm32/timer.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/cm3/nvic.h>
#endif

/*
 * STM32F4 TIM2 implements a 32-bit counter. This allows us to very easily
 * implement a 64-bit clock tick value by software incrementing the top
 * 32 bits on the 32-bit rollover every ~72 seconds.
 *
 * STM32F1 does not have a 32-bit counter on any timer, but two timers can
 * be chained to form a 32-bit counter. Because of this capability, we can
 * still implement a 64-bit clock tick value, but the code is a bit more
 * complicated. For that reason, the the low level routines must be slightly
 * different.
 */

static volatile uint32_t timer_high = 0;

/**
 * __dmb() implements a Data Memory Barrier.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
__attribute__((always_inline))
static inline void __dmb(void)
{
    __asm__ volatile("dmb");
}

void
tim2_isr(void)
{
    uint32_t flags = TIM_SR(TIM2) & TIM_DIER(TIM2);

    TIM_SR(TIM2) = ~flags;  // Clear observed flags

    if (flags & TIM_SR_UIF)
        timer_high++;  // Increment upper bits of 64-bit timer value

    if (flags & ~TIM_SR_UIF) {
        TIM_DIER(TIM2) &= ~(flags & ~TIM_SR_UIF);
        printf("Unexpected TIM2 IRQ: %04lx\n", flags & ~TIM_SR_UIF);
    }
}

#ifdef STM32F1
uint64_t
timer_tick_get(void)
{
    uint32_t high   = timer_high;
    uint32_t high16 = TIM_CNT(TIM2);
    uint32_t low16  = TIM_CNT(TIM3);

    /*
     * A Data Memory Barrier (ARM "dmb") here is necessary to prevent
     * a pipeline fetch of timer_high before the TIM2 CNT fetch has
     * completed. Without it, a timer update interrupt happening at
     * this point could potentially exhibit a non-monotonic clock.
     */
    __dmb();

    /*
     * Check for unhandled timer rollover. Note this must be checked
     * twice due to an ARM pipelining race with interrupt context.
     */
    if ((TIM_SR(TIM2) & TIM_SR_UIF) && (TIM_SR(TIM2) & TIM_SR_UIF)) {
        high++;
        if ((low16 > TIM_CNT(TIM3)) || (high16 > TIM_CNT(TIM2))) {
            /* timer wrapped */
            high16 = TIM_CNT(TIM2);
            low16 = TIM_CNT(TIM3);
        }
    } else if ((high16 != TIM_CNT(TIM2)) || (high != timer_high)) {
        /* TIM3 or interrupt rollover */
        high   = timer_high;
        high16 = TIM_CNT(TIM2);
        low16  = TIM_CNT(TIM3);
    }
    return (((uint64_t) high << 32) | (high16 << 16) | low16);
}

void
timer_init(void)
{
    /*
     * TIM3 is the low 16 bits of the 32-bit counter.
     * TIM2 is the high 16 bits of the 32-bit counter.
     * We chain a rollover of TIM3 to increment TIM2.
     * TIM2 rollover causes an interrupt, which software
     * uses to then increment the upper 32-bit part of
     * the 64-bit system tick counter.
     */

    /* Enable and reset TIM2 and TIM3 */
    RCC_APB1ENR  |=   RCC_APB1ENR_TIM2EN   | RCC_APB1ENR_TIM3EN;
    RCC_APB1RSTR |=   RCC_APB1RSTR_TIM2RST | RCC_APB1RSTR_TIM2RST;
    RCC_APB1RSTR &= ~(RCC_APB1RSTR_TIM2RST | RCC_APB1RSTR_TIM2RST);

    /* Set timer CR1 mode (No clock division, Edge, Dir Up) */
    TIM_CR1(TIM2) &= ~(TIM_CR1_CKD_CK_INT_MASK | TIM_CR1_CMS_MASK | TIM_CR1_DIR_DOWN);
    TIM_CR1(TIM3) &= ~(TIM_CR1_CKD_CK_INT_MASK | TIM_CR1_CMS_MASK | TIM_CR1_DIR_DOWN);

    TIM_ARR(TIM2)  = 0xffff;       // Set period (rollover at 2^16)
    TIM_ARR(TIM3)  = 0xffff;       // Set period (rollover at 2^16)
    TIM_CR1(TIM3) |= TIM_CR1_URS;  // Update on overflow
    TIM_CR1(TIM3) &= ~TIM_CR1_OPM; // Continuous mode

    /* TIM3 is master - generate TRGO to TIM2 on rollover (UEV) */
    TIM_CR2(TIM3) &= TIM_CR2_MMS_MASK;
    TIM_CR2(TIM3) |= TIM_CR2_MMS_UPDATE;

    /* TIM2 is slave of TIM3 (ITR2) per Table 86 */
    TIM_SMCR(TIM2) = 0;
    TIM_SMCR(TIM2) |= TIM_SMCR_TS_ITR2;

    /* TIM2 has External Clock Mode 1 (increment on rising edge of TRGI) */
    TIM_SMCR(TIM2) |= TIM_SMCR_SMS_ECM1;

    /* Enable counters */
    TIM_CR1(TIM2)  |= TIM_CR1_CEN;
    TIM_CR1(TIM3)  |= TIM_CR1_CEN;

    /* Enable TIM2 rollover interrupt, but not TIE (interrupt on trigger) */
    TIM_DIER(TIM2) |= TIM_DIER_UIE | TIM_DIER_TDE;
    nvic_set_priority(NVIC_TIM2_IRQ, 0x11);
    nvic_enable_irq(NVIC_TIM2_IRQ);
}

void
timer_delay_ticks(uint32_t ticks)
{
    uint32_t start = TIM_CNT(TIM3);
    while (TIM_CNT(TIM3) - start < ticks) {
        /* Empty */
    }
}

#else  /* STM32F407 */

uint64_t
timer_tick_get(void)
{
    uint32_t high = timer_high;
    uint32_t low  = TIM_CNT(TIM2);

    /*
     * A Data Memory Barrier (ARM "dmb") here is necessary to prevent
     * a pipeline fetch of timer_high before the TIM2 CNT fetch has
     * completed. Without it, a timer update interrupt happening at
     * this point could potentially exhibit a non-monotonic clock.
     */
    __dmb();

    /*
     * Check for unhandled timer rollover. Note this must be checked
     * twice due to an ARM pipelining race with interrupt context.
     */
    if ((TIM_SR(TIM2) & TIM_SR_UIF) && (TIM_SR(TIM2) & TIM_SR_UIF)) {
        high++;
        if (low > TIM_CNT(TIM2))
            low = TIM_CNT(TIM2);
    } else if (high != timer_high) {
        low = TIM_CNT(TIM2);
        high = timer_high;
    }
    return (((uint64_t) high << 32) | low);
}

void
timer_init(void)
{
    /* Enable and reset TIM2 */
    RCC_APB1ENR  |=  RCC_APB1ENR_TIM2EN;
    RCC_APB1RSTR |=  RCC_APB1RSTR_TIM2RST;
    RCC_APB1RSTR &= ~RCC_APB1RSTR_TIM2RST;

    /* Set TIM2 CR1 mode (CK INT, Edge, Dir Up) */
    TIM_CR1(TIM2) &= ~(TIM_CR1_CKD_CK_INT_MASK | TIM_CR1_CMS_MASK | TIM_CR1_DIR_DOWN);

    TIM_ARR(TIM2)  = 0xffffffff;   // Set period (rollover at 2^32)
    TIM_CR1(TIM2) |= TIM_CR1_URS;  // Update on overflow
    TIM_CR1(TIM2) &= ~TIM_CR1_OPM; // Continuous mode
    TIM_CR1(TIM2) |= TIM_CR1_CEN;  // Enable counter

    /* Enable TIM2 rollover interrupt */
    TIM_DIER(TIM2) |= TIM_DIER_TIE | TIM_DIER_UIE | TIM_DIER_TDE;
    nvic_set_priority(NVIC_TIM2_IRQ, 0x11);
    nvic_enable_irq(NVIC_TIM2_IRQ);
}

void
timer_delay_ticks(uint32_t ticks)
{
    uint32_t start = TIM_CNT(TIM2);
    while (TIM_CNT(TIM2) - start < ticks) {
        /* Empty */
    }
}
#endif

/**
 * timer_usec_to_tick() converts the specified number of microseconds to an
 *                      equivalent number of timer ticks.
 *
 * @param [in]  usec - The number of microseconds to convert.
 *
 * @return      The equivalent number of timer ticks.
 */
uint64_t
timer_usec_to_tick(uint usec)
{
    uint64_t ticks_per_usec = rcc_pclk2_frequency / 1000000;  // nominal 60 MHz
    return (ticks_per_usec * usec);
}

/*
 * timer_nsec_to_tick() converts the specified number of nanoseconds to an
 *                      equivalent number of timer ticks.
 *
 * @param [in]  nsec - The number of nanoseconds to convert.
 *
 * @return      The equivalent number of timer ticks.
 */
uint32_t
timer_nsec_to_tick(uint nsec)
{
    return (rcc_pclk2_frequency / 1000000 * nsec / 1000);
}

/**
 * timer_tick_to_usec() converts a tick timer count to microseconds.
 *                      This function is useful for reporting time
 *                      difference measurements.
 *
 * @param [in]  value - The tick timer count.
 *
 * @return      The converted microseconds value.
 *
 * Example usage:
 *     uint64_t start, end;
 *     start = timer_tick_get();
 *     measure_func();
 *     end = timer_tick_get();
 *     printf("diff=%lu us\n", (uint32_t)timer_tick_to_usec(end - start));
 */
uint64_t
timer_tick_to_usec(uint64_t value)
{
    return (value / (rcc_pclk2_frequency / 1000000));
}

/**
 * timer_tick_has_elapsed() indicates whether the specified tick timer value
 *                          has already elapsed.
 *
 * @param [in]  value - The value to compare against the current tick timer.
 *
 * @return      true  - The specified value has elapsed.
 * @return      false - The specified value has not yet elapsed.
 *
 * Example usage: See timer_tick_plus_msec()
 */
bool
timer_tick_has_elapsed(uint64_t value)
{
    uint64_t now = timer_tick_get();

    if (now >= value)
        return (true);

    return (false);
}

/**
 * timer_tick_plus_msec() returns what the tick timer value will be when the
 *                        specified number of milliseconds have elapsed.
 *                        This function is useful for computing timeouts.
 *
 * @param [in]  msec - The number of milliseconds to add to the current
 *                     tick timer value.
 *
 * @return      The value of the tick timer when the specified number of
 *              milliseconds have elapsed.
 *
 * Example usage:
 *     uint64_t timeout = timer_tick_plus_msec(1000);  // Expire in 1 second
 *
 *     while (wait_for_condition() == false)
 *         if (timer_tick_has_elapsed(timeout)) {
 *             printf("Condition timeout\n");
 *             return (RC_TIMEOUT);
 *         }
 */
uint64_t
timer_tick_plus_msec(uint msec)
{
    uint64_t ticks_per_msec = rcc_pclk2_frequency / 1000;
    return (timer_tick_get() + ticks_per_msec * msec);
}

/**
 * timer_tick_plus_usec() returns what the tick timer value will be when the
 *                        specified number of microseconds have elapsed. This
 *                        function is useful for computing timeouts.
 *
 * @param [in]  usec - The number of microseconds to add to the current
 *                     tick timer value.
 *
 * @return      The value of the tick timer when the specified number of
 *              microseconds have elapsed.
 */
uint64_t
timer_tick_plus_usec(uint usec)
{
    uint64_t ticks_per_usec = rcc_pclk2_frequency / 1000000;
    return (timer_tick_get() + ticks_per_usec * usec);
}

/**
 * timer_delay_msec() delays the specified number of milliseconds.
 *
 * @param [in]  msec - The number of milliseconds to delay.
 *
 * @return      None.
 */
void
timer_delay_msec(uint msec)
{
    uint64_t end = timer_tick_plus_msec(msec);
    while (timer_tick_has_elapsed(end) == false) {
        /* Empty */
    }
}

/**
 * timer_delay_usec() delays the specified number of microseconds.
 *
 * @param [in]  usec - The number of microseconds to delay.
 *
 * @return      None.
 */
void
timer_delay_usec(uint usec)
{
    uint64_t end = timer_tick_plus_usec(usec);
    while (timer_tick_has_elapsed(end) == false) {
        /* Empty */
    }
}
