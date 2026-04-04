#ifndef __F18_EPOLL_H__
#define __F18_EPOLL_H__

#include "f18_types.h"

#include "f18_channel.h"

extern int f18_epoll_init(void);
extern int f18_epoll_terminate(void);
extern int f18_epoll_add(int fd);
extern int f18_epoll_del(int fd);
extern int f18_epoll_select(int fd, chan_t* chan, f18_chan_mode_t mode);
extern void* f18_epoll_main(void * arg);

#endif
