/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * main prototypes.
 */

#ifndef _MAIN_H
#define _MAIN_H

#if defined(STM32F1XX)
#define STM32F1
#elif defined(STM32F4XX)
#define STM32F4
#endif

typedef unsigned int uint;

#endif /* _MAIN_H */
