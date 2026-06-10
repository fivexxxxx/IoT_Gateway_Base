#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>

#include "xlab_memory.h"
#include "xlab_utils.h"
#include "xlab_string.h"
#include "xlab_config.h"
#include "xlab_socket.h"
#include "xlab_macros.h"


#if 0
/* Convert hexadecimal to int */
int xlab_utils_hex2int(char *hex, int len)
{
    int i = 0;
    int res = 0;
    char c;

    while ((c = *hex++) && i < len) {
        res *= 0x10;

        if (c >= 'a' && c <= 'f') {
            res += (c - 0x57);
        }
        else if (c >= 'A' && c <= 'F') {
            res += (c - 0x37);
        }
        else if (c >= '0' && c <= '9') {
            res += (c - 0x30);
        }
        else {
            return -1;
        }
        i++;
    }

    if (res < 0) {
        return -1;
    }

    return res;
}
#endif

void xlab_print(int type, const char *format, ...)
{
    time_t now;
    struct tm *current;

    char *header_color = NULL;
    char *header_title = NULL;
    char *bold_color = ANSI_BOLD;
    char *reset_color = ANSI_RESET;
    char *white_color = ANSI_WHITE;
    va_list args;

    va_start(args, format);

    switch (type) {
    case XLAB_INFO:
        header_title = "Info ";
        header_color = ANSI_GREEN;
        break;
    case XLAB_ERR:
        header_title = "Error";
        header_color = ANSI_RED;
        break;
    case XLAB_WARN:
        header_title = "Warning";
        header_color = ANSI_YELLOW;
        break;
    }

    /* Only print colors to a terminal */
    if (!isatty(STDOUT_FILENO)) {
        header_color = "";
        bold_color = "";
        reset_color = "";
        white_color = "";
    }

    now = time(NULL);
    struct tm result;
    current = localtime_r(&now, &result);
    printf("%s[%s%i/%02i/%02i %02i:%02i:%02i%s]%s ",
           bold_color, reset_color,
           current->tm_year + 1900,
           current->tm_mon + 1,
           current->tm_mday,
           current->tm_hour,
           current->tm_min,
           current->tm_sec,
           bold_color, reset_color);

    printf("%s[%s%7s%s]%s ",
           bold_color, header_color, header_title, white_color, reset_color);

    vprintf(format, args);
    va_end(args);
    printf("%s\n", reset_color);
    fflush(stdout);
}
