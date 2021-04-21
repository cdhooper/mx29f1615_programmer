/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * STM32 USB Device Descriptor handling.
 */

#include "printf.h"
#include "utils.h"
#include <stdbool.h>
#include "usb.h"
#include "main.h"
#include "gpio.h"
#include "uart.h"
#include "timer.h"

#undef DEBUG_NO_USB

#ifdef USE_HAL_DRIVER
/* ST-Micro HAL Library compatibility definitions */
#include <usbd_core.h>
#include <usbd_desc.h>
#include <usbd_conf.h>
#include <usb_device.h>
#ifdef STM32F1
#include <stm32f1xx_ll_usb.h>
#else
#endif
#define USB_DT_DEVICE USB_DESC_TYPE_DEVICE
#define USB_DT_STRING USB_DESC_TYPE_STRING
#define GPIO_OSPEED_LOW GPIO_SPEED_FREQ_LOW
#define DESIG_UNIQUE_ID_BASE UID_BASE

#if defined(STM32F103xE)
#define USB_INTERRUPT USB_LP_CAN1_RX0_IRQn
#else
#define USB_INTERRUPT OTG_FS_IRQn
#endif

#define nvic_enable_irq(x)  HAL_NVIC_EnableIRQ(x)
#define nvic_disable_irq(x) HAL_NVIC_DisableIRQ(x)

#else
/* libopencm3 */
#include <libopencm3/stm32/gpio.h>
#if defined(STM32F1)
#include <libopencm3/stm32/f1/gpio.h>
#include <libopencm3/stm32/f1/rcc.h>
#include <libopencm3/stm32/f1/nvic.h>
#ifdef STM32F103xE
#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/tools.h>
#include <libopencm3/stm32/st_usbfs.h>
#include <libopencm3/usb/usbd.h>
#endif
#elif defined(STM32F4)
#include <libopencm3/stm32/f4/rcc.h>
#include <libopencm3/stm32/f4/nvic.h>
#endif
#include <libopencm3/usb/usbstd.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/dwc/otg_fs.h>
#include <string.h>

#if defined(STM32F103xE)
#define USB_INTERRUPT NVIC_USB_LP_CAN_RX0_IRQ
#else
#define USB_INTERRUPT NVIC_OTG_FS_IRQ
#endif

#define USB_MAX_EP0_SIZE 64
#define DEVICE_CLASS_MISC 0xef
#define GPIO_OSPEED_LOW GPIO_MODE_OUTPUT_2_MHZ

#define USBD_IDX_MFC_STR           0x01U
#define USBD_IDX_PRODUCT_STR       0x02U
#define USBD_IDX_SERIAL_STR        0x03U
#define USBD_MAX_NUM_CONFIGURATION 0x01
#define USBD_MAX_STR_DESC_SIZ      0x100

#define USING_USB_INTERRUPT

#endif  /* opencm3 */

#ifdef STM32F103xE
#define DRIVE_USB_PULLUP
#endif

/*
 * XXX: Need to register USB device ID at http://pid.codes once board files
 *      and source code have been published.
 */
#define USBD_MANUFACTURER_STRING        "eebugs"
#define USBD_VID                        0x1209
#define USBD_PID                        0x1615
#define BYTESPLIT(x)                    (uint8_t) (x), (uint8_t) ((x) >> 8)

#define DEVICE_CLASS_APPSPEC            0xfe  // Application-specific
#define DEVICE_CLASS_MISC               0xef  // Miscellaneous
#define DEVICE_SUBCLASS_MISC_COMMON     0x02

#define DEVICE_PROTOCOL_MISC_IAD        0x01  // Interface Association Descr.

#define USB_SIZE_DEVICE_DESC            0x12
#define USB_SIZE_STRING_LANGID          0x04
#define USBD_LANGID_STRING              0x409

/*
 * #define STM32F0_UDID_BASE            0x1ffff7ac
 * #define STM32F1_UDID_BASE            0x1ffff7e8
 * #define STM32F2_UDID_BASE            0x1fff7a10
 * #define STM32F3_UDID_BASE            0x1ffff7ac
 * #define STM32F4_UDID_BASE            0x1fff7a10
 * #define STM32F7_UDID_BASE            0x1ff0f420
 */
#define STM32_UDID_LEN                  12    // 96 bits
#define STM32_UDID_BASE                 DESIG_UNIQUE_ID_BASE

/* CDC includes the VCP endpoint and a Bulk transport */
#define USBD_CONFIGURATION_FS_STRING    "CDC Config"
#define USBD_INTERFACE_FS_STRING        "CDC Interface"

#define ARRAY_SIZE(x) (int)((sizeof (x) / sizeof ((x)[0])))

static bool using_usb_interrupt = false;
uint8_t usb_console_active = false;

/**
 * Device Descriptor for USB FS port
 */
__attribute__((aligned(4)))
static const uint8_t USBD_FS_DeviceDesc[USB_SIZE_DEVICE_DESC] = {
    USB_SIZE_DEVICE_DESC,        /* bLength */
    USB_DT_DEVICE,               /* bDescriptorType */
    BYTESPLIT(0x0200),           /* bcdUSB = 2.00 */
    DEVICE_CLASS_MISC,           /* bDeviceClass    (Misc)   */
    DEVICE_SUBCLASS_MISC_COMMON, /* bDeviceSubClass (common) */
    DEVICE_PROTOCOL_MISC_IAD,    /* bDeviceProtocol (IAD)    */
    USB_MAX_EP0_SIZE,            /* bMaxPacketSize */
    BYTESPLIT(USBD_VID),         /* idVendor */
    BYTESPLIT(USBD_PID),         /* idProduct */
    BYTESPLIT(0x0200),           /* bcdDevice rel. 2.00 */
    USBD_IDX_MFC_STR,            /* Index of manufacturer string */
    USBD_IDX_PRODUCT_STR,        /* Index of product string */
    USBD_IDX_SERIAL_STR,         /* Index of serial number string */
    USBD_MAX_NUM_CONFIGURATION   /* bNumConfigurations */
};

#ifdef USE_HAL_DRIVER
/*
 * String buffer for return of USB descriptor data.
 */
__attribute__((aligned(4)))
static uint8_t usbd0_strdesc[USBD_MAX_STR_DESC_SIZ];

/*
 * USB Language ID descriptor.
 */
__attribute__((aligned(4)))
static const uint8_t usbd_lang_id_desc[USB_SIZE_STRING_LANGID] = {
    sizeof (usbd_lang_id_desc),
    USB_DT_STRING,
    BYTESPLIT(USBD_LANGID_STRING),
};
#endif

void
usb_mask_interrupts(void)
{
#ifdef DEBUG_NO_USB
    return;
#endif
    if (!using_usb_interrupt)
        return;
    nvic_disable_irq(USB_INTERRUPT);
}

void
usb_unmask_interrupts(void)
{
#ifdef DEBUG_NO_USB
    return;
#endif
    if (!using_usb_interrupt)
        return;
    nvic_enable_irq(USB_INTERRUPT);
}

#ifdef USE_HAL_DRIVER
/**
 * usbd_usr_lang_descriptor() returns a USB Language ID which is suitable for
 *                            return response to the USB Get Language
 *                            Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the language descriptor.
 *
 * @return      The language descriptor.
 */
static uint8_t *
usbd_usr_lang_descriptor(uint8_t speed, uint16_t *length)
{
    *length = sizeof (usbd_lang_id_desc);
    return ((uint8_t *) usbd_lang_id_desc);  // Stack treats reply as const
}
#endif

/**
 * usbd_stm32_serial() reads the STM32 Unique Device ID from the CPU's system
 *                     memory area of the Flash memory module.  It converts
 *                     the ID to a printable Unicode string which is suitable
 *                     for return response to the USB Get Serial Descriptor
 *                     request.
 *
 * @param [out] buf    - A buffer to hold the converted Unicode serial number.
 * @param [out] length - The length of the Unicode serial number.
 *
 * @return      The converted Unicode serial number.
 */
static uint8_t *
usbd_usr_serial(uint8_t *buf, uint16_t *length)
{
    uint len = 0;
    uint pos;

#ifdef USE_HAL_DRIVER
    buf[len++] = 0;
    buf[len++] = USB_DT_STRING;
#endif

    for (pos = 0; pos < STM32_UDID_LEN; pos++) {
        uint8_t temp  = *ADDR8(STM32_UDID_BASE + pos);
        uint8_t ch_hi = (temp >> 4) + '0';
        uint8_t ch_lo = (temp & 0xf) + '0';

        if (temp == 0xff)
            continue;
        if ((temp >= '0') && (temp <= 'Z')) {
            /* Show ASCII directly */
            buf[len++] = temp;
#ifdef USE_HAL_DRIVER
            buf[len++] = '\0';
#endif
            continue;
        }
        if (ch_hi > '9')
            ch_hi += 'a' - '0' - 10;
        if (ch_lo > '9')
            ch_lo += 'a' - '0' - 10;

#ifdef USE_HAL_DRIVER
        buf[len++] = ch_hi;
        buf[len++] = '\0';
        buf[len++] = ch_lo;
        buf[len++] = '\0';
#else
        buf[len++] = ch_hi;
        buf[len++] = ch_lo;
#endif
    }
    buf[len++] = '\0';
#ifdef USE_HAL_DRIVER
    buf[len++] = '\0';
    buf[0] = (uint8_t) len;
#endif
    *length = (uint16_t) len;
    return (buf);
}

#ifdef USE_HAL_DRIVER
/**
 * usbd_usr_device_descriptor() returns a device descriptor which is suitable
 *                              for return response to the USB Get Device
 *                              Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the device descriptor.
 *
 * @return      The device descriptor.
 */
static uint8_t *
usbd_usr_device_descriptor(uint8_t speed, uint16_t *length)
{
    *length = sizeof (USBD_FS_DeviceDesc);

    /* USB driver doesn't modify DeviceDesc, I hope */
    return ((uint8_t *) USBD_FS_DeviceDesc);
}

/**
 * usbd_usr_mfg_descriptor() returns a Unicode format manufacturer descriptor
 *                           string which is suitable for return response to
 *                           the USB Get Manufacturer Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the manufacturer descriptor string.
 *
 * @return      The Unicode manufacturer descriptor string.
 */
static uint8_t *
usbd_usr_mfg_descriptor(uint8_t speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, usbd0_strdesc, length);
    return (usbd0_strdesc);
}

/**
 * usbd_usr_product_descriptor() returns a Unicode format product descriptor
 *                               string which is suitable for return response
 *                               to the USB Get Product Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the product descriptor string.
 *
 * @return      The Unicode product descriptor string.
 */
static uint8_t *
usbd_usr_product_descriptor(uint8_t speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)"MX29F1615 Prg", usbd0_strdesc, length);
    return (usbd0_strdesc);
}

/**
 * usbd_usr_serial_descriptor() returns a Unicode format product serial
 *                              string which is suitable for return response
 *                              to the USB Get Serial Descriptor request.
 *                              This string is guaranteed to be unique
 *                              per part (but common across both USB ports)
 *                              because it is generated based on the STM32
 *                              Unique Device ID.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the serial descriptor string.
 *
 * @return      The Unicode serial descriptor string.
 */
static uint8_t *
usbd_usr_serial_descriptor(uint8_t speed, uint16_t *length)
{
    return (usbd_usr_serial(usbd0_strdesc, length));
}

/**
 * usbd_usr_config_descriptor() returns a Unicode format configuration
 *                              descriptor string which is suitable for
 *                              return response to the USB Get Config
 *                              Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the configuration descriptor string.
 *
 * @return      The Unicode configuration descriptor string.
 */
static uint8_t *
usbd_usr_config_descriptor(uint8_t speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_FS_STRING, usbd0_strdesc, length);
    return (usbd0_strdesc);
}

/**
 * usbd_usr_interface_descriptor() returns a Unicode format interface
 *                                 descriptor string which is suitable for
 *                                 return response to the USB Get Interface
 *                                 Descriptor request.
 *
 * @param [in]  speed  - Current device speed (USB_OTG_SPEED_FULL or
 *                       USB_OTG_SPEED_HIGH) - ignored.
 * @param [out] length - The length of the interface descriptor string.
 *
 * @return      The Unicode interface descriptor string.
 */
static uint8_t *
usbd_usr_interface_descriptor(uint8_t speed, uint16_t *length)
{
    USBD_GetString((uint8_t *)USBD_INTERFACE_FS_STRING, usbd0_strdesc, length);
    return (usbd0_strdesc);
}
#endif

/**
 * USB descriptors
 */
#ifdef USE_HAL_DRIVER
USBD_DescriptorsTypeDef FS_Desc = {
    usbd_usr_device_descriptor,
    usbd_usr_lang_descriptor,
    usbd_usr_mfg_descriptor,
    usbd_usr_product_descriptor,
    usbd_usr_serial_descriptor,
    usbd_usr_config_descriptor,
    usbd_usr_interface_descriptor,
};
#else
/* Strings describing this USB device */
static const char *usb_strings[] = {
    USBD_MANUFACTURER_STRING,
    "MX29F1615 Prg",
    "",  // Serial number assigned at runtime
};
#endif

void
usb_shutdown(void)
{
#ifdef USE_HAL_DRIVER
//  extern PCD_HandleTypeDef hUsbDeviceFS;
    extern USBD_HandleTypeDef hUsbDeviceFS;

    USBD_Stop(&hUsbDeviceFS);
    USBD_DeInit(&hUsbDeviceFS);
    HAL_Delay(10);
    usb_console_active = false;
#endif
}

void usb_poll(void)
{
#ifdef USE_HAL_DRIVER
    /* Nothing to do for STM32 HAL driver, as this library is interrupt-based */
#else
#ifndef DEBUG_NO_USB
    if (!using_usb_interrupt)
        usbd_poll(usbd_gdev);
#endif
#endif
}

#ifdef USE_HAL_DRIVER
void
usb_signal_reset_to_host(int restart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* Set USB_PULLUP pin as 1 (this turns off the pullup) */
    HAL_GPIO_WritePin(USB_PULLUP_GPIO_Port, USB_PULLUP_Pin, 1);

    /* Configure USB_PULLUP pin as output */
    GPIO_InitStruct.Pin = USB_PULLUP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_OSPEED_LOW;
    HAL_GPIO_Init(USB_PULLUP_GPIO_Port, &GPIO_InitStruct);

    /* Configure USB pins as input */
    GPIO_InitStruct.Pin = USB_DM_Pin | USB_DP_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_OSPEED_LOW;
    HAL_GPIO_Init(USB_DPDM_Port, &GPIO_InitStruct);

    HAL_Delay(10);

    if (restart) {
        /* Turn back on USB pullup */
        HAL_GPIO_WritePin(USB_PULLUP_GPIO_Port, USB_PULLUP_Pin, 0);
#if 0
        GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
        HAL_GPIO_Init(USB_PULLUP_GPIO_Port, &GPIO_InitStruct);
#endif
        HAL_Delay(1);
    }
}
#else
void
usb_signal_reset_to_host(int restart)
{
#ifdef DEBUG_NO_USB
    return;
#endif
#if 0
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_AFIO);

#endif
#ifdef DRIVE_USB_PULLUP
    gpio_setv(USB_PULLUP_PORT, USB_PULLUP_PIN, 1);
    gpio_set_mode(USB_PULLUP_PORT, GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL, USB_PULLUP_PIN);
    if (restart) {
        timer_delay_msec(10);
        gpio_setv(USB_PULLUP_PORT, USB_PULLUP_PIN, 0);  // Enable pull-up
    }
#endif
}
#endif

#ifndef USE_HAL_DRIVER
/* libopencm3 */
static uint8_t *gbuf;                      // Start of current packet
static uint16_t glen;                      // Total length of current packet
static uint16_t gpos;                      // Progress position of current pkt
static volatile bool preparing_packet = false;  // Preparing to send new packet

/*
 * CDC_Transmit_FS() is used to put data in the USB transmit FIFO so that
 *                   the hardware can provide the data to the host.
 *
 * Note that this function might get called for every character. The caller
 * is responsible for checking for error return (which might just mean that
 * the USB hardware is currently busy sending the previous packet).
 *
 * Do not send more than the configured maximum packet size (wMaxPacketSize)
 * as this will overrun hardware buffers.
 */
uint8_t
CDC_Transmit_FS(uint8_t *buf, uint16_t len)
{
#ifdef DEBUG_NO_USB
    return (USBD_OK);
#endif
    uint64_t timeout = timer_tick_plus_msec(10);
    bool     first = true;
    if (usb_console_active == false)
        return (-1);
    preparing_packet = true;

    while (len > 0) {
        uint16_t tlen = len;
        uint16_t rc;
        if (tlen > 64)
            tlen = 64;
        usb_poll();
        usb_mask_interrupts();
        rc = usbd_ep_write_packet(usbd_gdev, 0x82, buf, tlen);
        usb_unmask_interrupts();
        if (rc == 0) {
            if (first == true)
                return (-1);
            if (timer_tick_has_elapsed(timeout))
                return (-1);
        } else {
            first = false;
            len -= tlen;
            buf += tlen;
            timeout = timer_tick_plus_msec(10);
        }
    }
    return (USBD_OK);
}
#endif

#ifndef USE_HAL_DRIVER
/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 */
static const struct usb_endpoint_descriptor comm_endp[] = {
    {
        .bLength          = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType  = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x83,
        .bmAttributes     = USB_ENDPOINT_ATTR_INTERRUPT,
        .wMaxPacketSize   = 16,
        .bInterval        = 255,
    }
};

static const struct usb_endpoint_descriptor data_endp[] = {
    {
        .bLength          = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType  = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x01,
        .bmAttributes     = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize   = 64,
        .bInterval        = 1,
    }, {
        .bLength          = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType  = USB_DT_ENDPOINT,
        .bEndpointAddress = 0x82,
        .bmAttributes     = USB_ENDPOINT_ATTR_BULK,
        .wMaxPacketSize   = 64,
        .bInterval        = 1,
    }
};

static const struct {
        struct usb_cdc_header_descriptor header;
        struct usb_cdc_call_management_descriptor call_mgmt;
        struct usb_cdc_acm_descriptor acm;
        struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
    .header = {
        .bFunctionLength    = sizeof(struct usb_cdc_header_descriptor),
        .bDescriptorType    = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_HEADER,
        .bcdCDC = 0x0110,
    },
    .call_mgmt = {
        .bFunctionLength    = sizeof(struct usb_cdc_call_management_descriptor),
        .bDescriptorType    = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
        .bmCapabilities     = 0,
        .bDataInterface     = 1,
    },
    .acm = {
        .bFunctionLength    = sizeof(struct usb_cdc_acm_descriptor),
        .bDescriptorType    = CS_INTERFACE,
        .bDescriptorSubtype = USB_CDC_TYPE_ACM,
        .bmCapabilities     = 0,
    },
    .cdc_union = {
        .bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
        .bDescriptorType        = CS_INTERFACE,
        .bDescriptorSubtype     = USB_CDC_TYPE_UNION,
        .bControlInterface      = 0,
        .bSubordinateInterface0 = 1,
     }
};

static const struct usb_interface_descriptor comm_iface[] = {
    {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 0,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 1,
        .bInterfaceClass    = USB_CLASS_CDC,
        .bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
        .bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
        .iInterface         = 0,

        .endpoint           = comm_endp,

        .extra              = &cdcacm_functional_descriptors,
        .extralen           = sizeof (cdcacm_functional_descriptors)
    }
};

static const struct usb_interface_descriptor data_iface[] = {
    {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType    = USB_DT_INTERFACE,
        .bInterfaceNumber   = 1,
        .bAlternateSetting  = 0,
        .bNumEndpoints      = 2,
        .bInterfaceClass    = USB_CLASS_DATA,
        .bInterfaceSubClass = 0,
        .bInterfaceProtocol = 0,
        .iInterface         = 0,

        .endpoint           = data_endp,
    }
};

static const struct usb_interface ifaces[] = {
    {
        .num_altsetting     = 1,
        .altsetting         = comm_iface,
    }, {
        .num_altsetting     = 1,
        .altsetting         = data_iface,
    }
};

static const struct usb_config_descriptor config = {
    .bLength = USB_DT_CONFIGURATION_SIZE,
    .bDescriptorType = USB_DT_CONFIGURATION,
    .wTotalLength        = 0,
    .bNumInterfaces      = 2,
    .bConfigurationValue = 1,
    .iConfiguration      = 0,
    .bmAttributes        = 0x80,
    .bMaxPower           = 0x32,

    .interface           = ifaces,
};

/* Buffer to be used for control requests. */
static uint8_t usbd_control_buffer[128];

#define USB_CDC_REQ_GET_LINE_CODING 0x21  // Not defined in libopencm3

static enum usbd_request_return_codes
cdcacm_control_request(usbd_device *usbd_dev, struct usb_setup_data *req,
                       uint8_t **buf, uint16_t *len,
                       void (**complete)(usbd_device *usbd_dev,
                       struct usb_setup_data *req))
{
    static struct usb_cdc_line_coding line_coding = {
        .dwDTERate   = 115200,
        .bCharFormat = USB_CDC_1_STOP_BITS,
	.bParityType = USB_CDC_NO_PARITY,
	.bDataBits   = 0x08,
    };
    switch (req->bRequest) {
        case USB_CDC_REQ_SET_CONTROL_LINE_STATE: {
            /*
             * This Linux cdc_acm driver requires this to be implemented
             * even though it's optional in the CDC spec, and we don't
             * advertise it in the ACM functional descriptor.
             */
            char local_buf[10];
            struct usb_cdc_notification *notif = (void *)local_buf;

            /* We echo signals back to host as notification. */
            notif->bmRequestType = 0xA1;
            notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
            notif->wValue = 0;
            notif->wIndex = 0;
            notif->wLength = 2;
            local_buf[8] = req->wValue & 3;
            local_buf[9] = 0;
            // usbd_ep_write_packet(usbd_gdev, 0x83, buf, 10);
            return (USBD_REQ_HANDLED);
        }
        case USB_CDC_REQ_SET_LINE_CODING:
            /* Windows 10 VCP driver requires this */
            if (*len < sizeof (struct usb_cdc_line_coding))
                return USBD_REQ_NOTSUPP;
            memcpy(&line_coding, *buf, sizeof (struct usb_cdc_line_coding));
            return (USBD_REQ_HANDLED);
        case USB_CDC_REQ_GET_LINE_CODING:
            usbd_ep_write_packet(usbd_gdev, 0x83, &line_coding,
                                 sizeof (struct usb_cdc_line_coding));
            return (USBD_REQ_HANDLED);
    }
    return (USBD_REQ_NOTSUPP);
}

/*
 * cdcacm_rx_cb() gets called when the USB hardware has received data from
 *                the host on the data OUT endpoint (0x01).
 */
static void cdcacm_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    char buf[64];
    int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, sizeof (buf));

    if (len > 0) {
        int pos;
        usb_console_active = true;
        for (pos = 0; pos < len; pos++)
            usb_rb_put(buf[pos]);
    }
}

/*
 * cdcacm_tx_cb() gets called when the USB hardware has sent the previous
 *                frame on the IN endpoint (0x82). It can be used to continue
 *                sending queuing frames, but this code does not use it in
 *                that manner.
 */
static void cdcacm_tx_cb(usbd_device *usbd_dev, uint8_t ep)
{
    if (preparing_packet)
        return;  // New transmit packet is being prepared

    // XXX: If the final transfer was exactly a multiple of a packet
    //      size, then a zero length packet must be sent to terminate
    //      the host requests.
    if (gpos < glen) {
        uint16_t len = glen - gpos;
        if (len > 64)
            len = 64;

        if (usbd_ep_write_packet(usbd_dev, 0x82, gbuf + gpos, len) != 0) {
            uart_putchar('+');
            uart_putchar("0123456789abcdef"[gbuf[gpos] >> 4]);
            uart_putchar("0123456789abcdef"[gbuf[gpos] & 0xf]);
            gpos += len;
        }
#if 0
uart_putchar("0123456789abcdef"[(gpos >> 8) & 0xf]);
uart_putchar("0123456789abcdef"[(gpos >> 4) & 0xf]);
uart_putchar("0123456789abcdef"[gpos & 0xf]);
#endif
    }
#if 0
    /* Not currently used because we keep all transfers under 64 bytes */
    char buf[64] __attribute__ ((aligned(4)));
    usbd_ep_write_packet(usbd_dev, 0x82, buf, 64);

    uart_putchar('+');
#endif
}

static void cdcacm_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
    usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_rx_cb);
    usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_tx_cb);
    usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);

    usbd_register_control_callback(usbd_dev,
                                   USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                                   USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
                                   cdcacm_control_request);
}

#ifdef USING_USB_INTERRUPT
#ifdef STM32F103xE
void usb_lp_can_rx0_isr(void)
{
    static uint16_t preg1 = 0;
    static uint16_t preg2 = 0;
    uint16_t        reg;

    usbd_poll(usbd_gdev);

    /* Detect and clear unhandled interrupt */
    reg = *USB_ISTR_REG;
    if (reg & preg1 & preg2) {
        *USB_ISTR_REG = ~(reg & preg1 & preg2);
#ifdef DEBUG_IRQS
        static bool complained = false;
        if (complained == false) {
            complained = true;
            printf("USB Unhandled IRQ %04x\n", reg & preg1 & preg2);
        }
#endif
    }

    preg2 = preg1;
    preg1 = reg;
}
#else /* STM32F4 / STM32F107 */
void otg_fs_isr(void)
{
    usbd_poll(usbd_gdev);
}
#endif

static void
usb_enable_interrupts(void)
{
#if defined(STM32F103xE)
    *USB_CNTR_REG |= USB_CNTR_SOFM | USB_CNTR_SUSPM | USB_CNTR_RESETM |
                     USB_CNTR_ERRM | USB_CNTR_WKUPM | USB_CNTR_CTRM;
#elif defined (STM32F107xC)
#if 1
    /* Is this correct? */
    OTG_FS_GOTGINT |= OTG_GOTGINT_DBCDNE | OTG_GOTGINT_ADTOCHG |
                      OTG_GOTGINT_HNGDET | OTG_GOTGINT_HNSSCHG |
                      OTG_GOTGINT_SRSSCHG | OTG_GOTGINT_SEDET;
#else
    /* Maybe it should be this */
    OTG_FS_GINTMSK |= OTG_GINTMSK_USBSUSPM | OTG_GINTMSK_USBRST |
                      OTG_GINTMSK_ENUMDNEM | OTG_GINTMSK_IEPINT |
                      OTG_GINTMSK_OEPINT   | OTG_GINTMSK_IISOIXFRM |
                      OTG_GINTMSK_IISOOXFRM | OTG_GINTMSK_WUIM;
#endif
#endif

    nvic_set_priority(USB_INTERRUPT, 0x40);
    nvic_enable_irq(USB_INTERRUPT);

    using_usb_interrupt = false;
    usb_poll();
    using_usb_interrupt = true;
}
#endif /* USING_USB_INTERRUPT */

__attribute__((aligned(4)))
static uint8_t usb_serial_str[32];
usbd_device   *usbd_gdev = NULL;

#endif /* !USE_HAL_DRIVER */


void usb_startup(void)
{
#ifdef USE_HAL_DRIVER
    MX_USB_DEVICE_Init();
    using_usb_interrupt = true;  // STM32 HAL driver always uses interrupts

#else /* !USE_HAL_DRIVER */
#ifdef DEBUG_NO_USB
    return;
#endif
    uint16_t len = sizeof (usb_serial_str);
    usbd_usr_serial(usb_serial_str, &len);
    usb_strings[2] = (char *)usb_serial_str;

#ifdef STM32F4
    /* GPIO9 should left as an INPUT */
    gpio_mode_setup(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO11 | GPIO12);
    gpio_set_af(GPIOA, GPIO_AF10, GPIO11 | GPIO12);

#define USB_DRIVER otgfs_usb_driver

#else
#ifdef STM32F103xE
#define USB_DRIVER st_usbfs_v1_usb_driver
    usb_signal_reset_to_host(1);
#else
#define USB_DRIVER stm32f107_usb_driver
#endif
#endif
    usbd_gdev = usbd_init(&USB_DRIVER,
                          (const struct usb_device_descriptor *)
                             &USBD_FS_DeviceDesc[0], &config,
                          usb_strings, ARRAY_SIZE(usb_strings),
                          usbd_control_buffer, sizeof (usbd_control_buffer));

    usbd_register_set_config_callback(usbd_gdev, cdcacm_set_config);

#ifdef USING_USB_INTERRUPT
    usb_enable_interrupts();
#else
    using_usb_interrupt = false;
#endif

#endif /* !USE_HAL_DRIVER */
}
