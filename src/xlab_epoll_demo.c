#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include "xlab_socket.h"
#include "xlab_server.h"
#include "xlab_macros.h"
#include "xlab_info.h"
#include "xlab_scheduler.h"
#include "xlab_request.h"
#include "xlab_config.h"
#include "xlab_async.h"        /* 异步任务层 */
#include "xlab_business.h"
#include "xlab_trans.h" 

#if 1
#define CC  "GCC "
#if defined( DATE ) && defined( TIME )
static const char XLAB_BUILT[] __attribute__((unused)) = DATE "  "  TIME;
#else
static const char XLAB_BUILT[] __attribute__((unused)) = "Unknown ";
#endif
#endif

static void xlab_thread_keys_init(void)
{
    pthread_key_create(&worker_sched_node_k, NULL);
    pthread_key_create(&request_list_k, NULL);
    pthread_key_create(&epoll_fd_k, NULL);
}

static void xlab_details(void)
{
    xlab_info("Process ID is %i", getpid());
  
    xlab_info("Server socket listening on " ANSI_CYAN "%s:" ANSI_RESET " : " ANSI_RED  "%i" ANSI_RESET ,
        config->listen_addr ? config->listen_addr : "0.0.0.0",
        config->serverport);

    xlab_info("%i threads, %i client connections per thread, total %i",
        config->workers, config->worker_capacity,
        config->workers * config->worker_capacity);
}
static void xlab_version(void)
{
#if 0
    xlab_info("Epoll Server Xlab %i.%i.%i",
        XLAB, XLAB_MINOR, XLAB_PATCHLEVEL);
    xlab_info("Built : %s (GCC %i.%i.%i)",
        XLAB_BUILT, __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
    xlab_info("Epoll Server starting initialization...");
}

/* 主函数 */
int main(int argc, char** argv)
{
    char* file_config = NULL;

    /* 初始化配置结构 */
    config = xlab_mem_malloc_z(sizeof(struct server_config));
    if (!file_config)
        config->file_config = XLAB_PATH_CONF;
    else
        config->file_config = file_config;

    xlab_version();

    /* 1. 加载配置文件 */
    xlab_config_start_configure();
    /* 5. 初始化调度器 (创建 Worker 线程) */
    xlab_sched_init();
    /* 2. 初始化异步任务框架 (创建 eventfd + 后台硬件线程) */
    if (xlab_async_init() < 0) {
        xlab_err("Failed to init async framework");
        return -1;
    }

    /* 3. 初始化传输驱动 (UDP/UART) */
    xlab_trans_udp_init(config->serverport);          /* UDP 复用配置端口 */
    /* xlab_trans_uart_init("/dev/ttyS1", B115200); */ /* UART 按需启用 */

    /* 4. 注册业务命令 */
    xlab_business_init();    

    /* 6. 创建监听 Socket */
    config->server_fd = xlab_socket_server(config->serverport, config->listen_addr);
   
    xlab_thread_keys_init();
    xlab_details();

    /* 7. 启动 Worker 线程 */
    xlab_server_launch_workers();

    /* 8. 等待 Worker 就绪 */
    while (1) {
        int i, ready = 0;
        pthread_mutex_lock(&mutex_worker_init);
        for (i = 0; i < config->workers; i++) {
            if (sched_list[i].initialized)
                ready++;
        }
        pthread_mutex_unlock(&mutex_worker_init);
        if (ready == config->workers) break;
        usleep(10000);
    }

    /* 9. 主循环: accept + 派发 */
    xlab_server_loop(config->server_fd);

    /* 清理 (实际嵌入式场景通常不执行) */
    xlab_mem_free(sched_list);
    xlab_mem_free(config);
    return 0;
}