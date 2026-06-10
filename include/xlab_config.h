#include "xlab_memory.h"
#include "xlab_list.h"

#ifndef XLAB_CONFIG_H
#define XLAB_CONFIG_H

#include <unistd.h>
#include <sys/types.h>

#if 0
#ifndef O_NOATIME
#define O_NOATIME       01000000
#endif
#endif
#define M_DEFAULT_CONFIG_FILE	"raifull.conf"
#define M_DEFAULT_HOSTS_FILE	"default"
#define XLAB_DEFAULT_LISTEN_ADDR  "0.0.0.0"
#define XLAB_DEFAULT_SERVER_PORT   2001 
#define XLAB_WORKERS_DEFAULT 1

#define VALUE_ON "on"
#define VALUE_OFF "off"

#define XLAB_CONFIG_VAL_STR 0
#define XLAB_CONFIG_VAL_NUM 1
#define XLAB_CONFIG_VAL_BOOL 2
#define XLAB_CONFIG_VAL_LIST 3

/* Indented configuration */
struct xlab_config
{
    int created;
    char *file;

    /* list of sections */
    struct xlab_list sections;
};

struct xlab_config_section
{
    char *name;

    struct xlab_list entries;
    struct xlab_list _head;
};

struct xlab_config_entry
{
    char *key;
    char *val;

    struct xlab_list _head;
};

/* Base struct of server */
struct server_config
{
    int server_fd;              /* server socket file descriptor */
    int worker_capacity;        /* how many clients per thread... */
    int max_load;               /* max number of clients (worker_capacity * workers) */
    short int workers;          /* number of worker threads */
    char *serverconf;           /* path to configuration files */
    char *listen_addr;
  
    xlab_pointer server_software;
    char *file_config;
    int serverport;             /* port */    
    int thread_counter; /* counter of threads working */
    int max_request_size;
    struct xlab_config *config;
};

struct server_config *config;

/* Functions */
int xlab_config_start_configure(void);
void xlab_config_set_init_values(void);

/* config helpers */
void xlab_config_error(const char *path, int line, const char *msg);
struct xlab_config *xlab_config_create(const char *path);
struct xlab_config_section *xlab_config_section_get(struct xlab_config *conf,
                                                const char *section_name);
struct xlab_config_section *xlab_config_section_add(struct xlab_config *conf,
                                                char *section_name);
void *xlab_config_section_getval(struct xlab_config_section *section, char *key, int mode);
void xlab_config_free(struct xlab_config *cnf);
void xlab_config_free_entries(struct xlab_config_section *section);
#endif
