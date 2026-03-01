// various pty implemenations

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>


#if defined(__APPLE__)
#include <IOKit/serial/ioss.h>
#include <util.h>
#define HAVE_PTY
#elif defined(__FreeBSD__)
#include <libutil.h>
#define HAVE_PTY
#else
#include <pty.h>
#define HAVE_PTY
#endif

#define MAX_PTY_NAME 256

#define DEBUGF(fmt, args...) fprintf(stderr, (fmt), args)

#if defined(__APPLE__)
static int local_ptsname_r(int fd, char* buf, size_t maxlen)
{
    char devname[128];
    struct stat sbuf;    
    size_t n;
    if (ioctl(fd, TIOCPTYGNAME, devname) < 0) {
	DEBUGF("TIOCPTYGNAME failed : %s", strerror(errno));
	return -1;
    }
    if (stat(devname, &sbuf) < 0) {
	DEBUGF("stat %s failed : %s", devname, strerror(errno));	
	return -1;
    }
    if ((n=strlen(devname)) >= maxlen) {
	errno = ERANGE;
	return -1;
    }
    memcpy(buf, devname, n);
    buf[n] = '\0';
    return 0;
}
#elif defined(__linux__)
#define local_ptsname_r ptsname_r
#elif defined(HAVE_PTY)
static int local_ptsname_r(int fd, char* buf, size_t maxlen)
{
    char* ptr;
    size_t n;

    // FIXME lock!
    if ((ptr = ptrname(fd)) == NULL) {
	DEBUGF("ptsname failed : %s", strerror(errno));
	return -1;
    }
    if ((n=strlen(ptr)) >= maxlen) {
	errno = ERANGE;
	return -1;
    }
    memcpy(buf, ptr, n);
    buf[n] = '\0';
    return 0;
}
#endif

// pseudo terminal devices to try
// On MacOS X: /dev/pty[p-w][0-9a-f]
// On *BSD: /dev/pty[p-sP-S][0-9a-v]
// On AIX: /dev/ptyp[0-9a-f]
// On HP-UX: /dev/pty[p-r][0-9a-f]
// On OSF/1: /dev/pty[p-q][0-9a-f]
// On Solaris: /dev/pty[p-r][0-9a-f]
#if defined(__APPLE__)
int local_openpt(int oflag)
{
//    (void) oflag;
    char devname[32];
    const char* a = "pqrstuvw";
    const char* b = "0123456789abcdef";
    char* prefix = "/dev/pty";
    int i,j,fd;

//    return getpt();
//    return open("/dev/ptmx", oflag);
//    return posix_openpt(oflag);

    for (i = 0; a[i]; i++) {
	for (j = 0; b[j]; j++) {
	    sprintf(devname, "%s%c%c", prefix,a[i],b[j]);
	    if ((fd = open(devname, oflag)) >= 0) {
		// fixme: check that the device is available
		return fd;
	    }
	}
    }
    errno = ENOENT;
    return -1;
}
#elif defined(__linux__)
int local_openpt(int oflag)
{
    return posix_openpt(oflag);
}
#elif defined(HAVE_PTY)
int local_openpt(int oflag)
{
    return posix_openpt(oflag);
}
#endif

int set_exclusive(int fd, int on)
{
#ifdef TIOCEXCL
    if (on) {
	if (ioctl(fd, TIOCEXCL, NULL) < 0) {
	    DEBUGF("set exclusive mode failed: %s\r\n", strerror(errno));
	    return -1;
	}
	else {
	    DEBUGF("set exclusive mode ok%s\r\n", "");
	}
    }
#endif
#ifdef TIOCNXCL
    if (!on) {
	if (ioctl(fd, TIOCNXCL, NULL) < 0) {
	    DEBUGF("clear exclusive mode failed: %s\r\n", strerror(errno));
	    return -1;
	}
	else {
	    DEBUGF("clear exclusive mode ok%s\r\n", "");
	}
    }
#endif
    return 0;
}

int set_blocking(int fd, int on)
{
    int flags;    
	
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
	DEBUGF("fcntl: F_GETFL failed : %s", strerror(errno));
	return -1;
    }
    if (on)
	flags = flags & ~O_NONBLOCK;
    else
	flags = flags | O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) < 0) {
	    DEBUGF("fcntl: F_SETFL failed : %s", strerror(errno));
	    return -1;
    }
    return 0;
}

int open_pty(char* name, size_t max_namelen)
{
#ifdef HAVE_PTY    
    int fd;
    char slave_name[MAX_PTY_NAME];
    
    if ((fd = local_openpt(O_RDWR|O_NOCTTY)) < 0) { 
	DEBUGF("posix_openpt failed : %s", strerror(errno));
	return -1;
    }
    if (grantpt(fd) < 0) {
	DEBUGF("grantpt failed : %s", strerror(errno));
	goto error;
    }
    if (unlockpt(fd) < 0) {
	DEBUGF("unlockpt failed : %s", strerror(errno));
	goto error;
    }
    if (local_ptsname_r(fd, slave_name, sizeof(slave_name)) < 0) {
	DEBUGF("ptsname_r failed : %s", strerror(errno));
	goto error;
    }
    if (strlen(slave_name) >= max_namelen) {
	errno = ERANGE;
	goto error;
    }
    strcpy(name, slave_name);

    if (set_blocking(fd, 0) < 0)
	goto error;
    return fd;
#endif
	errno = ENOTSUP;
	return -1;    
error:
	{
	    int save_errno = errno;
	    if (fd >= 0) close(fd);
	    errno = save_errno;
	}
	return -1;
}

    
