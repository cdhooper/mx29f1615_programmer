/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * EEPROM high level access code for MX29F1615 programmer.
 */

#include "main.h"
#include "cmdline.h"
#include "prom_access.h"
#include "mx29f1615.h"
#include "printf.h"
#include "uart.h"
#include <stdbool.h>
#include "timer.h"
#include "crc32.h"

#define DATA_CRC_INTERVAL 256

rc_t
prom_read(uint32_t addr, uint width, void *bufp)
{
    uint16_t val;
    uint8_t *buf = (uint8_t *) bufp;

#undef DEBUG_PROM_READ
#ifdef DEBUG_PROM_READ
    int pos;
    for (pos = 0; pos < width; pos++)
        buf[pos] = (uint8_t) (pos + addr);
    return (RC_SUCCESS);
#endif

    mx_enable();
    if (addr & 1) {
        /* Handle odd start address */
        if (mx_read(addr >> 1, &val, 1))
            return (RC_FAILURE);
        *buf = val >> 8;
        buf++;
        addr++;
        width--;
    }

    if (mx_read(addr >> 1, (uint16_t *)buf, width >> 1))
        return (RC_FAILURE);
    if (width & 1) {
        /* Handle odd trailing byte */
        buf  += width - 1;
        addr += width;
        if (mx_read(addr >> 1, &val, 1))
            return (RC_FAILURE);
        *buf = (uint8_t) val;
    }
    return (RC_SUCCESS);
}

rc_t
prom_write(uint32_t addr, uint width, void *bufp)
{
    uint16_t val;
    uint8_t *buf = (uint8_t *) bufp;
    mx_enable();
    if (addr & 1) {
        /* Handle odd start address */
        if (mx_read(addr >> 1, &val, 1))
            return (RC_FAILURE);
        val = (val & 0xff) | (*buf << 8);
        if (mx_write(addr >> 1, &val, 1))
            return (RC_FAILURE);
        buf++;
        addr++;
        width--;
    }
    if (mx_write(addr >> 1, (uint16_t *)buf, width >> 1))
        return (RC_FAILURE);
    if (width & 1) {
        /* Handle odd trailing byte */
        buf  += width - 1;
        addr += width;
        if (mx_read(addr >> 1, &val, 1))
            return (RC_FAILURE);
        val = (val & 0xff00) | *buf;
        if (mx_write(addr >> 1, &val, 1))
            return (RC_FAILURE);
    }

    return (RC_SUCCESS);
}

rc_t
prom_erase(uint mode, uint32_t addr, uint32_t len)
{
    mx_enable();
    return (mx_erase(mode, addr >> 1, len >> 1, 1));
}

void
prom_cmd(uint32_t addr, uint16_t cmd)
{
    mx_enable();
    mx_cmd(addr, cmd, 1);
}

void
prom_id(void)
{
    mx_enable();
    printf("%08lx\n", mx_id());
}

void
prom_status(void)
{
    char status[64];
    mx_enable();
    printf("%04x %s\n", mx_status_read(status, sizeof (status)), status);
}

void
prom_status_clear(void)
{
    mx_enable();
    mx_status_clear();
}

static int
getchar_wait(uint pos)
{
    int      ch;
    uint64_t timeout = timer_tick_plus_msec(200);

    while ((ch = getchar()) == -1)
        if (timer_tick_has_elapsed(timeout))
            break;

    return (ch);
}

static int
check_crc(uint32_t crc, uint spos, uint epos, bool send_rc)
{
    int      ch;
    size_t   pos;
    uint32_t compcrc;

    for (pos = 0; pos < sizeof (compcrc); pos++) {
        ch = getchar_wait(200);
        if (ch == -1) {
            printf("Receive timeout waiting for CRC %08lx at 0x%x\n",
                   crc, epos);
            return (RC_TIMEOUT);
        }
        ((uint8_t *)&compcrc)[pos] = ch;
    }
    if (crc != compcrc) {
        printf("Received CRC %08lx doesn't match %08lx at 0x%x-0x%x\n",
               compcrc, crc, spos, epos);
        return (1);
    }
    return (0);
}

static int
check_rc(uint pos)
{
    int ch = getchar_wait(200);
    if (ch == -1) {
        printf("Receive timeout waiting for rc at 0x%x\n", pos);
        return (RC_TIMEOUT);
    }
    if (ch != 0) {
        printf("Remote sent error %d at 0x%x\n", ch, pos);
        return (RC_FAILURE);
    }
    return (RC_SUCCESS);
}

/*
 * prom_read_binary() reads data from an EEPROM and writes it to the host.
 *                    Every 256 bytes, a rolling CRC value is expected back
 *                    from the host.
 */
rc_t
prom_read_binary(uint32_t addr, uint32_t len)
{
    rc_t     rc;
    uint8_t  buf[256];
    uint32_t crc = 0;
    uint     crc_next = DATA_CRC_INTERVAL;
    uint32_t cap_pos[4];
    uint     cap_count = 0;
    uint     cap_prod  = 0;  // producer
    uint     cap_cons  = 0;  // consumer
    uint     pos = 0;

    mx_enable();
    while (len > 0) {
        uint32_t tlen = sizeof (buf);
        if (tlen > len)
            tlen = len;
        if (tlen > crc_next)
            tlen = crc_next;
        rc = prom_read(addr, tlen, buf);
        if (puts_binary(&rc, 1)) {
            printf("Status send timeout at %lx\n", addr + pos);
            return (RC_TIMEOUT);
        }
        if (rc != RC_SUCCESS)
            return (rc);
        if (puts_binary(buf, tlen)) {
            printf("Data send timeout at %lx\n", addr + pos);
            return (RC_TIMEOUT);
        }

        crc = crc32(crc, buf, tlen);
        crc_next -= tlen;
        addr     += tlen;
        len      -= tlen;
        pos      += tlen;

        if (cap_count >= ARRAY_SIZE(cap_pos)) {
            /* Verify received RC */
            cap_count--;
            if (check_rc(cap_pos[cap_cons]))
                return (RC_FAILURE);
            if (++cap_cons >= ARRAY_SIZE(cap_pos))
                cap_cons = 0;
        }

        if (crc_next == 0) {
            /* Send and record the current CRC value */
            if (puts_binary(&crc, sizeof (crc))) {
                printf("Data send CRC timeout at %lx\n", addr + pos);
                return (RC_TIMEOUT);
            }
            cap_pos[cap_prod] = pos;
            if (++cap_prod >= ARRAY_SIZE(cap_pos))
                cap_prod = 0;
            cap_count++;
            crc_next = DATA_CRC_INTERVAL;
        }
    }
    if (crc_next != DATA_CRC_INTERVAL) {
        /* Send CRC for last partial segment */
        if (puts_binary(&crc, sizeof (crc)))
            return (RC_TIMEOUT);
    }

    /* Verify trailing CRC packets */
    cap_prod += ARRAY_SIZE(cap_pos) - cap_count;
    if (cap_prod >= ARRAY_SIZE(cap_pos))
        cap_prod -= ARRAY_SIZE(cap_pos);

    while (cap_count-- > 0) {
        if (check_rc(cap_pos[cap_cons]))
            return (1);
        if (++cap_cons >= ARRAY_SIZE(cap_pos))
            cap_cons = 0;
    }

    if (crc_next != DATA_CRC_INTERVAL) {
        /* Verify CRC for last partial segment */
        if (check_rc(pos))
            return (RC_FAILURE);
    }
    return (RC_SUCCESS);
}

/*
 * prom_write_binary() takes binary input from an application via the serial
 *                     console and writes that to the EEPROM. Every 256 bytes,
 *                     a rolling 8-bit CRC value is sent back to the host.
 *                     This is so the host knows that the data was received
 *                     correctly. Incorrectly received data will still be
 *                     written to the EEPROM.
 */
rc_t
prom_write_binary(uint32_t addr, uint32_t len)
{
    uint8_t  buf[128];
    int      ch;
    rc_t     rc;
    uint32_t crc = 0;
    uint32_t saddr = addr;
    uint     crc_next = DATA_CRC_INTERVAL;

    mx_enable();
    while (len > 0) {
        uint32_t tlen    = len;
        uint32_t rem     = addr & (sizeof (buf) - 1);
        uint64_t timeout = timer_tick_plus_msec(1000);
        uint32_t pos;
        uint8_t *ptr = buf;

        if (tlen > sizeof (buf) - rem)
            tlen = sizeof (buf) - rem;

        for (pos = 0; pos < tlen; pos++) {
            while ((ch = getchar()) == -1)
                if (timer_tick_has_elapsed(timeout)) {
                    printf("Data receive timeout at %lx\n", addr + pos);
                    rc = RC_TIMEOUT;
                    goto fail;
                }
            timeout = timer_tick_plus_msec(1000);
            *(ptr++) = ch;
            crc = crc32(crc, ptr - 1, 1);
            if (--crc_next == 0) {
                if (check_crc(crc, saddr, addr + pos + 1, false)) {
                    rc = RC_FAILURE;
                    goto fail;
                }
                rc = RC_SUCCESS;
                if (puts_binary(&rc, 1)) {
                    rc = RC_TIMEOUT;
                    goto fail;
                }
                crc_next = DATA_CRC_INTERVAL;
                saddr = addr + pos + 1;
            }
        }
        rc = prom_write(addr, tlen, buf);
        if (rc != RC_SUCCESS) {
fail:
            (void) puts_binary(&rc, 1);  // Inform remote side
            timeout = timer_tick_plus_msec(2000);
            while (!timer_tick_has_elapsed(timeout))
                (void) getchar();  // Discard input
            return (rc);
        }
        addr += tlen;
        len  -= tlen;
    }
    if (crc_next != DATA_CRC_INTERVAL) {
        if (check_crc(crc, saddr, addr, false)) {
            rc = RC_FAILURE;
            goto fail;
        }
        rc = RC_SUCCESS;
        if (puts_binary(&rc, 1)) {
            rc = RC_TIMEOUT;
            goto fail;
        }
    }
    return (RC_SUCCESS);
}

void
prom_disable(void)
{
    mx_disable();
}

int
prom_vcc_is_on(void)
{
    return (mx_vcc_is_on());
}

int
prom_vpp_is_on(void)
{
    return (mx_vpp_is_on());
}

int
prom_verify(int verbose)
{
    return (mx_verify(verbose));
}
