/*
 * This is free and unencumbered software released into the public domain.
 * See the LICENSE file for additional details.
 *
 * Designed by Chris Hooper in 2020.
 *
 * ---------------------------------------------------------------------
 *
 * Command line interpreter.
 */

#ifdef EMBEDDED_CMD
#include "printf.h"
#include "main.h"
#include "uart.h"
#include <stdlib.h>
#else /* !EMBEDDED_CMD */
#include <stdio.h>
#include <stdlib.h>
#ifndef AMIGA
#include <err.h>
#include <unistd.h>
#endif /* !AMIGA */
#endif /* !EMBEDDED_CMD */

#include <string.h>
#include <ctype.h>
#include "cmdline.h"
#include "cmds.h"
#include "led.h"
#include "readline.h"
#ifdef EMBEDDED_CMD
#include "pcmds.h"
#else
#include "sfile.h"
#endif

typedef struct {
    rc_t      (*cl_func)(int argc, char * const *argv);
    const char *cl_name;
    int         cl_len;
    const char *cl_help_long;
    const char *cl_help_args;
    const char *cl_help_desc;
} cmd_t;

static rc_t cmd_help(int argc, char * const *argv);

static const cmd_t cmd_list[] = {
    { cmd_help,    "?",       0, NULL, " [<cmd>]", "display help" },
    { cmd_copy,    "copy",    3, cmd_copy_help,
                        "[bwlqoh] <saddr> <daddr> <len>", "copy memory" },
    { cmd_comp,    "comp",    2, cmd_comp_help,
                        "[bwlqoh] <addr1> <addr2> <len>", "compare memory" },
#ifdef EMBEDDED_CMD
    { cmd_cpu,     "cpu",     2, cmd_cpu_help, " regs|usb", "CPU information" },
#endif
    { cmd_c,       "c",       1, cmd_c_help,
                        "[bwlqohS] <addr> <value...>", "change memory" },
    { cmd_delay,   "delay",   2, NULL, "<time> [s|ms|us]", "delay for time" },
    { cmd_d,       "d",       1, cmd_d_help,
                        "[bwlqohRS] <addr> [<len>]", "display memory" },
    { cmd_echo,    "echo",    0, NULL, " <text>", "display text" },
#ifdef EMBEDDED_CMD
    { cmd_gpio,    "gpio",    1, cmd_gpio_help, " show", "show GPIOs" },
#endif
    { cmd_ignore,  "ignore",  0, NULL, " <cmd>", "ignore result of command" },
    { cmd_help,    "help",    0, NULL, " [<cmd>]", "display help" },
    { cmd_history, "history", 4, NULL, "", "show history" },
    { cmd_loop,    "loop",    0, NULL,
                        " <count> <cmd>", "execute command multiple times" },
#ifdef EMBEDDED_CMD
    { cmd_map,     "map",     1, NULL, "", "show memory map" },
#endif
    { cmd_echo,    "print",   0, NULL, " <text>", "display text" },
#ifndef EMBEDDED_CMD
    { cmd_echo,    "quit",    1, NULL, "", "exit program" },
#endif
    { cmd_patt,    "patt",    2, cmd_patt_help,
                        "[bwlqoh] <addr> <len> <pattern>", "pattern memory" },
    { cmd_test,    "test",    2, cmd_test_help,
                        "[bwlqoh] <addr> <len> <testtype>", "test memory" },
#ifdef EMBEDDED_CMD
    { cmd_prom,    "prom",    1, cmd_prom_help, " [erase|id|read|write|...]",
                        "perform EEPROM operation" },
    { cmd_reset,   "reset",   0, cmd_reset_help, " [dfu]", "reset CPU" },
    { cmd_time,    "time",    0, cmd_time_help, " cmd|now|watch>",
                        "measure or show time" },
    { cmd_usb,     "usb",    0, cmd_usb_help, " disable|regs|reset",
                        "show or change USB status" },
#else
    { cmd_time,    "time",    0, cmd_time_help, " cmd <cmd>",
                        "measure command execution time" },
#endif
    { cmd_version, "version", 1, NULL, "", "show version" },
};

static rc_t
cmd_help(int argc, char * const *argv)
{
    int    cur;
    rc_t   rc = RC_SUCCESS;
    int    arg;

    if (argc <= 1) {
        for (cur = 0; cur < ARRAY_SIZE(cmd_list); cur++) {
            int len = strlen(cmd_list[cur].cl_name) +
                      strlen(cmd_list[cur].cl_help_args);
            printf("%s%s", cmd_list[cur].cl_name, cmd_list[cur].cl_help_args);
            if (len < 38)
                printf("%*s", 38 - len, "");
            printf("- %s\n", cmd_list[cur].cl_help_desc);
        }

        return (RC_SUCCESS);
    }
    for (arg = 1; arg < argc; arg++) {
        bool_t matched = FALSE;
        for (cur = 0; cur < ARRAY_SIZE(cmd_list); cur++) {
            int         cl_len  = cmd_list[cur].cl_len;
            const char *cl_name = cmd_list[cur].cl_name;

            if ((strcmp(argv[arg], cl_name) == 0) ||
                ((cl_len != 0) && (strncmp(argv[arg], cl_name, cl_len) == 0))) {
                printf("%s%s - %s\n", cl_name,
                       cmd_list[cur].cl_help_args, cmd_list[cur].cl_help_desc);
                if (cmd_list[cur].cl_help_long != NULL)
                    printf("%s\n", cmd_list[cur].cl_help_long);
                matched = TRUE;
                break;
            }
        }
        if (matched == FALSE) {
            printf("Unknown command \"%s\"\n", argv[arg]);
            rc = RC_FAILURE;
        }
        matched = TRUE;
    }
    return (rc);
}

int
make_arglist(const char *cmd, char *argv[])
{
    int         do_split = 0;
    int         next_split = 0;
    int         args = 0;
    bool_t      in_squotes = FALSE;
    bool_t      in_dquotes = FALSE;
    const char *ptr;
    char        lch = '\0';
    char        buf[128];
    int         cur = 0;

    for (ptr = cmd; *ptr != '\0'; lch = *ptr, ptr++) {
        char   ch   = *ptr;
        char   nch  = ptr[1];

        do_split += next_split;
        next_split = 0;

        if (ch == '\\') {
            if (nch != '\0') {
                buf[cur++] = nch;
                ptr++;
            }
            continue;
        }

        if ((ch == '\'') && (!in_dquotes)) {
            in_squotes = !in_squotes;
            continue;
        }
        if ((ch == '"') && (!in_squotes)) {
            in_dquotes = !in_dquotes;
            continue;
        }
        if (!in_squotes && !in_dquotes) {
            switch (ch) {
                case ' ':
                    do_split = 1;
                    break;
                case ';':
                    do_split += 2;
                    break;
                case '&':
                    if (lch == '&')
                        next_split++;
                    else if (nch == '&')
                        do_split++;
                    break;
                case '|':
                    if (lch == '|')
                        next_split++;
                    else if (nch == '|')
                        do_split++;
                    break;
            }
        }
#ifdef DEBUG_ARGSPLIT
        printf("argsplit %d %s\n", cur, ptr);
#endif

        if (do_split > 0) {
            do_split--;
            if (cur > 0) {
                buf[cur] = '\0';
                argv[args] = malloc(cur + 1);
                if (argv[args] == NULL)
                    errx(EXIT_FAILURE, "Unable to allocate memory");
                strncpy(argv[args], buf, cur);
                argv[args][cur] = '\0';
#ifdef DEBUG_ARGSPLIT
                printf("arg[%d]=%s\n", args, argv[args]);
#endif
                args++;
                if (args >= MAX_ARGS - 1) {
                    warnx("Too many arguments");
                    goto finish;
                }

                cmd = ptr;
                while (*cmd == ' ')
                    cmd++;
                if (ptr < cmd - 1)
                    ptr = cmd - 1;
                lch = '\0';
                cur = 0;
            }
            if (ch == ' ')
                continue;
        }
        buf[cur++] = ch;
    }
    if (cur > 0) {
        /* Add buffer as last argument */
        buf[cur] = '\0';
        argv[args] = strdup(buf);
        if (argv[args] == NULL)
            errx(EXIT_FAILURE, "Unable to allocate memory");
#ifdef DEBUG_ARGSPLIT
        printf("arg[%d]=%s\n", args, argv[args]);
#endif
        args++;
    }
finish:
    argv[args] = NULL;
    return (args);
}

void
free_arglist(int argc, char *argv[])
{
    int arg;
    for (arg = 0; arg < argc; arg++)
        free(argv[arg]);
}

#ifdef DEBUG_ARGLIST
static void
print_arglist(int argc, char * const *argv)
{
    int arg;
    for (arg = 0; arg < argc; arg++)
        printf("    argv[%d] = \"%s\"\n", arg, argv[arg]);
}
#endif

char *
cmd_string_from_argv(int argc, char * const *argv)
{
    int    arg;
    size_t curlen = 0;
    size_t cmdbuf_len = 64;
    char  *cmdbuf = malloc(cmdbuf_len);

    if (cmdbuf == NULL)
        errx(EXIT_FAILURE, "Unable to allocate memory");

    cmdbuf[curlen] = '\0';

    for (arg = 0; arg < argc; arg++)  {
        int len = strlen(argv[arg]);
        if (curlen + len + 2 > cmdbuf_len) {
            cmdbuf_len *= 2;
            cmdbuf = realloc(cmdbuf, cmdbuf_len);
            if (cmdbuf == NULL)
                errx(EXIT_FAILURE, "Unable to allocate memory");
            cmdbuf[curlen] = '\0';
        }
        if (curlen > 0)
            cmdbuf[curlen++] = ' ';
        strcpy(cmdbuf + curlen, argv[arg]);
        curlen += len;
    }
    if (*cmdbuf == '\0') {
        free(cmdbuf);
        cmdbuf = NULL;
    }
    return (cmdbuf);
}

rc_t
scan_int(const char *str, int *intval)
{
    int pos = 0;
    if (*str == '\0') {
        printf("No value supplied\n");
        return (RC_USER_HELP);
    }
    if ((sscanf(str, "%i%n", intval, &pos) != 1) ||
        (str[pos] != '\0')) {
        printf("Invalid value \"%s\"\n%*s^\n", str, 15 + pos, "");
        return (RC_USER_HELP);
    }
    return (RC_SUCCESS);
}

static rc_t
cmd_exec_argv_single(int argc, char * const *argv)
{
    int cur;
    int rc = RC_SUCCESS;
#ifdef DEBUG_ARGLIST
    printf("exec_argv\n");
    print_arglist(argc, argv);
#endif
    for (cur = 0; cur < ARRAY_SIZE(cmd_list); cur++) {
        int         cl_len  = cmd_list[cur].cl_len;
        const char *cl_name = cmd_list[cur].cl_name;

        if ((strcmp(argv[0], cl_name) == 0) ||
            ((cl_len != 0) && (strncmp(argv[0], cl_name, cl_len) == 0))) {
            rc = cmd_list[cur].cl_func(argc, argv);
            if (rc == RC_USER_HELP) {
                if (cmd_list[cur].cl_help_long != NULL)
                    printf("%s\n", cmd_list[cur].cl_help_long);
                else
                    printf("%s%s - %s\n", cl_name, cmd_list[cur].cl_help_args,
                           cmd_list[cur].cl_help_desc);
            }
#ifdef HAVE_SPACE_FILE
            file_cleanup_handles();
#endif
            break;
        }
    }
    if (cur == ARRAY_SIZE(cmd_list)) {
        int arg;
        printf("Unknown command:");
        for (arg = 0; arg < argc; arg++)
            printf(" %s", argv[arg]);
        printf("\n");
        return (RC_USER_HELP);
    }
    return (rc);
}

rc_t
cmd_exec_argv(int argc, char * const *argv)
{
    int  sarg;
    int  earg;
    rc_t rc = RC_SUCCESS;

    for (sarg = 0, earg = 0; earg < argc; earg++) {
        if (strcmp(argv[earg], ";") == 0) {
            rc = cmd_exec_argv_single(earg - sarg, argv + sarg);
            sarg = earg + 1;
        } else if (strcmp(argv[earg], "&&") == 0) {
            rc = cmd_exec_argv_single(earg - sarg, argv + sarg);
            if (rc != 0)
                goto finish;
            sarg = earg + 1;
        } else if (strcmp(argv[earg], "||") == 0) {
            rc = cmd_exec_argv_single(earg - sarg, argv + sarg);
            if (rc == 0)
                goto finish;
            sarg = earg + 1;
        }
    }
    if (sarg < earg)
        rc = cmd_exec_argv_single(earg - sarg, argv + sarg);

finish:
    return (rc);
}

static uint64_t
op_add(uint64_t arg1, uint64_t arg2)
{
    return (arg1 + arg2);
}

static uint64_t
op_sub(uint64_t arg1, uint64_t arg2)
{
    return (arg1 - arg2);
}

static uint64_t
op_mul(uint64_t arg1, uint64_t arg2)
{
    return (arg1 * arg2);
}

static uint64_t
op_div(uint64_t arg1, uint64_t arg2)
{
    if (arg2 == 0)
        return (-1);
    return (arg1 / arg2);
}

static uint64_t
op_mod(uint64_t arg1, uint64_t arg2)
{
    if (arg2 == 0)
        return (arg1);
    return (arg1 % arg2);
}

static uint64_t
op_and(uint64_t arg1, uint64_t arg2)
{
    return (arg1 & arg2);
}

static uint64_t
op_xor(uint64_t arg1, uint64_t arg2)
{
    return (arg1 ^ arg2);
}

static uint64_t
op_or(uint64_t arg1, uint64_t arg2)
{
    return (arg1 | arg2);
}

static uint64_t
op_not(uint64_t arg1, uint64_t arg2)
{
    return (!arg2);
}

static uint64_t
op_invert(uint64_t arg1, uint64_t arg2)
{
    return (~arg2);
}

static uint64_t
op_rshift(uint64_t arg1, uint64_t arg2)
{
    return (arg1 >> arg2);
}

static uint64_t
op_lshift(uint64_t arg1, uint64_t arg2)
{
    return (arg1 << arg2);
}

static uint64_t
op_ge(uint64_t arg1, uint64_t arg2)
{
    return (arg1 >= arg2);
}

static uint64_t
op_gt(uint64_t arg1, uint64_t arg2)
{
    return (arg1 > arg2);
}

static uint64_t
op_lt(uint64_t arg1, uint64_t arg2)
{
    return (arg1 < arg2);
}

static uint64_t
op_le(uint64_t arg1, uint64_t arg2)
{
    return (arg1 <= arg2);
}

static uint64_t
op_eq(uint64_t arg1, uint64_t arg2)
{
    return (arg1 == arg2);
}

static uint64_t
op_ne(uint64_t arg1, uint64_t arg2)
{
    return (arg1 != arg2);
}

static uint64_t
op_l_and(uint64_t arg1, uint64_t arg2)
{
    return (arg1 && arg2);
}

static uint64_t
op_l_or(uint64_t arg1, uint64_t arg2)
{
    return (arg1 || arg2);
}

#define SINGLE_ARG 1
#define DUAL_ARG   2

typedef struct {
    uint64_t  (*op_func)(uint64_t arg1, uint64_t arg2);
    const char *op_name;   /* String match for operator */
    int         op_len;    /* Length of operator */
    int         op_level;  /* Precedence level */
    int         op_args;   /* Argument count */
} ops_t;

/*
 * These operations must be in order of precedence (highest first).
 * Operations at the same precedence level should use the same
 * precedence number so that left-right precedence is performed.
 */
static const ops_t math_ops[] = {
    { op_not,     "!",  1,  0, SINGLE_ARG },
    { op_invert,  "~",  1,  0, SINGLE_ARG },
    { op_mul,     "*",  1,  2, DUAL_ARG },
    { op_div,     "/",  1,  2, DUAL_ARG },
    { op_mod,     "%",  1,  2, DUAL_ARG },
    { op_add,     "+",  1,  3, DUAL_ARG },
    { op_sub,     "-",  1,  3, DUAL_ARG },
    { op_rshift,  ">>", 2,  4, DUAL_ARG },
    { op_lshift,  "<<", 2,  4, DUAL_ARG },
    { op_le,      "<=", 2,  5, DUAL_ARG },
    { op_lt,      "<",  1,  5, DUAL_ARG },
    { op_ge,      ">=", 2,  5, DUAL_ARG },
    { op_gt,      ">",  1,  5, DUAL_ARG },
    { op_eq,      "==", 2,  6, DUAL_ARG },
    { op_ne,      "!=", 2,  6, DUAL_ARG },
    { op_and,     "&",  1,  7, DUAL_ARG },
    { op_xor,     "^",  1,  8, DUAL_ARG },
    { op_or,      "|",  1,  9, DUAL_ARG },
    { op_l_and,   "&&", 2, 10, DUAL_ARG },
    { op_l_or,    "||", 2, 11, DUAL_ARG },
/*  { op_ternary, "?",  2, 12, DUAL_ARG },   NOT SUPPORTED YET */
};

#undef DEBUG_EVAL


static rc_t
eval_string_expr(char *str, int len, char **full_str)
{
    int      op_base;
    int      op_len;
    int      op_num;
    char    *op_pos;
    bool_t   in_squotes = FALSE;
    bool_t   in_dquotes = FALSE;
    unsigned long long arg1 = 0;
    unsigned long long arg2;
    unsigned long long result;

#ifdef DEBUG_EVAL
    printf("eval '%.*s'\n", len, str);
#endif
    if (*str == '(') {
        str[0] = ' ';
        str[len - 1] = ' ';
    }

    for (op_base = 0; op_base < ARRAY_SIZE(math_ops); op_base++) {
        for (op_pos = str; op_pos < str + len; op_pos++) {
            if (*op_pos == '\\') {
                /* Skip escaped operators */
                if (++op_pos < str + len)
                    continue;
                break;
            }
            if ((*op_pos == '\'') && (!in_dquotes)) {
                /* Ignore single quoted expressions */
                in_squotes = !in_squotes;
                continue;
            }

            if ((*op_pos == '\"') && (!in_squotes)) {
                /* Ignore double quoted expressions */
                in_dquotes = !in_dquotes;
                continue;
            }
            if (in_squotes || in_dquotes)
                continue;

            for (op_num = op_base; op_num < ARRAY_SIZE(math_ops); op_num++) {
                if ((op_num != op_base) &&
                    (math_ops[op_num].op_level != math_ops[op_base].op_level)) {
                    /* Skip to the next operator */
                    break;
                }
                op_len = math_ops[op_num].op_len;

                if (strncmp(op_pos, math_ops[op_num].op_name, op_len) == 0) {
                    int op_position = (int) (op_pos - str);
                    int pos = 0;
                    int epos = 0;
                    int spos = 0;
                    bool_t no_eval = FALSE;
                    arg1 = 0;

                    if (math_ops[op_num].op_args == SINGLE_ARG) {
                        /* Special case for !: check for != */
                        if ((op_pos[0] == '!') && (op_pos[1] == '='))
                            goto continue_op_search;

                        for (spos = op_position - 1; spos > 0; spos--) {
                            if (str[spos] != ' ') {
                                spos++;
                                break;
                            }
                        }
                        if (spos > 0 &&
                            str[spos] == ' ' && str[spos - 1] != ' ')
                            spos++;  /* Leave in padding from prev keyword */
                    } else if (math_ops[op_num].op_args == DUAL_ARG) {
                        /* Special case for && and || */
                        if ((op_pos[0] == '&') && (op_pos[1] == '&') &&
                            (op_len == 1))
                            continue;
                        if ((op_pos[0] == '|') && (op_pos[1] == '|') &&
                            (op_len == 1))
                            continue;

                        /* Skip whitespace before operator */
                        for (spos = op_position - 1; spos > 0; spos--)
                            if (str[spos] != ' ')
                                break;
                        /* Walk backward to find start of arg */
                        while (spos > 0) {
                            if (!isxdigit((uint8_t)str[spos]) &&
                                (str[spos] != 'x' || str[spos - 1] != '0')) {
                                int opn;
                                char ch = str[spos];
                                spos++;
                                if (ch == ' ')
                                    break;
                                for (opn = 0; opn < ARRAY_SIZE(math_ops); opn++)
                                    if (ch == *math_ops[opn].op_name)
                                        break;
                                if (opn < ARRAY_SIZE(math_ops))
                                    break;

                                /*
                                 * Not preceded by a math op or whitespace.
                                 * Don't do math on this expression.
                                 */
                                no_eval = TRUE;
                                break;
                            }
                            spos--;
                        }
                        if (no_eval)
                            continue;
                        if (spos > 0 &&
                            str[spos] == ' ' && str[spos - 1] != ' ')
                            spos++;  /* Leave in padding from prev keyword */
#ifdef DEBUG_EVAL
                        printf("start=%s %d\n", str + spos, spos);
#endif
                        if ((spos == op_position) ||
                            ((str[spos] == ' ') && (spos == op_position - 1))) {
                            /* Did not find second arg -- skip eval */
                            continue;
                        }
                        if (sscanf(str + spos, "%llx%n", &arg1, &pos) != 1) {
invalid_arg1:
                            /* Skip this evaluation and continue */
                            continue;
#ifdef DEBUG_INVALID_LHS
                            printf("Invalid lhs \"%.*s\" for math %s "
                                   "op\n%*s^\n", op_position,
                                   str + spos, math_ops[op_num].op_name,
                                   pos + 19, "");
                            return (RC_FAILURE);
#endif
                        }
                        while (str[spos + pos] == ' ')
                            pos++;
                        if (spos + pos < op_position)
                            goto invalid_arg1;
                    }

                    if (sscanf(op_pos + op_len, "%llx%n", &arg2, &epos) != 1) {
                        /* Skip this evaluation and continue */
                        continue;
#ifdef DEBUG_INVALID_LHS
                        printf("Invalid rhs \"%.*s\" for math %s op\n%*s^\n",
                               len - (op_position + op_len), op_pos + op_len,
                               math_ops[op_num].op_name, 19 + epos, "");
                        return (RC_FAILURE);
#endif
                    }
                    epos += op_position + op_len;
                    if (str[epos] != '\0')
                        while (str[epos + 1] == ' ')
                            epos++;

#ifdef DEBUG_EVAL
                    printf("arg1=0x%llx arg2=0x%llx\n", arg1, arg2);
                    printf("found %s at pos %d\n",
                           math_ops[op_num].op_name, (int) (op_pos - str));
#endif
                    result = math_ops[op_num].op_func(arg1, arg2);
#ifdef DEBUG_EVAL
                    printf("overlay: %s\n", str);
#endif
                    int pocket_need;
                    int pocket_len = epos - spos + 1;
                    char echar = str[epos];
#ifdef AMIGA
                    /* DICE does not have snprintf */
                    char temp_buf[32];
                    pocket_need = sprintf(temp_buf, "%llx", result);
                    if (pocket_len >= pocket_need)
                        pocket_need = sprintf(str + spos, "%llx", result);
#else
                    pocket_need = snprintf(str + spos, pocket_len,
                                           "%llx", result);
#endif
                    pocket_need++;  /* Account for '\0' */
                    str[epos] = echar;  /* replace '\0' with original */
#ifdef DEBUG_EVAL
                    printf("pocket start=%d len=%d need=%d\n",
                           spos, pocket_len, pocket_need);
                    printf("epos=%s\n", str + epos);
                    printf("intermd: %s\n", str);
#endif
                    if (pocket_len >= pocket_need) {
                        /* Enough space -- move left the remaining string */
                        int traillen = strlen(str + epos) + 1;
                        memmove(str + spos + pocket_need - 1,
                                str + epos, traillen);
                        /* Move current pointer to start of moved string */
                        op_pos = str + spos;
                    } else {
                        /* Not enough space - must reallocate new source str */
                        int   strpos   = str - *full_str;
                        int   traillen = strlen(str + epos) + 1;
                        int   oldlen   = traillen + (str + epos - *full_str);
                        int   wantlen  = oldlen + pocket_need - pocket_len + 1;
                        char *newstr   = realloc(*full_str, wantlen);

                        /* Update caller's full string pointer */
                        *full_str = newstr;
                        str = *full_str + strpos;

#ifdef DEBUG_EVAL
                        printf("expand oldlen=%d wantlen=%d traillen=%d\n",
                               oldlen, wantlen, traillen);
                        printf("str premove='%s'\n", str);
                        printf("str moving='%*.*s'\n", traillen - 1,
                               traillen - 1, str + spos + pocket_len);
#endif

                        /* Now that there is space, push following data */
                        memmove(str + spos + pocket_need,
                                str + spos + pocket_len, traillen);
#ifdef DEBUG_EVAL
                        printf("str postmove='%s'\n", str);
#endif

                        /* Insert new value again */
                        (void) sprintf(str + spos, "%llx", result);
                        /* replace '\0' with original */
                        str[spos + pocket_need - 1] = echar;

                        /* Tell caller to try again (reinitialize pointers) */
                        return (RC_BUSY);
                    }
#ifdef DEBUG_EVAL
                    printf("     to: %s\n", str);
                    printf("%d %d %d\n", op_position, epos, pos);
#endif
                    break; /* Stop trying to match at this position */
                }
            }
        }
        while (op_base < ARRAY_SIZE(math_ops)) {
            if (math_ops[op_base].op_level == math_ops[op_base + 1].op_level) {
                op_base++;
            } else
                break;
        }
continue_op_search:  ; /* Allow label before end of control struct */
    }
#ifdef DEBUG_EVAL
    printf("final \"%.*s\"\n", len, str);
#endif
    return (RC_SUCCESS);
}

char *
eval_cmdline_expr(const char *str)
{
    char *ptr;
    char *buf = strdup(str);
    char *sptr = NULL;

    if (buf == NULL)
        errx(EXIT_FAILURE, "Unable to allocate memory");

    /* Repeatedly scan string, evaluating expressions within parens first */
eval_again:
    for (ptr = buf; *ptr != '\0'; ptr++) {
        if (*ptr == '(') {
            sptr = ptr;
        } else if (*ptr == ')') {
            if (sptr == NULL) {
                printf("Close paren before open in expression:\n  %s\n%*s",
                        buf, (int) (ptr - buf), "");
                free(buf);
                return (NULL);
            }
            (void) eval_string_expr(sptr, 1 + ptr - sptr, &buf);
            goto eval_again;
        }
    }
    if (eval_string_expr(buf, ptr - buf, &buf) == RC_BUSY)
        goto eval_again;

    ptr = strdup(buf);
    if (ptr == NULL)
        errx(EXIT_FAILURE, "Unable to allocate memory");
    free(buf);
    return (ptr);
}

int
cmd_exec_string(const char *cmd)
{
    int   argc;
    int   rc      = RC_SUCCESS;
    char *cmdline = eval_cmdline_expr(cmd);
    char *argv[MAX_ARGS];

    if (cmdline == NULL)
        return (RC_USER_HELP);
    argc = make_arglist(cmdline, argv);
    free(cmdline);

#ifdef DEBUG_ARGLIST
    printf("got cmdline \"%s\"\n", cmd);
    print_arglist(argc, argv);
    printf("\n");
#endif
    rc = cmd_exec_argv(argc, argv);

    free_arglist(argc, argv);
    return (rc);
}

static char *
no_whitespace(char *line)
{
    char *ptr;

    /* Remove leading whitespace */
    while (isspace((uint8_t)*line))
        line++;

    /* Remove trailing whitespace */
    for (ptr = line + strlen(line) - 1; ptr > line; ptr--)
        if (!isspace((uint8_t)*ptr)) {
            *(++ptr) = '\0';
            break;
        }

    return (line);
}

#ifdef EMBEDDED_CMD
int
cmdline(void)
{
    char *line;

    (void) get_new_input_line("CMD> ", &line);
    if (line != NULL) {
        HIST_ENTRY *hist_cur;
        char *sline = no_whitespace(line);
        if (sline[0] == '\0')
            return (0);

        if ((strcmp(sline, "q") == 0) || (strcmp(sline, "quit") == 0)) {
            *line = '\0';
            return (0);
        }

        led_busy(1);
        hist_cur = history_get(history_length + history_base - 1);
        if ((hist_cur == NULL) || (strcmp(sline, hist_cur->line) != 0)) {
            /* Not a duplicate of previous line; add to history. */
            add_history(sline);
        }
        (void) cmd_exec_string(sline);
        *line = '\0';
        led_busy(0);
    }
    return (0);
}
#else /* !EMBEDDED_CMD */
int
cmdline(void)
{
    rc_t rc = RC_SUCCESS;
    char *line;

    if (isatty(0) == 0) {
        char line[256];
        while (fgets(line, sizeof (line) - 1, stdin) != NULL) {
            /* Got line -- eliminate CR/LF */
            char *ptr = strchr(line, '\r');
            if (ptr == NULL)
                ptr = strchr(line, '\n');
            if (ptr != NULL)
                *ptr = '\0';

            if (line[0] == '\0')
                continue;
            rc = cmd_exec_string(line);
        }
        return (rc);
    }

    /* Interactive user -- enable command editing and history */
    (void) rl_initialize();
    (void) rl_bind_key('\t', rl_insert);  /* No auto-complete */

    using_history();

    while ((line = readline("\rmed> ")) != NULL) {
        HIST_ENTRY *hist_cur;
        char *sline = no_whitespace(line);
        if (sline[0] == '\0')
            continue;

        if ((strcmp(sline, "q") == 0) || (strcmp(sline, "quit") == 0)) {
            rc = RC_SUCCESS;
            break;
        }
        hist_cur = history_get(history_length + history_base - 1);
        if ((hist_cur == NULL) || (strcmp(sline, hist_cur->line) != 0)) {
            /* Not a duplicate of previous line; add to history. */
            add_history(sline);
        }
        rc = cmd_exec_string(sline);

        free(line);
    }

    return (rc);
}
#endif /* !EMBEDDED_CMD */
