#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <dirent.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <ctype.h>

#include "xlab_config.h"
#include "xlab_string.h"
#include "xlab_utils.h"
#include "xlab_info.h"
#include "xlab_memory.h"
#include "xlab_server.h"
#include "xlab_macros.h"

/* Print a specific error */
static int xlab_config_print_error_msg(char *variable, char *path)
{
    xlab_err("Error in %s variable under %s, has an invalid value",
           variable, path);
    return -1;
}

/* Raise a configuration schema error */
void xlab_config_error(const char *path, int line, const char *msg)
{
    xlab_err("File %s", path);
    xlab_err("Error in line %i: %s", line, msg);
    
}

/* Returns a configuration section by [section name] */
struct xlab_config_section *xlab_config_section_get(struct xlab_config *conf,
	const char *section_name)
{
	struct xlab_config_section *section;
	struct xlab_list *head;

	xlab_list_foreach(head, &conf->sections) {
		section = xlab_list_entry(head, struct xlab_config_section, _head);
		if (strcasecmp(section->name, section_name) == 0) {
			return section;
		}
	}

	return NULL;
}

/* Register a new section into the configuration struct */
struct xlab_config_section *xlab_config_section_add(struct xlab_config *conf,
                                                char *section_name)
{
    struct xlab_config_section *new;

    /* Alloc section node */
    new = xlab_mem_malloc(sizeof(struct xlab_config_section));
    new->name = xlab_string_dup(section_name);
    xlab_list_init(&new->entries);
    xlab_list_add(&new->_head, &conf->sections);

    return new;
}

/* Register a key/value entry in the last section available of the struct */
void xlab_config_entry_add(struct xlab_config *conf,
                         const char *key, const char *val)
{
    struct xlab_config_section *section;
    struct xlab_config_entry *new;
    struct xlab_list *head = &conf->sections;;

    if (xlab_list_is_empty(&conf->sections) == 0) {
        xlab_err("Error: there are not sections available!");
    }

    /* Last section */
    section = xlab_list_entry_last(head, struct xlab_config_section, _head);

    /* Alloc new entry */
    new = xlab_mem_malloc(sizeof(struct xlab_config_entry));
    new->key = xlab_string_dup(key);
    new->val = xlab_string_dup(val);

    xlab_list_add(&new->_head, &section->entries);
}

struct xlab_config *xlab_config_create(const char *path)
{
    size_t i;
    size_t len;
    size_t line = 0;
    int indent_len = -1;
    size_t n_keys = 0;
    char buf[255];
    char *section = NULL;
    char *indent = NULL;
    char *key, *val;
    struct xlab_config *conf = NULL;
    struct xlab_config_section *current = NULL;
    FILE *f;

    /* Open configuration file */
    if ((f = fopen(path, "r")) == NULL) {
        xlab_warn("Config: I cannot open %s file", path);
        return NULL;
    }

    /* Alloc configuration node */
    conf = xlab_mem_malloc_z(sizeof(struct xlab_config));
    conf->created = (int)time(NULL);
    conf->file = xlab_string_dup(path);
    xlab_list_init(&conf->sections);

    /* looking for configuration directives */
    while (fgets(buf, 255, f)) {
        len = strlen(buf);
        if (buf[len - 1] == '\n') {
            buf[--len] = 0;
            if (len && buf[len - 1] == '\r') {
                buf[--len] = 0;
            }
        }

        /* Line number */
        line++;

        if (!buf[0]) {
            continue;
        }

        /* Skip commented lines */
        if (buf[0] == '#') {
            continue;
        }

        /* Section definition */
        if (buf[0] == '[') {
            int end = -1;
            end = xlab_string_char_search(buf, ']', (int)len);
            if (end > 0) {
                /*
                 * Before to add a new section, lets check the previous
                 * one have at least one key set
                 */
                if (current && n_keys == 0) {
                    xlab_config_error(path, (int)line, "Previous section did not have keys");
                }

                /* Create new section */
                section = xlab_string_copy_substr(buf, 1, end);
                current = xlab_config_section_add(conf, section);
                xlab_mem_free(section);
                n_keys = 0;
                continue;
            }
            else {
                xlab_config_error(path, (int)line, "Bad header definition");
            }
        }

        /* No separator defined */
        if (!indent) {
            i = 0;

            do { i++; } while (i < len && isblank(buf[i]));

            indent = xlab_string_copy_substr(buf, 0, (int)i);
            indent_len = (int)strlen(indent);

            /* Blank indented line */
            if (i == len) {
                continue;
            }
        }


        /* Validate indentation level */
        if (strncmp(buf, indent, (size_t)indent_len) != 0 || isblank(buf[indent_len]) != 0) {
            xlab_config_error(path, (int)line, "Invalid indentation level");
        }

        if (buf[indent_len] == '#' || (size_t)indent_len == len) {
            continue;
        }

        /* Get key and val */
        i = (size_t)xlab_string_char_search(buf + indent_len, ' ', ((int)len - indent_len));
        key = xlab_string_copy_substr(buf + indent_len, 0, (int)i);
        val = xlab_string_copy_substr(buf + indent_len + i, 1, ((int)len - indent_len));

        if (!key || !val || i ==(size_t) 0) {
            xlab_config_error(path, (int)line, "Each key must have a value");
        }

        /* Trim strings */
        xlab_string_trim(&key);
        xlab_string_trim(&val);

        /* Register entry: key and val are copied as duplicated */
        xlab_config_entry_add(conf, key, val);

        /* Free temporal key and val */
        xlab_mem_free(key);
        xlab_mem_free(val);

        n_keys++;
    }

    if (n_keys == 0) {
        xlab_config_error(path, (int)line, "Section do not have keys");
    }
 
    if (indent) xlab_mem_free(indent);

    fclose(f);
    return conf;
}

void xlab_config_free(struct xlab_config *conf)
{
    struct xlab_config_section *section;
    struct xlab_list *head, *tmp;

    /* Free sections */
    xlab_list_foreach_safe(head, tmp, &conf->sections) {
        section = xlab_list_entry(head, struct xlab_config_section, _head);
        xlab_list_del(&section->_head);

        /* Free section entries */
        xlab_config_free_entries(section);

        /* Free section node */
        xlab_mem_free(section->name);
        xlab_mem_free(section);
    }
    xlab_mem_free(conf->file);
    xlab_mem_free(conf);
}

void xlab_config_free_entries(struct xlab_config_section *section)
{
    struct xlab_config_entry *entry;
    struct xlab_list *head, *tmp;

    xlab_list_foreach_safe(head, tmp, &section->entries) {
        entry = xlab_list_entry(head, struct xlab_config_entry, _head);
        xlab_list_del(&entry->_head);

        /* Free memory assigned */
        xlab_mem_free(entry->key);
        xlab_mem_free(entry->val);
        xlab_mem_free(entry);
    }
}

void *xlab_config_section_getval(struct xlab_config_section *section, char *key, int mode)
{
    int on, off;
    struct xlab_config_entry *entry;
    struct xlab_list *head;

    xlab_list_foreach(head, &section->entries) {
        entry = xlab_list_entry(head, struct xlab_config_entry, _head);

        if (strcasecmp(entry->key, key) == 0) {
            switch (mode) {
            case XLAB_CONFIG_VAL_STR:
                return (void *) xlab_string_dup(entry->val);
            case XLAB_CONFIG_VAL_NUM:
                return (void *) strtol(entry->val, (char **) NULL, 10);
            case XLAB_CONFIG_VAL_BOOL:
                on = strcasecmp(entry->val, VALUE_ON);
                off = strcasecmp(entry->val, VALUE_OFF);

                if (on != 0 && off != 0) {
                    return (void *) -1;
                }
                else if (on >= 0) {
                    return (void *) XLAB_TRUE;
                }
                else {
                    return (void *) XLAB_FALSE;
                }
            case XLAB_CONFIG_VAL_LIST:
                return (void *)xlab_string_split_line(entry->val);
            }
        }
    }
    return NULL;
}

/* Read configuration files */
static int xlab_config_read_files(char *path_conf, char *file_conf)
{
    unsigned long len;
    char *tmp = NULL;
    struct stat checkdir;
    struct xlab_config *cnf;
    struct xlab_config_section *section;

    config->serverconf = xlab_string_dup(path_conf);
    config->workers = XLAB_WORKERS_DEFAULT;

    if (stat(config->serverconf, &checkdir) == -1) {
        xlab_err("ERROR: Cannot find/open '%s'", config->serverconf);
        return 0;
    }

    xlab_string_build(&tmp, &len, "%s/%s", path_conf, file_conf);
    cnf = xlab_config_create(tmp);
    if (!cnf) {
        xlab_err("Cannot read 'raifull.conf'");
        return 0;
    }
    section = xlab_config_section_get(cnf, "SERVER");// 判断raifull.conf文件是否有“SERVER”

    if (!section) {
        xlab_err("ERROR: No 'SERVER' section defined");
    }

    /* Map source configuration */
    config->config = cnf;

    /* Listen */
    config->listen_addr = xlab_config_section_getval(section, "Listen",
                                                   XLAB_CONFIG_VAL_STR);
    if (!config->listen_addr) {
        config->listen_addr = xlab_string_dup(XLAB_DEFAULT_LISTEN_ADDR);
    }

    /* Connection port */
    config->serverport = (int)(intptr_t)xlab_config_section_getval(section,
                                                           "Port",
                                                           XLAB_CONFIG_VAL_NUM);
    if (config->serverport <= 1 || config->serverport >= 65535) {
        if (xlab_config_print_error_msg("Port", tmp) < 0) {
            config->serverport = XLAB_DEFAULT_SERVER_PORT;  /* 降级使用默认值 */
        }
    }

    /* Number of thread workers */
    config->workers = (short)(intptr_t)xlab_config_section_getval(section,
                                                     "Workers",
                                                     XLAB_CONFIG_VAL_NUM);
    if (config->workers < 1) {
        if (xlab_config_print_error_msg("Workers", tmp) < 0) {
            config->workers = XLAB_WORKERS_DEFAULT;  /* 降级使用默认值 */
        }
    }
    /* Get each worker clients capacity based on FDs system limits */
    config->worker_capacity = xlab_server_worker_capacity(config->workers);

    /* Set max server load */
    config->max_load = (config->worker_capacity * config->workers);



    /* Max Request Size */
    config->max_request_size = (int)(intptr_t)xlab_config_section_getval(section,
                                                              "MaxRequestSize",
                                                              XLAB_CONFIG_VAL_NUM);
    if (config->max_request_size <= 0) {
        if (xlab_config_print_error_msg("MaxRequestSize", tmp) < 0) {
            config->max_request_size = XLAB_REQUEST_CHUNK * 8;  /* 默认 32KB */
        }
    }
    else {
        config->max_request_size *= 1024;
    }
   return 0;
}


void xlab_config_set_init_values(void)
{
    config->listen_addr = XLAB_DEFAULT_LISTEN_ADDR;
    config->serverport = XLAB_DEFAULT_SERVER_PORT;
    config->max_request_size = XLAB_REQUEST_CHUNK * 8;    
}

int xlab_config_start_configure(void)
{
    xlab_config_set_init_values();
    int ret = xlab_config_read_files(config->file_config, M_DEFAULT_CONFIG_FILE);
    if (ret < 0) {
        xlab_warn("Config load partial failed, running with fallback defaults.");
    }
    return 0;  /* 始终返回 0 保证 main 兼容 */
}