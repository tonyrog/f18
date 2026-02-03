//
//  F18 emulator
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>

#include "f18.h"
#include "f18_scan.h"
#include "f18_node_data.h"

extern node_t* node[8][18];

// setup left-right connecttion
void init_left_right(int i, int j)
{
    int lr[2];
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, lr) < 0) {
	perror("socketpair lr");
	exit(1);
    }    
    ((reg_node_t*)node[i][j-1])->right_fd = lr[0];
    ((reg_node_t*)node[i][j])->left_fd = lr[1];
}

void init_up_down(int i, int j)
{
    int ud[2];
    if (socketpair(PF_LOCAL, SOCK_STREAM, 0, ud) < 0) {
	perror("socketpair ud");
	exit(1);
    }
    ((reg_node_t*)node[i-1][j])->down_fd = ud[0];
    ((reg_node_t*)node[i][j])->up_fd = ud[1];
}

void init_node(int i, int j)
{
    reg_node_t* np = (reg_node_t*)node[i][j];
    np->up_fd      = -1;
    np->left_fd    = -1;
    np->down_fd    = -1;
    np->right_fd   = -1;    
}


int select_ports(reg_node_t* dp, uint18_t ioreg, int is_input,
		 int* fds, uint18_t* io_rd, uint18_t* io_wr)
{

    if ((ioreg < IOREG_START) || (ioreg > IOREG_END))
	return 0;
    if (io_rd) io_rd[0] = 0;
    if (io_wr) io_wr[0] = 0;
    switch(ioreg) {
    case IOREG_STDIO:
	if ((fds[0] = is_input ? dp->stdin_fd : dp->stdout_fd) < 0)
	    return 0;
	return 1;
    case IOREG_STDIN:
	if (!is_input || (dp->stdin_fd < 0))
	    return 0;
	fds[0] = dp->stdin_fd;
	return 1;
    case IOREG_STDOUT:
	if (is_input || (dp->stdout_fd < 0))
	    return 0;
	fds[0] = dp->stdout_fd;
	return 1;
    case IOREG_TTY:
	if (dp->tty_fd < 0)
	    return 0;
	fds[0] = dp->tty_fd;
	return 1;
    default: {
	int i = 0;
	if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	    if ((ioreg & F18_RIGHT_BIT) && (dp->right_fd >= 0)) {
		fds[i] = dp->right_fd;
		if (io_rd) io_rd[i] = F18_IO_RIGHT_RD;
		if (io_wr) io_wr[i] = F18_IO_RIGHT_WR;
		i++;
	    }
	    if (!(ioreg & F18_DOWN_BIT) && (dp->down_fd >= 0)) {
		fds[i] = dp->down_fd;
		if (io_rd) io_rd[i] = F18_IO_DOWN_RD;
		if (io_wr) io_wr[i] = F18_IO_DOWN_WR;
		i++;
	    }
	    if ((ioreg & F18_LEFT_BIT) && (dp->left_fd >= 0)) {
		fds[i] = dp->left_fd;
		if (io_rd) io_rd[i] = F18_IO_LEFT_RD;
		if (io_wr) io_wr[i] = F18_IO_LEFT_WR;
		i++;
	    }
	    if (!(ioreg & F18_UP_BIT) && (dp->up_fd >= 0)) {
		fds[i] = dp->up_fd;
		if (io_rd) io_rd[i] = F18_IO_UP_RD;
		if (io_wr) io_wr[i] = F18_IO_UP_WR;
		i++;
	    }
	}
	return i;
    }
    }
}

static int write_value(int fd, int is_bin, uint18_t value)
{
    if (is_bin)
	return write(fd, &value, sizeof(value));
    else {
	char buf[8];
	int  len;
	len = snprintf(buf, sizeof(buf), "%05x\n", value);
	if (len >= sizeof(buf)) {
	    fprintf(stderr, "warning: output truncated\n");
	    len = sizeof(buf)-1;
	}
	return write(fd, buf, len);
    }
}


void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    uint18_t io_wr[4];
    int fds[4];
    int nports;
    int i, r;
    reg_node_t* dp = (reg_node_t*) np;
    
    if ((nports = select_ports(dp, ioreg, 0, fds, NULL, io_wr)) == 0) {
	fprintf(stderr, "io error when writing ioreg=%x, not mapped\n", ioreg);
	return;
    }

    if (ioreg == IOREG_TTY) {  // special for character i/o
	char c = value;
	if (write(fds[0], &c, 1) < 0)
	    goto error;
	return;
    }

    for (i = 0; i < nports; i++) {
	uint18_t dd = io_wr[i];
	int fd = fds[i];

	switch(dd) {
	case 0:
	    r = write_value(fd, 0, value);
	    break;
	case F18_IO_RIGHT_WR:
	    r = write_value(fd, (np->flags & FLAG_WR_BIN_RIGHT), value);
	    break;
	case F18_IO_DOWN_WR:
	    r = write_value(fd, (np->flags & FLAG_WR_BIN_DOWN), value);
	    break;
	case F18_IO_LEFT_WR:
	    r = write_value(fd, (np->flags & FLAG_WR_BIN_LEFT), value);
	case F18_IO_UP_WR:
	    r = write_value(fd, (np->flags & FLAG_WR_BIN_UP), value);
	default:
	    break;
	}
	if (r < 0)
	    goto error;
    }
    return;

error:
    fprintf(stderr, "io error when writing ioreg=%x, error=%s\n",
	    ioreg, strerror(errno));
}

uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
{
    uint18_t io_rd[4];
    uint18_t dd = 0;
    int fds[4];
    int fd;
    fd_set readfds;
    int nfds;
    uint18_t value;
    int nports;
    int i;
    int r;
    reg_node_t* dp = (reg_node_t*) np;

    VERBOSE(np, "read_ioreg %05x, dp=%p\n", ioreg, dp);

    if (ioreg == IOREG_IO) {
	uint18_t io_wr[4];
	fd_set writefds;
	struct timeval tm = {0, 0};

	if ((nports = select_ports(dp, IOREG_RDLU, 1, fds, io_rd, io_wr))==0)
	    return 0;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	nfds = 0;
	for (i = 0; i < nports; i++) {
	    FD_SET(fds[i], &readfds);
	    FD_SET(fds[i], &writefds);
	    if (fds[i] >= nfds)
		nfds = fds[i]+1;
	}
	if (select(nfds, &readfds, &writefds, NULL, &tm) < 0)
	    goto error;
	value = 0;
	for (i = 0; i < nports; i++) {
	    if (FD_ISSET(fds[i], &readfds))  // otherside is writing
		value |= io_wr[i];  
	    if (!FD_ISSET(fds[i], &writefds)) // other side is reading
		value |= io_rd[i];
	}
	// fixme: this value is not correct
	return value;
    }

    if ((nports = select_ports(dp, ioreg, 1, fds, io_rd, NULL)) == 0) {
	fprintf(stderr, "io error when writing ioreg=%x, not mapped\n", ioreg);
	return 0;
    }
    VERBOSE(np, "node %p selected %d ports\n", np, nports);

    if (ioreg == IOREG_TTY) { // special for character i/o
	uint8_t c;
	if (read(fds[0], (char*) &c, 1) <= 0)
	    goto error;
	return c;
    }

    FD_ZERO(&readfds);
    nfds = 0;
    for (i = 0; i < nports; i++) {
	FD_SET(fds[i], &readfds);
	if (fds[i] >= nfds)
	    nfds = fds[i]+1;
    }

    // VERBOSE(np,"select nfds=%d\n", nfds);
    if (select(nfds, &readfds, NULL, NULL, NULL) <= 0)
	goto error;

    fd = -1;
    for (i = 0; i < nports; i++) {
	if (FD_ISSET(fds[i], &readfds)) {
	    // just grab the first one that is ready
	    fd = fds[i];
	    dd = io_rd[i];
	    goto again;
	}
    }
    fprintf(stderr, "error: no fd is set?\n");
    return 0;

again:
    if ((fd == 0) && (np->flags & FLAG_TERMINATE))
	return 0;

    // Check binary mode channel 
    if (dd) {
	if ( ((dd == F18_IO_RIGHT_RD) && (np->flags & FLAG_RD_BIN_RIGHT)) ||
	     ((dd == F18_IO_LEFT_RD) && (np->flags & FLAG_RD_BIN_LEFT)) ||
	     ((dd == F18_IO_UP_RD) && (np->flags & FLAG_RD_BIN_UP)) ||
	     ((dd == F18_IO_DOWN_RD) && (np->flags & FLAG_RD_BIN_DOWN))) {
	    r = read(fd, &value, sizeof(value));
	    if (r == 0) {  // input stream has closed
		if (fd == 0)  // it was stdin !
		    np->flags |= FLAG_TERMINATE;  // lets terminate
	    }
	    else if (r < 0) goto error;
	    return value;
	}
    }

    r = scan_line(fd, &value);
    VERBOSE(np,"read line fd=%d, r=%d, value=%05x\n", fd, r, value);
    
    if (r < 0) {
	if ((r == -2)||(r == -3)) {
	    np->flags |= FLAG_TERMINATE;
	    return 0;
	}
	goto error;
    }
    return value;

error:
    fprintf(stderr, "io error when reading ioreg=%x, error=%s\n",
	    ioreg, strerror(errno));
    return 0;
}

