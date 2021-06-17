/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * USB handling.
 */

#ifndef _MY_USBD_DESC_H
#define _MY_USBD_DESC_H

#ifndef USE_HAL_DRIVER
/* libopencm3 */
#include <libopencm3/usb/usbd.h>
#define USBD_OK 0U
extern usbd_device *usbd_gdev;
#endif

void usb_shutdown(void);
void usb_startup(void);
void usb_signal_reset_to_host(int restart);
void usb_poll(void);
void usb_mask_interrupts(void);
void usb_unmask_interrupts(void);
void usb_show_regs(void);

uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len);

extern uint8_t usb_console_active;

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#define USB_BASE_F1 USB_BASE
#define USB_BASE_F4 USB_OTG_FS_PERIPH_BASE

#if defined(STM32F103xE)
#define USB_PERIPH_BASE USB_BASE_F1
#else
#define USB_PERIPH_BASE USB_BASE_F4
#endif

#else
/* libopencm3 */
// #include <libopencm3/stm32/memorymap.h>
#if defined(STM32F103xE)
#define USB_PERIPH_BASE USB_DEV_FS_BASE
#else
#define USB_PERIPH_BASE USB_OTG_FS_BASE
#endif
#endif /* libopencm3 */

#endif /* _MY_USBD_DESC_H */
