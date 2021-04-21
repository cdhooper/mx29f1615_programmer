/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * MX29F1615-specific code (read, write, erase, status, etc).
 */

#include "main.h"
#include "printf.h"
#include "uart.h"
#include <stdbool.h>
#include <string.h>
#include "mx29f1615.h"
#include "adc.h"
#include "button.h"
#include "utils.h"
#include "timer.h"
#include "gpio.h"
#include "usb.h"

#undef DEBUG_SIGNALS

#define MX_DEVICE_SIZE          (1 << 20)   // 1M words (16-bit words)
#define MX_ERASE_SECTOR_SIZE    (64 << 10)  // 64K-word sectors
#define MX_PAGE_SIZE            64          // Page program words

#define MX_STATUS_FAIL_PROGRAM  0x10  // Status code - failed to program
#define MX_STATUS_FAIL_ERASE    0x20  // Status code - failed to erase
#define MX_STATUS_COMPLETE      0x80  // Status code - operation complete

#define MX_MODE_ERASE           0     // Waiting for erase to complete
#define MX_MODE_PROGRAM         1     // Waiting for program to complete

#ifdef USE_HAL_DRIVER
#define GPIO_PUPD_PULLUP 0x1
#else
/* libopencm3 */
#ifdef STM32F4
#define GPIO_MODE_OUTPUT_PP GPIO_MODE_OUTPUT
#else
#define GPIO_MODE_OUTPUT_PP GPIO_MODE_OUTPUT_10_MHZ
#endif
#endif

static uint32_t ticks_per_35_nsec;
static uint32_t ticks_per_60_nsec;
static uint32_t ticks_per_120_nsec;
static uint64_t mx_last_access = 0;
static bool     mx_enabled = false;


static void
address_output(uint32_t addr)
{
#ifdef STM32F4
    /*
     * The address bits are split across two port registers
     *    GPIOB PB0..PB15 A0..A15
     *    GPIOD PD0..PD4  A16..A19
     */
    GPIO_ODR(A0_GPIO_Port)   = addr & 0xffff;           // Set A15..A0
    GPIO_BSRR(A16_GPIO_Port) = 0x000f0000 |             // Clear A19..A16
                               ((addr >> 16) & 0x000f); // Set A19..A16
#else
    /*
     * The address bits are split across two port registers
     *    GPIOE PE0..PE15 A0..A15
     *    GPIOD PC6..PD9  A16..A19
     */
    GPIO_ODR(A0_GPIO_Port)   = addr & 0xffff;           // Set A15..A0
    GPIO_BSRR(A16_GPIO_Port) = 0x03c00000 |             // Clear A19..A16
                               ((addr >> 10) & 0x03c0); // Set A19..A16
#endif
}

static uint32_t
address_input(void)
{
    uint32_t addr = GPIO_IDR(A0_GPIO_Port);
#ifdef STM32F4
    addr |= ((GPIO_IDR(A16_GPIO_Port) & 0x000f) << 16);
#else
    addr |= ((GPIO_IDR(A16_GPIO_Port) & 0x03c0) << (16 - 6));
#endif
    return (addr);
}


static void
address_output_enable(void)
{
#ifdef STM32F4
    /*
     * Each port register manages up to 16 GPIOs.
     *  A0...A15 are in the first port  (GPIOB)
     * A16...A19 are in the second port (GPIOD)
     */
    GPIO_MODER(A0_GPIO_Port)  = 0x55555555;  // GPIO_MODE_OUTPUT_PP
    GPIO_MODER(A16_GPIO_Port) = (GPIO_MODER(A16_GPIO_Port) & 0xffffff00) | 0x00000055;
#else
    /*
     * Each register manages up to 8 GPIOs
     *  A0...A7  are in the first port CRL register
     *  A8...A15 are in the first port CRH register
     * A16...A19 are in the second port CRL register bottom half / middle
     */
    GPIO_CRL(A0_GPIO_Port)  = 0x11111111;  // PE0..PE15 Output Push-Pull
    GPIO_CRH(A0_GPIO_Port)  = 0x11111111;

    /* PC6...PC9 */
    GPIO_CRL(A16_GPIO_Port) = (GPIO_CRL(A16_GPIO_Port) & 0x00ffffff) |
                              0x11000000;
    GPIO_CRH(A16_GPIO_Port) = (GPIO_CRH(A16_GPIO_Port) & 0xffffff00) |
                              0x00000011;
#endif
}

static void
address_output_disable(void)
{
#ifdef STM32F4
    /*
     * Each port register manages up to 16 GPIOs.
     *  A0...A15 are in the first port  (GPIOB PB0..PB15)
     * A16...A19 are in the second port (GPIOD PD0..PD3)
     */
    GPIO_MODER(A0_GPIO_Port)  = 0x00000000;  // GPIO_MODE_INPUT
    GPIO_MODER(A16_GPIO_Port) = (GPIO_MODER(A16_GPIO_Port) & 0xffffff00) |
                                0x00000000;
#else
    /*
     * Each register manages up to 8 GPIOs
     *  A0...A7  are in the first port CRL register (PE0..PE7)
     *  A8...A15 are in the first port CRH register (PE8..PE15)
     * A16...A19 straddle the second port CRL and CRH (PC6..PC9)
     */
    GPIO_CRL(A0_GPIO_Port)  = 0x88888888; // PE0..PE15 Input Pull-Up / Pull-Down
    GPIO_CRH(A0_GPIO_Port)  = 0x88888888;
    /* PC6...PC9 */
    GPIO_CRL(A16_GPIO_Port) = (GPIO_CRL(A16_GPIO_Port) & 0x00ffffff) |
                              0x88000000;
    GPIO_CRH(A16_GPIO_Port) = (GPIO_CRH(A16_GPIO_Port) & 0xffffff00) |
                              0x00000088;
    GPIO_ODR(A0_GPIO_Port)   = 0x00000000;  // Pull down A0-A15
    GPIO_ODR(A16_GPIO_Port) &= 0xfffffc3f;  // Pull down A16-A19
#endif
}

static void
data_output(uint16_t data)
{
    GPIO_ODR(D0_GPIO_Port) = data;
}

static uint16_t
data_input(void)
{
    return (GPIO_IDR(D0_GPIO_Port));
}

static void
data_output_enable(void)
{
#ifdef STM32F4
    GPIO_MODER(D0_GPIO_Port) = 0x55555555; // GPIO_MODE_OUTPUT_PP
#else
    GPIO_CRL(D0_GPIO_Port) = 0x11111111;   // Output Push-Pull
    GPIO_CRH(D0_GPIO_Port) = 0x11111111;
#endif
}

static void
data_output_disable(void)
{
#ifdef STM32F4
    GPIO_MODER(D0_GPIO_Port) = 0x00000000; // GPIO_MODE_INPUT
#else
    GPIO_CRL(D0_GPIO_Port) = 0x88888888;   // Input Pull-Up / Pull-Down
    GPIO_CRH(D0_GPIO_Port) = 0x88888888;
    GPIO_ODR(D0_GPIO_Port) = 0x00000000;   // Pull down D0-D15
#endif
}

static void
ce_output(uint value)
{
#ifdef DEBUG_SIGNALS
    printf(" CE=%d", value);
#endif
    gpio_setv(CE_GPIO_Port, CE_Pin, value);
}

static void
oe_output(uint value)
{
#ifdef DEBUG_SIGNALS
    printf(" OE=%d", value);
#endif
    gpio_setv(OE_GPIO_Port, OE_Pin, value);
}

static void
ce_output_enable(void)
{
    gpio_mode_set(CE_GPIO_Port, CE_Pin, GPIO_MODE_OUTPUT_PP);
}

static void
ce_output_disable(void)
{
    gpio_mode_set(CE_GPIO_Port, CE_Pin, GPIO_MODE_INPUT);
    ce_output(0);
}

static void
oe_output_enable(void)
{
    gpio_mode_set(OE_GPIO_Port, OE_Pin, GPIO_MODE_OUTPUT_PP);
}

static void
oe_output_disable(void)
{
    gpio_mode_set(OE_GPIO_Port, OE_Pin, GPIO_MODE_INPUT);
    oe_output(0);
}

static void
vcc_enable(void)
{
#ifdef DEBUG_SIGNALS
    printf(" VCC=On");
#endif
    /* Drive EN_VCC low to turn on VCC */
    gpio_setv(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, 0);
    gpio_mode_set(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, GPIO_MODE_OUTPUT_PP);
}

static void
vcc_disable(void)
{
#ifdef DEBUG_SIGNALS
    printf(" VCC=Off");
#endif
    /*
     * Release EN_VCC (external pull-up) to turn off VCC. Note that it must
     * be released so the external pull-up can bring the MOSFET gate up to
     * 5V.
     */
    gpio_setv(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, 1);
    gpio_mode_set(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin, GPIO_MODE_INPUT);
}

/*
 * vpp_enable() applies 10V to the VPP input of the EEPROM device.
 *
 * It is the responsibility of the caller to meet the EEPROM device's
 * timing requirements.
 */
static void
vpp_enable(void)
{
#ifdef DEBUG_SIGNALS
    printf(" VPP=On");
#endif
    /* Drive EN_VPP high to turn on VPP */
    gpio_setv(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, 1);
}

static void
vpp_disable(void)
{
#ifdef DEBUG_SIGNALS
    printf(" VPP=Off");
#endif
    /* Drive EE_EN_VPP low to turn off VPP */
    gpio_setv(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin, 0);
}

/*
 * mx_enable() applies power to the EEPROM device and sets default state for
 *             the CE#, OE#, and address lines. Data lines are left floating.
 *
 * VPP is raised to VCC by an external pull-up resistor. It is not raised
 * to 10V by this routine.
 */
void
mx_enable(void)
{
    if (mx_enabled)
        return;
#ifdef DEBUG_SIGNALS
    printf("mx_enable\n");
#endif
    ticks_per_35_nsec  = timer_nsec_to_tick(35);
    ticks_per_60_nsec  = timer_nsec_to_tick(60);
    ticks_per_120_nsec = timer_nsec_to_tick(120);
    address_output(0);
    address_output_enable();
    vcc_enable();
    vpp_disable();
    ce_output(1);
    oe_output(1);
    ce_output_enable();
    oe_output_enable();
    data_output_disable();
    timer_delay_usec(52);  // tVCS=50us tVPS=2us
    mx_enabled = true;
    mx_read_mode();
#ifdef DEBUG_SIGNALS
    printf("GPIOA=%p GPIOB=%p GPIOC=%p GPIOD=%p GPIOE=%p\n",
           GPIOA, GPIOB, GPIOC, GPIOD, GPIOE);
#endif
}

/*
 * mx_disable() cuts power to the EEPROM device and tri-states all address
 *              and data lines to the device.
 */
void
mx_disable(void)
{
    ce_output_disable();
    oe_output_disable();
    address_output_disable();
    data_output_disable();
    vpp_disable();
    timer_delay_usec(50);
    vcc_disable();
    mx_enabled = false;
}

/*
 * mx_read_word() performs a single address read, with appropriate timing.
 *
 * MX29F1615 read timing waveform
 *
 *   Address  ####<------Address Stable----->###########
 *            ________                        __________
 *   CE#              \______________________/
 *            ______________                  __________
 *   OE#                    \________________/
 *
 *   VPP      ___________________________________________
 *
 *            High-Z                               High-Z
 *   DATA-OUT ~~~~~~~~~~~~~~~~~~~<-Data Out Valid->~~~~~~
 *
 * MX29F1615 timing notes
 *   tACC - Address stable to Data Out Valid    (max 120ns)
 *   tCE  - CE low to Data Out Valid            (max 120ns)
 *   tOE  - OE low to Data Out Valid            (max 60ns)
 *   tOH  - OE high to Data Out no longer valid (min 0ns)
 *   tDF  - OE high to Data OUT High-Z          (max 35ns)
 */
static void
mx_read_word(uint32_t addr, uint16_t *data)
{
    address_output(addr);
    ce_output(0);
    oe_output(0);
    timer_delay_ticks(ticks_per_120_nsec);  // Wait for tACC / tCE / tOE
    *data = data_input();
    ce_output(1);
    oe_output(1);
    timer_delay_ticks(ticks_per_35_nsec);   // Wait for tDF

#if 0
    /* If it's an EPROM, it may need more delay than MX29F1615... */
    timer_delay_usec(1);
#endif
}

/*
 * mx_read() will read the specified number of words from the EEPROM device.
 */
int
mx_read(uint32_t addr, uint16_t *data, uint count)
{
    if ((addr + count) > MX_DEVICE_SIZE)
        return (1);

    usb_mask_interrupts();
    while (count-- > 0)
        mx_read_word(addr++, data++);
    usb_unmask_interrupts();

    return (0);
}

/*
 * mx_write_word() performs a single address write, with appropriate timing.
 *
 * MX29F1615 command write timing waveform
 *
 *   Address  ####<------Address Stable----->##########
 *            ________                        _________
 *   CE#              \______________________/
 *            _________________________________________
 *   OE#             __________________________
 *            ______/                          \_______
 *   VPP      _________________________________________
 *
 *            High-Z                             High-Z
 *   DATA     ~~~~~~~~~~~~~<----Data In Valid--->~~~~~~
 *
 * Address is latched on the falling edge of CE.
 * Data is latched on the rising edge of CE
 *
 * MX29F1615 timing notes
 *   tVCS - VCC Setup Time                      (min 50us)
 *   tVPS - VPP Setup Time                      (min 2us)
 *   tOES - Output Enable Setup Time            (min 0ns)
 *   tAS  - Address Setup Time                  (min 0us)
 *   tAH  - Address Hold Time                   (min 60us)
 *   tDS  - Data Setup Time                     (min 60ns)
 *   tDH  - Data Hold Time                      (min 0ns)
 *   tVPH - VPP Hold Time                       (min 2us)
 */
static void
mx_write_word(uint32_t addr, uint16_t data)
{
    address_output(addr);
    oe_output(1);
    ce_output(0);          // tOES=0ns, tAS=0ns

    data_output(data);
    data_output_enable();

    timer_delay_ticks(ticks_per_60_nsec);  // tAH=60ns tDS=60ns
    ce_output(1);
    data_output_disable(); // tDH=0ns
}

/*
 * mx_cmd() sends a VPP-protected command to the EEPROM device. It will
 *          automatically raise before the command and lower VPP after
 *          the command.
 */
void
mx_cmd(uint32_t addr, uint16_t cmd, int vpp_delay)
{
    vpp_enable();
    timer_delay_usec(2);  // Wait 2us after enabling VPP=VHH (10V)
    usb_mask_interrupts();
    mx_last_access = timer_tick_get();

    mx_write_word(0x05555, 0x00aa);
    mx_write_word(0x02aaa, 0x0055);
    mx_write_word(addr, cmd);

    timer_delay_usec(2);   // Wait 2us before disabling VPP=VHH (10V)
    vpp_disable();
    usb_unmask_interrupts();
    timer_delay_usec(2);   // Wait for command to complete

    if (vpp_delay)
        timer_delay_usec(100); // Wait for command to complete
}

/*
 * mx_status_clear() resets any error status on the MX29F1615 part.
 */
void
mx_status_clear(void)
{
    mx_cmd(0x05555, 0x0050, 0);
    mx_read_mode();
}

/*
 * mx_wait_for_done_status() will poll the EEPROM part waiting for a done
 *                           status to be reported. It will detect and report
 *                           errors including erase and programming failures,
 *                           and command timeouts.
 */
static int
mx_wait_for_done_status(uint32_t timeout_usec, int verbose, int mode)
{
    int      rc = 0;
    uint     report_time = 0;
    uint64_t start;
    uint64_t now;
    uint16_t status = 0;
    uint64_t usecs = 0;

    start = timer_tick_get();
    while (usecs < timeout_usec) {
        now = timer_tick_get();
        usecs = timer_tick_to_usec(now - start);
        mx_read_word(0x00000, &status);
        if (status & 0xff03) {
            printf("\nInvalid status word %04x\n", status);
            rc = 3;  // Bad status (erase or program probably rejected)
            return (rc);
        }

        if (is_abort_button_pressed()) {
            printf("Aborted\n");
            return (3);
        }
        if (verbose) {
            if (report_time < usecs / 1000000) {
                report_time = usecs / 1000000;
                printf("\r%02x %u", status, report_time);
            }
        }
        if (status & 0x80) {
            if (verbose) {
                report_time = usecs / 1000000;
                printf("\r%02x %u.%03u sec", status, report_time,
                       (uint) ((usecs - report_time * 1000000) / 1000));
            }
            break;  // done
        }
        timer_delay_msec(1);
    }
    if (status & (MX_STATUS_FAIL_PROGRAM | MX_STATUS_FAIL_ERASE)) {
        printf("    %s failed %02x\n",
               (mode == MX_MODE_ERASE) ? "Erase" : "Program", status);
        if ((status & MX_STATUS_COMPLETE) == 0)
            printf("    Busy status\n");
        if (status & MX_STATUS_FAIL_PROGRAM)
            printf("    Program fail\n");
        if (status & MX_STATUS_FAIL_ERASE)
            printf("    Erase fail\n");
        rc = 2;  // Erase or program failed
        mx_status_clear();
    } else if ((status & MX_STATUS_COMPLETE) == 0) {
        printf("    Timeout\n");
        rc = 1;  // Timeout
    } else if (verbose) {
        printf("    Done\n");
    }
    return (rc);
}

/*
 * mx_program_page() writes at most a single page of words to EEPROM.
 *                   For the MX29F1615, this is up to 64 words. The
 *                   count of words written is returned in words.
 *
 * The time between successive word loads must be less than 30us,
 * otherwise the load period will be terminated by the device (and
 * program period started). The datasheet recommends waiting 100us
 * for the programming period to complete.
 *
 * A19 to A6 specify the page address (64 words boundary).
 * A5 to A0 specify the byte address within the page.
 * Words may be loaded in any order, but this code always
 * loads them sequentially. Words not loaded will not be
 * written to EEPROM (will remain with 0xffff value).
 */
static int
mx_program_page(uint32_t addr, uint16_t *data, uint count, uint *words)
{
    *words = 0;

    vpp_enable();
    timer_delay_usec(2);  // Wait 2us after enabling VPP=VHH (10V)
    usb_mask_interrupts();

    mx_write_word(0x05555, 0x00aa);
    mx_write_word(0x02aaa, 0x0055);
    mx_write_word(0x05555, 0x00a0);

    while (count > 0) {
        mx_write_word(addr, *data);

        data++;
        addr++;
        count--;
        (*words)++;
        if ((addr & (MX_PAGE_SIZE - 1)) == 0)
            break; // End of page
    }
    timer_delay_usec(2);    // tVPH - Hold Time before disabling VPP=VHH (10V)
    vpp_disable();
    usb_unmask_interrupts();
    timer_delay_usec(100);  // tBAL - Word Access Load Time

    return (mx_wait_for_done_status(2000000, 0, MX_MODE_PROGRAM));  // 2 sec
}

/*
 * mx_write() will program <count> words to EEPROM, starting at the
 *            specified address. It automatically uses page program
 *            to speed up programming. After each page is written,
 *            it is read back to verify that programming was successful.
 */
int
mx_write(uint32_t addr, uint16_t *data, uint count)
{
    int rc;
    uint words;
    uint16_t wordbuf[MX_PAGE_SIZE];

    if ((addr + count) > MX_DEVICE_SIZE)
        return (1);

    while (count > 0) {
        int try_count = 0;
        if (is_abort_button_pressed()) {
            printf("Aborted\n");
            return (3);
        }
try_again:
        rc = mx_program_page(addr, data, count, &words);
        if (rc != 0) {
            printf("  Program failed at %lx\n", addr << 1);
            return (rc);  // Page program failed
        }
        if (words == 0) {
            printf("No words programmed at %lx\n", addr << 1);
            return (1);
        }
        mx_read_mode();
        if (mx_read(addr, wordbuf, words) != 0) {
            printf("  Read failed at %lx\n", addr << 1);
            return (2);
        }
        if (memcmp(data, wordbuf, words * 2) != 0) {
            if (try_count++ < 2) {
//              printf("Read verify failed -- trying again at %lx\n", addr);
                goto try_again;
            }
            printf("  Read verify failed at %lx\n", addr << 1);
            return (3);
        }
        count -= words;
        addr  += words;
        data  += words;
    }

    mx_read_mode();
    return (0);
}

/*
 * mx_read_mode() sends a command to put the EEPROM chip back in the startup
 *                read mode.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
void
mx_read_mode(void)
{
    mx_cmd(0x05555, 0x00f0, 0);
}

/*
 * mx_status_read() acquires and converts to readable text the current value
 *                  of the status register from the EEPROM.
 *
 * @param [out] status     - A buffer where to store the status string.
 * @param [in]  status_len - The buffer length
 *
 * @return      status value read from the EEPROM.
 */
uint16_t
mx_status_read(char *status, uint status_len)
{
    uint16_t data;

    mx_cmd(0x05555, 0x0070, 0);
    mx_read_word(0x00000, &data);
    mx_read_mode();

    if (data == 0x0080)
        snprintf(status, status_len, "Normal");
    else if (data & 0xff03)
        snprintf(status, status_len, "Invalid status");
    else if (data & MX_STATUS_FAIL_ERASE)
        snprintf(status, status_len, "Erase Failure");
    else if (data & MX_STATUS_FAIL_PROGRAM)
        snprintf(status, status_len, "Program Failure");
    else
        snprintf(status, status_len, "Unknown");

    return (data);
}

/*
 * mx_erase() will erase the entire chip, individual sectors, or sequential
 *            groups of sectors.
 *
 * Chip erase time:       32-256 sec
 * Word Program Time:     14-420 us
 * Page programming time: 0.9-27 ms
 * Chip programming time: 14-42 sec
 * Erase/Program Cycles:  100
 *
 * Measured sector erase is about 2.5 seconds. I don't know the upper limit.
 *
 * A non-zero length erases all sectors making up the address range
 * of addr to addr + length - 1. This means that it's possible that
 * more than the specified length will be erased, but that never too
 * few sectors will be erased. A minimum of one sector will always
 * be erased.
 *
 * EEPROM erase MX_ERASE_MODE_CHIP erases the entire device.
 * EEPROM erase MX_ERASE_MODE_SECTOR erases a 64K-word (128KB) sector.
 * Unique sector addresses:
 *     0x010000  0x040000  0x080000  0x0c0000
 *     0x020000  0x050000  0x090000  0x0d0000
 *     0x030000  0x060000  0x0a0000  0x0e0000
 *     0x040000  0x070000  0x0b0000  0x0f0000
 *
 * Return values
 *  0 = Success
 *  1 = Erase Timeout
 *  2 = Erase failure
 *  3 = Erase rejected by device (low VPP?)
 *
 * MX29F1615 timing notes
 *   tDS  - Data Setup Time                     (min 60ns)
 *   tSRA - Status Register Access Time         (min 70ns)
 *
 * @param [in]  mode=MX_ERASE_MODE_CHIP   - The entire chip is to be erased.
 * @param [in]  mode=MX_ERASE_MODE_SECTOR - One or multiple sectors are to be
 *                                          erased.
 * @param [in]  addr    - The address to erased (if MX_ERASE_MODE_SECTOR).
 * @param [in]  len     - The length to erased (if MX_ERASE_MODE_SECTOR). Note
 *                        that the erase area is always rounded up to the next
 *                        full sector size. A length of 0 will still erase a
 *                        single sector.
 * @param [in]  verbose - Report the accumulated time as the erase progresses.
 */
int
mx_erase(uint mode, uint32_t addr, uint32_t len, int verbose)
{
    int rc = 0;
    int timeout;

    if (mode > MX_ERASE_MODE_SECTOR) {
        printf("BUG: Invalid erase mode %d\n", mode);
        return (1);
    }
    if ((len == 0) || (mode == MX_ERASE_MODE_CHIP))
        len = 1;

    mx_status_clear();
    while (len > 0) {
        if (addr >= MX_DEVICE_SIZE) {
            /* Exceeded the address range of the EEPROM */
            rc = 1;
            break;
        }

        vpp_enable();
        timer_delay_usec(2);  // Wait 2us after enabling VPP=VHH (10V)
        usb_mask_interrupts();

        mx_write_word(0x05555, 0x00aa);
        mx_write_word(0x02aaa, 0x0055);
        mx_write_word(0x05555, 0x0080);
        mx_write_word(0x05555, 0x00aa);
        mx_write_word(0x02aaa, 0x0055);

        if (mode == MX_ERASE_MODE_CHIP) {
            mx_write_word(0x05555, 0x0010);
            timeout = 200000000;  // 200 seconds
        } else {
            addr &= 0xff0000;
            mx_write_word(addr, 0x0030);
            timeout = 10000000;  // 10 seconds
        }

        timer_delay_usec(2);  // tVPH
        vpp_disable();
        usb_unmask_interrupts();
        timer_delay_usec(100);  // tBAL (Word Access Load Time)

        rc = mx_wait_for_done_status(timeout, verbose, MX_MODE_ERASE);
        if (len <= MX_ERASE_SECTOR_SIZE)
            break;
        len -= MX_ERASE_SECTOR_SIZE;
        addr += 0x10000;  // Advance to the next sector
    }

    mx_read_mode();
    return (rc);
}

/*
 * mx_id() queries and reports the current chip ID values.
 *         For the MX29F1615, the chip id should be 0x006B00C2
 *         (0xC2=MXID and 0x6B=MX29F1615)
 *
 * This function requires no arguments.
 *
 * @return      The manufacturer (high 16 bite) and device code (low 16 bits).
 */
uint32_t
mx_id(void)
{
    uint16_t low;
    uint16_t high;

    mx_cmd(0x05555, 0x0090, 0);
    mx_read_word(0x00000, &low);
    mx_read_word(0x00001, &high);

    mx_read_mode();
    return (low | (high << 16));
}

/*
 * mx_vcc_is_on() reports whether VCC (runtime voltage) is enabled to the
 *                EEPROM.
 *
 * This function requires no arguments.
 *
 * @return      1 - VCC is on.
 * @return      0 - VCC is off.
 */
int
mx_vcc_is_on(void)
{
    return (gpio_get(EE_EN_VCC_GPIO_Port, EE_EN_VCC_Pin) ? 0 : 1);
}

/*
 * mx_vpp_is_on() reports whether VPP (programming voltage) is enabled
 *                to the EEPROM.
 *
 * This function requires no arguments.
 *
 * @return      1 - VPP is on.
 * @return      0 - VPP is off.
 */
int
mx_vpp_is_on(void)
{
    return (gpio_get(EE_EN_VPP_GPIO_Port, EE_EN_VPP_Pin) ? 1 : 0);
}

/*
 * mx_poll() monitors the EEPROM for last access and automatically cuts
 *           power to it after being idle for more than 1 second.
 *
 * This function requires no arguments.
 *
 * @return      None.
 */
void
mx_poll(void)
{
    if (mx_last_access != 0) {
        uint64_t usec = timer_tick_to_usec(timer_tick_get() - mx_last_access);
        if (usec > 1000000) {
            mx_disable();
            mx_last_access = 0;
        }
    }
}

static void
mx_print_bits(uint32_t value, int high_bit, char *prefix)
{
    int bit;
    for (bit = high_bit; bit >= 0; bit--) {
        if (value & (1 << bit))
            printf("%s%d ", prefix, bit);
    }
}

/*
 * mx_verify() verifies pin connectivity to an installed EEPROM. This is done
 *             by a sequence of distinct tests.
 *
 * These are:
 *   1) Pull-down test (all address and data lines are weakly pulled
 *      down to verify no exernal power is present).
 *      including using STM32 internal
 *   2) VCC, CE, OE, and VPP are then applied in sequence to verify
 *      no address or data lines are pulled up.
 *   3) Pull-up test (all address and data lines are weakly pulled up, one
 *      at a time) to verify:
 *      A) Each line is pulled up in less than 1 ms
 *      B) No other line is affected by that pull-up
 */
int
mx_verify(int verbose)
{
    int         rc = 0;
    int         pass;
    uint32_t    value;
    uint32_t    expected;
    const char *when = "";

    if (verbose)
        printf("Test address and data pull-down: ");
    for (pass = 0; pass <= 4; pass++) {
        /* Set up next pass */
        switch (pass) {
            case 0:
                /* Start in an unpowered state, all I/Os input, pulldown */
                mx_disable();
                break;
            case 1:
                oe_output_enable();
                oe_output(1);
                when = " when OE high";
                break;
            case 2:
                vcc_enable();
                when = " when VCC enabled";
                break;
            case 3:
                ce_output_enable();
                ce_output(1);
                when = " when CE high";
                break;
            case 4:
                vpp_enable();
                when = " when VPP enabled";
                break;
        }
        timer_delay_usec(100);  // Pull-downs should quickly drop voltage

        value = address_input();
        if (value != 0) {
            mx_print_bits(value, 19, "A");
            printf("stuck high: 0x%05lx%s\n", value, when);
            rc = 1;
            goto fail;
        }

        value = data_input();
        if (value != 0) {
            mx_print_bits(value, 15, "D");
            printf("stuck high: 0x%04lx%s\n", value, when);
            rc = 1;
            goto fail;
        }

        adc_poll(false, true);
        if (v5_overcurrent == true) {
            printf("V5 overcurrent%s\n", when);
            rc = 1;
            goto fail;
        }
        if (v5_stable == false) {
            printf("V5 is not stable%s\n", when);
            rc = 1;
            goto fail;
        }
        if (v10_stable == false) {
            printf("V10 is not stable%s\n", when);
            rc = 1;
            goto fail;
        }
    }

    vpp_disable();
    if (verbose) {
        printf("pass\n");
        printf("Test address pull-up: ");
    }

    /* Pull up and verify address lines, one at a time */
    for (pass = 0; pass <= 19; pass++) {
#ifdef STM32F4
        if (pass < 16) {
            gpio_mode_setup(A0_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                            1 << pass);            // PA0..PA15
        } else {
            gpio_mode_setup(A0_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                            1 << (pass - 16));     // PD0..PD3
        }
#else
        /* STM32F1 pullup/pulldown is controlled by output data register */
        address_output((1 << (pass + 1)) - 1);
#endif
        uint64_t timeout = timer_tick_plus_msec(1);
        uint64_t start = timer_tick_get();
        uint64_t seen = 0;

        while (timer_tick_has_elapsed(timeout) == false) {
            value = data_input();
            if (value != 0) {
                mx_print_bits(value, 16, "D");
                printf("found high with A%d pull-up: %04lx\n", pass, value);
                rc = 1;
                break;
            }
            value = address_input();
            if (value & (1 << pass)) {
                if (seen == 0)
                    seen = timer_tick_get();
                expected = (1U << (pass + 1)) - 1;
                if (value != expected) {
                    printf("A%d pull-up caused incorrect ", pass);
                    mx_print_bits(value ^ expected, 19, "A");
                    printf("value: 0x%05lx\n", value);
                    rc = 1;
                    break;
                }
            }
        }
        if (seen == 0) {
            printf("A%d stuck low: 0x%05lx\n", pass, value);
            rc = 1;
        } else if (verbose > 1) {
            printf(" A%d: %lld usec\n",
                   pass, timer_tick_to_usec(seen - start));
        }
    }
    if (rc != 0)
        goto fail;

    if (verbose) {
        printf("pass\n");
        printf("Test data pull-up: ");
    }

    /* Pull up and verify data lines, one at a time */
    for (pass = 0; pass <= 15; pass++) {
#ifdef STM32F4
        gpio_mode_setup(D0_GPIO_Port, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                        1 << pass);             // PE0..PE15
#else
        /* STM32F1 pullup/pulldown is controlled by output data register */
        data_output((1 << (pass + 1)) - 1);
#endif
        uint64_t timeout = timer_tick_plus_msec(1);
        uint64_t start = timer_tick_get();
        uint64_t seen = 0;

        while (timer_tick_has_elapsed(timeout) == false) {
            value = address_input();
            if (value != 0xfffff) {
                mx_print_bits(value ^ 0xffff, 19, "A");
                printf("found low with D%d pull-up: %05lx\n", pass, value);
                rc = 1;
                break;
            }
            value = data_input();
            if (value & (1 << pass)) {
                if (seen == 0)
                    seen = timer_tick_get();
                expected = (1U << (pass + 1)) - 1;
                if (value != expected) {
                    printf("D%d pull-up caused incorrect ", pass);
                    mx_print_bits(value ^ expected, 16, "D");
                    printf("value: 0x%04lx\n", value);
                    rc = 1;
                    break;
                }
            }
        }
        if (seen == 0) {
            printf("D%d stuck low: 0x%04lx\n", pass, value);
            rc = 1;
        } else if (verbose > 1) {
            printf(" D%d: %lld usec\n",
                   pass, timer_tick_to_usec(seen - start));
        }
    }
    if (rc != 0)
        goto fail;

    if (verbose) {
        printf("pass\n");
    }

fail:
    mx_disable();
    return (rc);
}
