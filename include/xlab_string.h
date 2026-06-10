#ifndef XLAB_STR_H
#define XLAB_STR_H

#include "xlab_memory.h"
#include "xlab_list.h"
#define XLAB_STR_SENSITIVE 0
#define XLAB_STR_INSENSITIVE 1

struct xlab_string_line
{
    char *val;
    int len;

    struct xlab_list _head;
};

int xlab_string_char_search(const char *string, int c, int len);
int xlab_string_char_search_r(const char *string, int c, int len);
int xlab_string_search(const char *haystack, const char *needle, int sensitive);
int xlab_string_search_n(const char *haystack, const char *needle, int sensitive, int len);
char *xlab_string_remove_space(char *buf);
char *xlab_string_casestr(char *heystack, char *needle);
char *xlab_string_dup(const char *s);
struct xlab_list *xlab_string_split_line(const char *line);
void xlab_string_split_free(struct xlab_list *list);
int xlab_string_trim(char **str);
char *xlab_string_build(char **buffer, unsigned long *len,
                      const char *format, ...);
char *xlab_string_copy_substr(const char *string, int pos_init, int pos_end);

#endif
