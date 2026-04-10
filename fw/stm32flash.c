/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2024.
 *
 * ---------------------------------------------------------------------
 *
 * Erase and write STM32 internal flash memory
 */

#include "printf.h"
#include "board.h"
#include "main.h"
#include "stm32flash.h"
#include "cmdline.h"
#include <string.h>
#include "utils.h"
#include <stdbool.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/stm32/common/flash_common_idcache.h>

#ifdef STM32F1
#define FL_PAGE_SIZE 2048

#define flash_lock()         FLASH_CR |= FLASH_CR_LOCK;
#define flash_unlock()       FLASH_KEYR = FLASH_KEYR_KEY1; \
                             FLASH_KEYR = FLASH_KEYR_KEY2;

// flash_get_status_flags()
#define flash_status_flags() (FLASH_SR & (FLASH_SR_PGERR | FLASH_SR_EOP | \
                                          FLASH_SR_WRPRTERR | FLASH_SR_BSY))

static void
flash_wait_for_done(void)
{
    while ((FLASH_SR & FLASH_SR_BSY) == FLASH_SR_BSY)
        ;
}

static void
flash_page_erase(uint32_t addr)
{
    flash_wait_for_done();

    FLASH_CR |= FLASH_CR_PER;
    FLASH_AR = addr;
    FLASH_CR |= FLASH_CR_STRT;

    flash_wait_for_done();

    FLASH_CR &= ~FLASH_CR_PER;
}

static int
flash_write16(uint32_t addr, uint16_t data)
{
    if (*(uint16_t *) addr == data)
        return (0);  // Data already matches

    flash_wait_for_done();

    FLASH_CR |= FLASH_CR_PG;
    MMIO16(addr) = data;

    flash_wait_for_done();

    FLASH_CR &= ~FLASH_CR_PG;
    return (MMIO16(addr) != data);
}

static int
flash_write32(uint32_t addr, uint32_t data)
{
    return (flash_write16(addr + 0, (uint16_t) data) |
            flash_write16(addr + 2, (uint16_t) (data >> 16)));
}

int
stm32flash_erase(uint32_t addr, uint len)
{
    uint elen;

    flash_unlock();
    while (len > 0) {
        flash_page_erase(addr);
        elen = FL_PAGE_SIZE;
        if ((addr & (FL_PAGE_SIZE - 1)) != 0)
            elen = ((addr + FL_PAGE_SIZE - 1) & ~(FL_PAGE_SIZE - 1)) - addr;
        if (elen > len)
            elen = len;

        addr += elen;
        len  -= elen;
    }
    flash_lock();
    return (0);
}

int
stm32flash_read(uint32_t addr, uint len, void *buf)
{
    if (addr + len > 0x40000)
        return (RC_BAD_PARAM);
    memcpy(buf, (void *)((uint)addr | FLASH_BASE), len);
    return (0);
}

int
stm32flash_write(uint32_t addr, uint len, void *buf, uint flags)
{
    int      rc   = 0;
    uint8_t *bufp = (uint8_t *) buf;

    if (addr + len > 0x40000)
        return (RC_BAD_PARAM);

    if ((flags & STM32FLASH_FLAG_AUTOERASE) &&
        ((addr & (FL_PAGE_SIZE - 1)) == 0)) {
        /* Write at the start of a page */
        rc = stm32flash_erase(addr, len);
        if (rc != 0)
            return (rc);
    }
    addr += FLASH_BASE;

    flash_unlock();
    while (len > 0) {
        uint plen = len;

        if (addr & 1)
            plen = 1;
        else if ((addr & 2) && (plen != 1))
            plen = 2;
        else if (plen > 4)
            plen = 4;

        switch (plen) {
            case 1: {
                uint16_t val = *(uint16_t *) addr;
                if (addr & 1)
                    val = (val & 0x00ff) | *bufp;
                else
                    val = (val & 0xff00) | *bufp;
                rc += flash_write16(addr, val);
                break;
            }
            case 2:
                rc += flash_write16(addr, *(uint16_t *) bufp);
                break;
            case 4:
                rc += flash_write32(addr, *(uint32_t *) bufp);
                break;
        }
        addr += plen;
        bufp += plen;
        len  -= plen;
    }
    flash_lock();

    FLASH_ACR &= ~FLASH_ACR_DCEN;  // flash_dcache_disable
    FLASH_ACR |= FLASH_ACR_DCRST;  // flash_dcache_reset
    FLASH_ACR |= FLASH_ACR_DCEN;   // flash_dcache_enable

    return (rc);
}
#endif /* STM32F1 */

#ifdef STM32F4

/*
 * STM32F205 / STM32F215 / STM32F207 / STM32F217
 *
 *            |  Sector 0  0x08000000 - 0x08003fff  16 KB
 *            |  Sector 1  0x08004000 - 0x08007fff  16 KB
 *            |  Sector 2  0x08008000 - 0x0800bfff  16 KB
 *            |  Sector 3  0x0800c000 - 0x0800ffff  16 KB
 * User Flash |  Sector 4  0x08010000 - 0x0801ffff  64 KB
 *            |  Sector 5  0x08020000 - 0x0803ffff  128 KB
 *            |  Sector 6  0x08040000 - 0x0808ffff  128 KB
 * ...        |
 *            |  Sector 11 0x080e0000 - 0x080fffff  128 KB
 *
 * System Memory (DFU)     0x1fff0000 - 0x1fff77ff  30 KB
 * OTP area                0x1fff7800 - 0x1fff7a0f  528 bytes
 * Option bytes            0x1fffc000 - 0x1fffc00f  16 bytes
 *
 * Option byte 0
 *      bit 1:0    Not used (11)
 *      bit 2:3    BOR level (reset voltage threshold)
 *                   00 = 2.70V to 3.60V   01 = 2.40V to 2.70V
 *                   10 = 2.10V to 2.40V   11 = 1.8V to 2.10V
 *      bit 4      Not used (1)
 *      bit 5      0=HW Watchdog, 1=SW Watchdog
 *      bit 6      0=Reset at Stop enter, 1=No reset
 *      bit 7      0=Reset at Standby enter, 1=No reset
 * Option byte 8
 *      bit 0:7    1=Write protect not active on sector (0-7)
 * Option byte 9
 *      bit 0:3    1=Write protect not active on sector (8-11)
 *      bit 4:7    Unused (1)
 */

/*
 * flash_sector_size
 * -----------------
 * Returns the size in bytes of the specified flash sector
 */
static uint32_t
flash_sector_size(uint32_t sector)
{
    /*
     * Sectors 0-3  16 KB
     * Sectors 4-11 128 KB
     */
    if (sector < 4)
        return (0x4000);
    else if (sector < 12)
        return (0x20000);
    else
        return (0);
}

/*
 * flash_addr_to_sector
 * --------------------
 * Returns the flash sector number corresponding to the specified address
 */
static uint32_t
flash_addr_to_sector(uint32_t addr)
{
    if (addr < 0x10000)
        return (addr >> 14);
    else
        return (4 + (addr >> 17));
}

/*
 * flash_is_erased
 * ---------------
 * Returns true if the specified flash area has already been erased
 */
static bool_t
flash_is_erased(uint32_t addr, uint len)
{
    /*
     * Assume that addr is 4-byte aligned because flash_write() already verified
     * alignment.
     */
    addr += FLASH_BASE;

    if ((addr & 3) == 0) {
        while (len >= sizeof (uint32_t)) {
            if (*ADDR32(addr) != 0xffffffff)
                return (false);

            addr += sizeof (uint32_t);
            len  -= sizeof (uint32_t);
        }
    }

    while (len >= sizeof (uint8_t)) {
        if (*ADDR8(addr) != 0xff)
            return (false);

        addr += sizeof (uint8_t);
        len  -= sizeof (uint8_t);
    }

    return (true);
}

typedef enum {
    FLASH_MODE_NORMAL    = 0,
    FLASH_MODE_AUTOERASE = 1,
    FLASH_MODE_ERASE     = 2,
    FLASH_MODE_PROTECT   = 3
} flash_write_mode_t;

static rc_t
flash_write(uint32_t addr, uint nbytes, const void *datap, uint mode)
{
    uintptr_t data = (uintptr_t) datap;
    uint32_t sector = flash_addr_to_sector(addr);
    uint32_t size = flash_sector_size(sector);

    if (addr + nbytes > STM32FLASH_SIZE)
        return (RC_BAD_PARAM);

    if (FLASH_CR & FLASH_CR_LOCK)
        flash_unlock();

    switch (mode) {
        case FLASH_MODE_AUTOERASE:
        case FLASH_MODE_ERASE:
            if ((addr & (size - 1)) == 0)
                flash_erase_sector(sector << 3, FLASH_CR_PROGRAM_X32);
            else if (mode == FLASH_MODE_ERASE)
                return (RC_FAILURE);
            break;
        case FLASH_MODE_PROTECT:
            if (!flash_is_erased(addr, nbytes))
                return (RC_PROTECT);
            break;
        default:
            break;
    }

    addr += FLASH_BASE;
    while (nbytes > 0) {
        uintptr_t align = data | addr;
        if ((nbytes >= sizeof (uint32_t)) &&
            ((align & (sizeof (uint32_t) - 1)) == 0)) {
            /* Program in 32-bit writes where possible */
            flash_program_word(addr, *ADDR32(data));
            addr   += sizeof (uint32_t);
            data   += sizeof (uint32_t);
            nbytes -= sizeof (uint32_t);
        } else if ((nbytes >= sizeof (uint16_t)) &&
                   ((align & (sizeof (uint16_t) - 1)) == 0)) {
            /* Program in 16-bit writes where possible */
            flash_program_half_word(addr, *ADDR16(data));
            addr   += sizeof (uint16_t);
            data   += sizeof (uint16_t);
            nbytes -= sizeof (uint16_t);
        } else {
            /* Otherwise, program in 8-bit writes */
            flash_program_byte(addr, *ADDR8(data));
            addr   += sizeof (uint8_t);
            data   += sizeof (uint8_t);
            nbytes -= sizeof (uint8_t);
        }
    }

    /* Flush the data cache */
    flash_dcache_disable();
    flash_dcache_reset();
    flash_dcache_enable();

    return (RC_SUCCESS);
}

int
stm32flash_erase(uint32_t addr, uint len)
{
    if (addr + len > STM32FLASH_SIZE)
        return (RC_BAD_PARAM);
    while (len > 0) {
        uint32_t sector = flash_addr_to_sector(addr);
        uint32_t size   = flash_sector_size(sector);

        flash_erase_sector(sector << 3, FLASH_CR_PROGRAM_X32);

        addr += size;
        if (size > len)
            len = 0;
        else
            len -= size;
    }

    /* Flush the data cache */
    flash_dcache_disable();
    flash_dcache_reset();
    flash_dcache_enable();
    return (RC_SUCCESS);
}

int
stm32flash_write(uint32_t addr, uint len, void *buf, uint flags)
{
    uint mode = (flags & STM32FLASH_FLAG_AUTOERASE) ? FLASH_MODE_AUTOERASE :
                                                      FLASH_MODE_NORMAL;
    return (flash_write(addr, len, buf, mode));
}

int
stm32flash_read(uint32_t addr, uint len, void *buf)
{
    if (addr + len > STM32FLASH_SIZE)
        return (RC_BAD_PARAM);
    memcpy(buf, (void *)((uint)addr | FLASH_BASE), len);
    return (0);
}
#endif /* STM32F4 */
