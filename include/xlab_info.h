#ifndef XLAB_INFO_H
#define XLAB_INFO_H

#define __XLAB__            1
#define __XLAB_MINOR__      0
#define __XLAB_PATCHLEVEL__ 1
/* 兼容 main.c 中的旧宏名 */
#define XLAB __XLAB__
#define XLAB_MINOR __XLAB_MINOR__
#define XLAB_PATCHLEVEL __XLAB_PATCHLEVEL__
#define XLAB_VERSION (__XLAB__ * 10000 \
                                __XLAB_MINOR__ * 100 \
                                __XLAB_PATCHLEVEL__)
#define VERSION "1.0.1"
#define XLAB_PATH_CONF "/home/sin"

#endif
