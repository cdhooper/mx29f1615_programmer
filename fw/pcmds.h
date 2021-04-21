/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Command implementations.
 */

#ifndef _PCMDS_H
#define _PCMDS_H

#define HAVE_SPACE_PROM

rc_t cmd_cpu(int argc, char * const *argv);
rc_t cmd_prom(int argc, char * const *argv);
rc_t cmd_reset(int argc, char * const *argv);
rc_t cmd_map(int argc, char * const *argv);

extern const char cmd_cpu_help[];
extern const char cmd_prom_help[];
extern const char cmd_reset_help[];

#endif  /* _PCMDS_H */
