/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Generic POSIX function emulation.
 */

#ifndef _UTILS_H
#define _UTILS_H

#ifdef USE_HAL_DRIVER
#define reset_check SystemInit
#endif

#define ADDR8(x)    ((uint8_t *)  ((uintptr_t)(x)))
#define ADDR16(x)   ((uint16_t *) ((uintptr_t)(x)))
#define ADDR32(x)   ((uint32_t *) ((uintptr_t)(x)))

void reset_dfu(void);
void reset_cpu(void);
void reset_check(void);
void show_reset_reason(void);
void identify_cpu(void);

#endif /* _UTILS_H */
