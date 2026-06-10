#if 1
#ifndef XLAB_UTILS_H
#define XLAB_UTILS_H

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "xlab_request.h"
#include "xlab_memory.h"
#include "xlab_list.h"

#define XLAB_GMT_CACHES 10

struct xlab_gmt_cache {
    time_t time;
    char text[32];
    unsigned long long hits;
};
void xlab_print(int type, const char *format, ...);

#endif
#endif