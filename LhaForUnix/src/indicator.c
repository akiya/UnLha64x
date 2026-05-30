/* ------------------------------------------------------------------------ */
/* LHa for UNIX                                                             */
/*              indicator.c -- put indicator                                */
/*                                                                          */
/*      Modified                Nobutaka Watazaki                           */
/*                                                                          */
/*  Ver. 1.14   Source All chagned              1995.01.14  N.Watazaki      */
/*              Separated from append.c         2003.07.21  Koji Arai       */
/* ------------------------------------------------------------------------ */
#include "lha.h"

#define MAX_INDICATOR_COUNT     58

static off_t reading_size;

#ifdef NEED_INCREMENTAL_INDICATOR
static off_t indicator_count;
static long indicator_threshold;
#endif
static off_t next_msg_size = 0;

#define ALIGN(size, threshold) (((size) + ((threshold)-1))/(threshold))

static void
carriage_return()
{
    static int tty = -1;
    if (tty == -1) {
        if (isatty(1))          /* stdout */
            tty = 1;
        else
            tty = 0;
    }

    if (tty)
        fputs("\r", stdout);
    else
        fputs("\n", stdout);
}

void
start_indicator(char *name, off_t size, char *msg, long def_indicator_threshold)
{
#ifdef LHA_LIBRARY
    if (Lha_SendProgressMessage(0, name, 0, size)) {
        fatal_error("User cancelled.");
    }
#endif

#ifdef NEED_INCREMENTAL_INDICATOR
    long i;
    int m;
#endif

#ifdef LHA_LIBRARY
    Lha_SendProgressMessage(0, name, 0, size);
#endif

    reading_size = 0L;
    indicator_count = 0;
    next_msg_size = 0;

    if (quiet)
        return;

#ifdef NEED_INCREMENTAL_INDICATOR
    switch (quiet_mode) {
    case 0:
        m = MAX_INDICATOR_COUNT - (int)strlen(name);
        if (m < 1)      /* Bug Fixed by N.Watazaki */
            m = 3;      /* (^_^) */
        carriage_return();
        printf("%s\t- %s :  ", name, msg);

        indicator_threshold =
            (long)(ALIGN(size, m*def_indicator_threshold) * def_indicator_threshold);

        if (indicator_threshold)
            i = (long)ALIGN(size, indicator_threshold);
        else
            i = 0;

        while (i--)
            putchar('.');
        indicator_count = 0;
        carriage_return();
        printf("%s\t- %s :  ", name, msg);
        break;
    case 1:
        carriage_return();
        printf("%s :", name);
        break;
    }
#else
    printf("%s\t- ", name);
#endif
    fflush(stdout);
}

void
put_indicator(long int count)
{
#ifdef LHA_LIBRARY
    if (Lha_CheckAbort()) {
        fatal_error("User cancelled.");
    }
#endif

    reading_size += count;

#ifdef NEED_INCREMENTAL_INDICATOR
    if (!quiet && indicator_threshold) {
        while (reading_size > indicator_count) {
            putchar('o');
            fflush(stdout);
            indicator_count += indicator_threshold;
        }
    }
#endif

#ifdef LHA_LIBRARY
    if (reading_size >= next_msg_size || reading_size < next_msg_size - 1024*1024) {
        if (Lha_SendProgressMessage(1, NULL, reading_size, 0)) {
            fatal_error("User cancelled.");
        }
        // 次の通知は 32KB 先、または indicator_threshold 先とする
        long interval = (indicator_threshold > 0) ? indicator_threshold : 32768;
        if (interval < 4096) interval = 4096; // 最低 4KB
        next_msg_size = reading_size + interval;
    }
#endif
}

void
finish_indicator2(char *name, char *msg, int pcnt)
{
#ifdef LHA_LIBRARY
    if (Lha_SendProgressMessage(2, name, reading_size, reading_size)) {
        fatal_error("User cancelled.");
    }
#endif

    if (quiet)
        return;

    if (pcnt > 100)
        pcnt = 100; /* (^_^) */
#ifdef NEED_INCREMENTAL_INDICATOR
    carriage_return();
    printf("%s\t- %s(%d%%)\n", name,  msg, pcnt);
#else
    printf("%s\n", msg);
#endif
    fflush(stdout);
}

void
finish_indicator(char *name, char *msg)
{
#ifdef LHA_LIBRARY
    if (Lha_SendProgressMessage(1, name, reading_size, reading_size)) {
        fatal_error("User cancelled.");
    }
#endif

    if (quiet)
        return;

#ifdef NEED_INCREMENTAL_INDICATOR
    carriage_return();
    printf("%s\t- %s\n", name, msg);
#else
    printf("%s\n", msg);
#endif
    fflush(stdout);
}
