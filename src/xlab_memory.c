#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "xlab_config.h"
#include "xlab_memory.h"
#include "xlab_request.h"

//inline ALLOCSZ_ATTR(1)
void *xlab_mem_malloc(const size_t size)
{
    void *aux = malloc(size);

    if (!aux && size) {
        perror("malloc");
        return NULL;
    }

    return aux;
}

//inline ALLOCSZ_ATTR(1)
void *xlab_mem_malloc_z(const size_t size)
{
    void *buf = calloc(1, size);
    if (!buf)
        return NULL;

    return buf;
}

//inline ALLOCSZ_ATTR(2)
void *xlab_mem_realloc(void *ptr, const size_t size)
{
    void *aux = realloc(ptr, size);

    if (!aux && size) {
        perror("realloc");
        return NULL;
    }

    return aux;
}

void xlab_mem_free(void *ptr)
{
    free(ptr);
}

void xlab_pointer_free(xlab_pointer* p)
{
    xlab_mem_free(p->data);
    p->len = 0;
}


void xlab_pointer_set(xlab_pointer* p, char* data)
{
    p->data = data;
    p->len = strlen(data);
}
