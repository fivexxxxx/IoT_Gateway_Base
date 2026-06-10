#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include "xlab_macros.h"
#include "xlab_request.h"
#include "xlab_utils.h"
#include "xlab_memory.h"
#include "xlab_string.h"
#include <stdio.h>

/*
 * Base function for search routines, it accept modifiers to enable/disable
 * the case sensitive feature and also allow to specify a haystack len
 * Get position of a substring.
 */
static int _xlab_string_search(const char *string, const char *search, int sensitive, int len)
{
    int i = 0;
    char *p = NULL, *q = NULL;
    char *s = NULL;

    /* Fast path */
    if (len <= 0) {
        switch(sensitive) {
        case XLAB_STR_SENSITIVE:
            p = strstr(string, search);
            break;
        case XLAB_STR_INSENSITIVE:
            p = strcasestr(string, search);
            break;
        }

        if (p) {
            return (int)(p - string);
        }
        else {
            return -1;
        }
    }

    p = (char *) string;
    do {
        q = p;
        s = (char *) search;
        if (sensitive == XLAB_STR_SENSITIVE) {
            while (*s && (*s == *q)) {
                q++, s++;
            }
        }
        else if (sensitive == XLAB_STR_INSENSITIVE) {
            while (*s && (toupper(*q) == toupper(*s))) {
                q++, s++;
            }
        }

        /* match */
        if (*s == 0) {
            return (int)(p - string);
        }

        i++;
        if (i >= len) {
            break;
        }
    } while (*p++);

    return -1;
}

/* Lookup char into string, return position */
int xlab_string_char_search(const char *string, int c, int len)
{
    char *p;

    if (len < 0) {
        len = (int)strlen(string);
    }

    p = memchr(string, c, (size_t)len);
    if (p) {
        return (int)(p - string);
    }

    return -1;
}

/* Find char into string searching in reverse order, returns position */
int xlab_string_char_search_r(const char *string, int c, int len)
{
    char *p;

    if (len <= 0) {
        len = (int)strlen(string);
    }

    p = memrchr(string, c, (size_t)len);
    if (p) {
        return (int)(p - string);
    }

    return -1;
}

int xlab_string_search(const char *haystack, const char *needle, int sensitive)
{
    return _xlab_string_search(haystack, needle, sensitive, -1);
}

int xlab_string_search_n(const char *haystack, const char *needle, int sensitive, int len)
{
    return _xlab_string_search(haystack, needle, sensitive, len);
}

char *xlab_string_casestr(char *heystack, char *needle)
{
    if (!heystack || !needle) {
        return NULL;
    }

    return strcasestr(heystack, needle);
}

char *xlab_string_dup(const char *s)
{
    if (!s)
        return NULL;

    return strdup(s);
}

struct xlab_list *xlab_string_split_line(const char *line)
{
    unsigned int i = 0, len, val_len;
    int end;
    char *val;
    struct xlab_list *list;
    struct xlab_string_line *new;

    if (!line) {
        return NULL;
    }

    list = xlab_mem_malloc(sizeof(struct xlab_list));
    xlab_list_init(list);

    len = (unsigned int)strlen(line);

    while (i < len) {
        end = xlab_string_char_search(line + i, ' ', (int)(len - i));

        if (end >= 0 && (end + (int)i) < (int)len) {
            end += (int)i;

            if (i == (unsigned int)end) {
                i++;
                continue;
            }

            val = xlab_string_copy_substr(line, (int)i, (int)end);
            val_len = (unsigned int)(end - (int)i);
        }
        else {
            val = xlab_string_copy_substr(line, (int)i, (int)len);
            val_len = (len - i);
            end = (int)len;

        }

        /* Alloc node */
        new = xlab_mem_malloc(sizeof(struct xlab_string_line));
        new->val = val;
        new->len = (int)val_len;

        xlab_list_add(&new->_head, list);
        i = (unsigned int)end + 1;
    }

    return list;
}

void xlab_string_split_free(struct xlab_list *list)
{
    struct xlab_list *head, *tmp;
    struct xlab_string_line *entry;

    xlab_list_foreach_safe(head, tmp, list) {
        entry = xlab_list_entry(head, struct xlab_string_line, _head);
        xlab_list_del(&entry->_head);
        xlab_mem_free(entry->val);
        xlab_mem_free(entry);
    }

    xlab_mem_free(list);
}

char *xlab_string_build(char **buffer, unsigned long *len,
                      const char *format, ...)
{
    va_list ap;
    int length;
    char *ptr;
    static size_t _mem_alloc = 64;
    size_t alloc = 0;

    /* *buffer *must* be an empty/NULL buffer */
    xlab_bug(*buffer);//起到优化作用，减少JMP的动作
    *buffer = (char *) xlab_mem_malloc(_mem_alloc);

    if (!*buffer) {
        return NULL;
    }
    alloc = _mem_alloc;

    va_start(ap, format);
    length = vsnprintf(*buffer, alloc, format, ap);
    va_end(ap);

    if ((size_t)length >= alloc) {
        ptr = realloc(*buffer, (size_t)length + 1);
        if (!ptr) {
            return NULL;
        }
        *buffer = ptr;
        alloc = (size_t)length + 1;

        va_start(ap, format);
        length = vsnprintf(*buffer, alloc, format, ap);
        va_end(ap);
    }

    if (length < 0) {
        return NULL;
    }

    ptr = *buffer;
    ptr[length] = '\0';
    *len = (size_t)length;

    return *buffer;
}

int xlab_string_trim(char **str)
{
    unsigned int i;
    unsigned int len;
    char *left = 0, *right = 0;
    char *buf;

    buf = *str;
    if (!buf) {
        return -1;
    }

    len = (unsigned int)strlen(buf);
    left = buf;

    if(len == 0) {
        return 0;
    }

    /* left spaces */
    while (left) {
        if (isspace(*left)) {
            left++;
        }
        else {
            break;
        }
    }

    right = buf + (len - 1);
    /* Validate right v/s left */
    if (right < left) {
        buf[0] = '\0';
        return -1;
    }

    /* Move back */
    while (right != buf){
        if (isspace(*right)) {
            right--;
        }
        else {
            break;
        }
    }

    len = (unsigned int)(right - left) + 1;
    for(i=0; i<len; i++){
        buf[i] = (char) left[i];
    }
    buf[i] = '\0';

    return 0;
}

int xlab_string_itop(int value, xlab_pointer *p)
{
    char aux;
    char *wstr = p->data;
    char *begin, *end;
    unsigned int uvalue = (unsigned int)((value < 0) ? -value : value);

    do *wstr++ = (char)(48 + (uvalue % 10)); while(uvalue /= 10);
    if (value < 0) *wstr++ = '-';
    *wstr='\0';

    begin = p->data;
    end = wstr - 1;

    while (end > begin) {
        aux = *end, *end-- = *begin, *begin++ = aux;
    }

    *wstr++ = '\r';
    *wstr++ = '\n';
    *wstr++ = '\0';

    p->len = (size_t)(wstr - p->data - 1);
    return 0;
}

/* Return a buffer with a new string from string */
char *xlab_string_copy_substr(const char *string, int pos_init, int pos_end)
{
    unsigned int size, bytes;
    char *buffer = 0;

    size = (unsigned int) (pos_end - pos_init) + 1;
    if (size <= 2)
        size = 4;

    buffer = xlab_mem_malloc(size);

    if (!buffer) {
        return NULL;
    }

    if (pos_init > pos_end) {
        xlab_mem_free(buffer);
        return NULL;
    }

    bytes =(unsigned int)( pos_end - pos_init);
    memcpy(buffer, string + pos_init, bytes);
    buffer[bytes] = '\0';

    return (char *) buffer;
}

