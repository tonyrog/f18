/*
 * Epoll thread used to wait and signal for async events
 */

#include <sys/epoll.h>

#include "f18_channel.h"

#define MAX_EVENTS (18+18+6+6)

static int f18_efd = -1;
struct epoll_event f18_epoll_array[MAX_EVENTS];

int f18_epoll_init()
{
    f18_efd = epoll_create1(0);
    return f18_efd;
}

int f18_epoll_add(int fd)
{
    struct epoll_event epe;
    epe.events = 0;
    epe.data.u32 = 0;
    return epoll_ctl(f18_efd, EPOLL_CTL_ADD, fd, &epe);
}

int f18_epoll_del(int fd)
{
    return epoll_ctl(f18_efd, EPOLL_CTL_ADD, fd, NULL);
}

int f18_epoll_select(int fd, chan_t* chan, f18_chan_mode_t mode)
{
    struct epoll_event epe;

    epe.events = 0;
    if (mode & F18_CHAN_READ)
	epe.events |= EPOLLIN;
    if (mode & F18_CHAN_WRITE)
	epe.events |= EPOLLOUT;
    epe.events |= EPOLLONESHOT;
    epe.data.ptr = chan;
    // clear io before selecting
    pthread_mutex_lock(&chan->lock);
    chan->io = 0;
    pthread_mutex_unlock(&chan->lock);    
    return epoll_ctl(f18_efd, EPOLL_CTL_MOD, fd, &epe);
}

void* f18_epoll_main(void * arg)
{
    (void) arg;
    PRINTF("starting f18_epoll_main\n");
    while(1) {
	int n;
	if ((n = epoll_wait(f18_efd, f18_epoll_array, MAX_EVENTS, -1)) > 0) {
	    int i;
	    PRINTF("f18_epoll_main: found %d events\n", n);
	    for (i = 0; i < n; i++) {
		chan_t* chan = (chan_t*) f18_epoll_array[i].data.ptr;
		f18_chan_mode_t rw = F18_CHAN_NONE;
		if (f18_epoll_array[i].events & EPOLLIN)
		    rw |= F18_CHAN_READ;
		if (f18_epoll_array[i].events & EPOLLOUT)
		    rw |= F18_CHAN_WRITE;
		f18_chan_wakeup(chan, rw);
	    }
	}
    }
}
