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

#ifndef __MX29F1615_H
#define __MX29F1615_H

void     mx_enable(void);
void     mx_disable(void);
int      mx_read(uint32_t addr, uint16_t *data, uint count);
int      mx_write(uint32_t addr, uint16_t *data, uint count);
uint32_t mx_id(void);
void     mx_read_mode(void);
int      mx_erase(uint mode, uint32_t addr, uint32_t len, int verbose);
uint16_t mx_status_read(char *status, uint status_len);
void     mx_status_clear(void);
void     mx_cmd(uint32_t addr, uint16_t cmd, int vpp_delay);
int      mx_vcc_is_on(void);
int      mx_vpp_is_on(void);
void     mx_poll(void);
int      mx_verify(int verbose);

#define MX_ERASE_MODE_CHIP   0
#define MX_ERASE_MODE_SECTOR 1

#endif /* __MX29F1615_H */
