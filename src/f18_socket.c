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

#ifndef SIGRETTYPE		/* allow override via Makefile */
#define SIGRETTYPE void
#endif

// NOTE! STACK_SIZE is the smallest possible page number I found working
#define PAGE_SIZE   g_page_size
#define PAGE(x)     ((((x)+g_page_size-1)/g_page_size)*g_page_size)
#define NODE_SIZE   sizeof(node_t)
#define STACK_SIZE  (2*PAGE_SIZE+PTHREAD_STACK_MIN)


static int tty_fd = -1;
static struct termios tty_smode;
static struct termios tty_rmode;
static size_t  g_page_size = 0;
static uint18_t g_flags = 0;

static SIGRETTYPE ctl_c(int);
static SIGRETTYPE suspend(int);
static SIGRETTYPE (*orig_ctl_c)(int);

typedef struct {
    int      i;
    int      j;
    node_t*  np;
    pthread_t thread;
    pthread_attr_t attr;
    int up_fd;
    int left_fd;
    int down_fd;
    int right_fd;

    int tty_fd;
    int stdin_fd;
    int stdout_fd;    
} node_data_t;

node_data_t node[8][18];

int select_ports(node_data_t* dp, uint18_t ioreg, int is_input,
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


static void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    uint18_t io_wr[4];
    int fds[4];
    int nports;
    int i, r;
    node_data_t* dp = np->user;
    
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

static uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
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
    node_data_t* dp = np->user;

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
	if (r == -2) {
	    np->flags |= FLAG_TERMINATE;
	    return 0;
	}
	if (r == -3) {
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


SIGRETTYPE (*sys_sigset(int sig, SIGRETTYPE (*func)(int)))(int)
{
    struct sigaction act, oact;

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = func;
    sigaction(sig, &act, &oact);
    return(oact.sa_handler);
}

void sys_sigblock(int sig)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_BLOCK, &mask, (sigset_t *)NULL);
}

void sys_sigrelease(int sig)
{
    sigset_t mask;

    sigemptyset(&mask);
    sigaddset(&mask, sig);
    sigprocmask(SIG_UNBLOCK, &mask, (sigset_t *)NULL);
}

static int tty_set(int fd)
{
    if (tcsetattr(fd, TCSANOW, &tty_smode) < 0)
	return -1;
    return 0;
}

static int tty_reset(int  fd)
{
    if (tcsetattr(fd, TCSANOW, &tty_rmode) < 0)
	return -1;
    return 0;
}

static SIGRETTYPE suspend(int sig)
{
    tty_reset(tty_fd);

    sys_sigset(sig, SIG_DFL);	/* Set signal handler to default */
    sys_sigrelease(sig);	/* Allow 'sig' to come through */
    kill(getpid(), sig);	/* Send ourselves the signal */
    sys_sigblock(sig);		/* Reset to old mask */
    sys_sigset(sig, suspend);	/* Reset signal handler */

    tty_set(tty_fd);
}


static SIGRETTYPE ctl_c(int sig)
{
    tty_reset(tty_fd);

    sys_sigset(sig, orig_ctl_c); /* Set ctl_c break handler to original */
    sys_sigrelease(sig);	/* Allow 'sig' to come through */
    kill(getpid(), sig);	/* Send ourselves the signal */
    sys_sigblock(sig);		/* Reset to old mask */
    sys_sigset(sig, ctl_c);	/* Reset signal handler */

    tty_set(tty_fd);
}


// Setup termial file descriptor "io port"

int tty_init(int fd)
{
    if (tcgetattr(fd, &tty_rmode) < 0)
	return -1;
    tty_smode = tty_rmode;

    // Default characteristics for all usage including termcap output.
    tty_smode.c_iflag &= ~ISTRIP;

    tty_smode.c_iflag |= ICRNL;     // cr -> nl on input
    tty_smode.c_lflag &= ~ICANON;
    tty_smode.c_oflag |= OPOST;     // nl -> cr-nl ..
    // Must get these really right or funny effects can occur.
    tty_smode.c_cc[VMIN] = 1;
    tty_smode.c_cc[VTIME] = 0;
#ifdef VDSUSP
    tty_smode.c_cc[VDSUSP] = 0;
#endif
    tty_smode.c_cflag &= ~(CSIZE | PARENB); // clear char-size,disable parity
    tty_smode.c_cflag |= CS8;               // 8-bit
    tty_smode.c_lflag &= ~ECHO;             // no echo

    if (tty_set(fd) < 0) {
	tty_fd = -1;
	tty_reset(fd);
	return -1;
    }
    tty_fd = fd;

    sys_sigset(SIGTSTP, suspend);
    sys_sigset(SIGTTIN, suspend);
    sys_sigset(SIGTTOU, suspend);
    orig_ctl_c = sys_sigset(SIGINT, ctl_c);
    return 0;
}


void usage(char* prog)
{
    fprintf(stderr, "usage: %s [options]\n", prog);
    fprintf(stderr, " options:\n"
	    "    -v               Enable verbose   (if debug compiled)\n"
	    "    -t               Enable trace     (if debug compiled)\n"
	    "    -i               Interactive\n"	    
	    "    -D reg,ram,rs,ds Dump registers,ram,return-stack,data-stack\n"
	    "    -d <delay>       Set delay between instructions (in usecs)\n"
	    "    -l VxH           Set processor mesh layout (max 8x18)\n"
	    "    -f input-file    Read interpreted stream\n"	    
	);
    exit(1);
}

void* f18_emu_start(void *arg)
{
    node_data_t* dp = (node_data_t*) arg;
    
    VERBOSE(dp->np, "node [%d,%d] started\r\n", dp->i, dp->j);
    f18_emu(dp->np);
    VERBOSE(dp->np, "node [%d,%d] stopped\r\n", dp->i, dp->j);
    return NULL;
}

//
//  Start F18 emulator
//  -v               Verbose       (if debug compiled)
//  -t               Trace         (if debug comipled)
//  -i               Interactive
//  -D reg,ram,rs,ds Dump registers,ram,return-stack,data-stack
//  -d delay         Set delay between instructions
//  -l VxH           processor layout (max 8x18)
//

int main(int argc, char** argv)
{
    int fd;
    int c;
    int i,j;
    useconds_t delay = 0;
    char* opt_layout = NULL;
    uint32_t h=1, v=1;
    void* node_mem;
    uint8_t* np_mem;
    size_t alloc_size;
    int interactive = 0;
    char* filename = NULL;
    int file_fd = -1;
    void* status = 0;
    
    g_page_size = sysconf(_SC_PAGESIZE);  // must be first!
    g_flags = 0;
    
    while((c=getopt(argc, argv, "ivtl:d:D:f:")) != -1) {
	switch(c) {
	case 'i':
	    interactive = 1;
	    break;
	case 'f':
	    filename = optarg;
	    break;
	case 'v':
	    g_flags |= FLAG_VERBOSE;
	    break;
	case 't':
	    g_flags |= FLAG_TRACE;
	    break;
	case 'l':
	    opt_layout = optarg;
	    break;
	case 'd': {
	    char* endptr = NULL;
	    if ((delay = strtol(optarg, &endptr,0)) == 0) {
		if (endptr && (*endptr != '\0'))
		    usage(basename(argv[0]));
	    }
	    break;
	}
	case 'D': {
	    char* ptr = optarg;
	    while(*ptr) {
		if (strncmp(ptr, "reg", 3) == 0) {
		    if ((ptr[3] == '\0') || (ptr[3] == ',')) {
			g_flags |= FLAG_DUMP_REG;
			ptr += (ptr[3] == ',') ? 4 : 3;
		    }
		    else
			usage(basename(argv[0]));
		}
		else if (strncmp(ptr, "ram", 3) == 0) {
		    if ((ptr[3] == '\0') || (ptr[3] == ',')) {
			g_flags |= FLAG_DUMP_RAM;
			ptr += (ptr[3] == ',') ? 4 : 3;
		    }
		    else 
			usage(basename(argv[0]));
		}
		else if (strncmp(ptr, "rs", 2) == 0) {
		    if ((ptr[2] == '\0') || (ptr[2] == ',')) {
			g_flags |= FLAG_DUMP_RS;
			ptr += (ptr[2] == ',') ? 3 : 2;
		    }
		    else 
			usage(basename(argv[0]));
		}
		else if (strncmp(ptr, "ds", 2) == 0) {
		    if ((ptr[2] == '\0') || (ptr[2] == ',')) {
			g_flags |= FLAG_DUMP_DS;
			ptr += (ptr[2] == ',') ? 3 : 2;
		    }
		    else 
			usage(basename(argv[0]));
		}
		else
		    usage(basename(argv[0]));
	    }
	    break;
	}
	case '?':
	default:
	    usage(basename(argv[0]));
	}
    }

    argc -= optind;
    argv += optind;

    if (interactive) {
	if ((fd = open("/dev/tty", O_RDWR)) < 0) {
	    fprintf(stderr, "unabled to open tty, error=%s\n",
		    strerror(errno));
	    exit(1);
	}
	if (tty_init(fd) < 0) {
	    fprintf(stderr, "unabled to setup tty, error=%s\n",
		    strerror(errno));
	    exit(1);
	}
    }
    if (filename != NULL) {
	if ((file_fd = open(filename, O_RDONLY)) < 0) {
	    fprintf(stderr, "unabled to open file %s, error=%s\n",
		    filename, strerror(errno));
	    exit(1);
	}
    }

    if (opt_layout) {
	char* ptr = opt_layout;
	v = strtol(ptr,&ptr,0);
	if ((v != 0) && (*ptr == 'x')) {
	    ptr++;
	    h = strtol(ptr,&ptr,0);
	    if ((h == 0) || (ptr && (*ptr != '\0')))
		usage(basename(argv[0]));
	}
	else if ((v != 0) && (*ptr == '\0')) {
	    // when only one numer is given assume it is 1xH
	    h = v; v = 1;
	}
	else if ((v == 0) && (*ptr != '\0'))
	    usage(basename(argv[0]));
	if ((v > 8) || (h > 18))
	    usage(basename(argv[0]));
    }

    // allocate HxV nodes threads (example 3x3 )
    // A - B - C
    // |   |   |
    // D - E - F
    // |   |   |
    // G - H - I
    //
    // connect the lines with socket pairs
    //
    alloc_size = v*h*(PAGE(NODE_SIZE));
    if (g_flags & FLAG_VERBOSE) {
	fprintf(stderr, "page size %ld\n", g_page_size);
	fprintf(stderr, "alloc size: %ld\n", alloc_size);
	fprintf(stderr, "stack size: %ld bytes\n", PAGE(STACK_SIZE));
	fprintf(stderr, "node size: %ld bytes\n", PAGE(NODE_SIZE));
	fprintf(stderr, "sizeof(node_t): %lu\n", sizeof(node_t));	
	fprintf(stderr, "layout: %d x %d\n", v, h);
    }

    if (posix_memalign(&node_mem, g_page_size, alloc_size)) {
	perror("posix_memalign (node_mem) failed");
	exit(1);
    }

    // reset global node connection 8x18 array
    memset(node, 0, sizeof(node));

    // start moving memory into the node data
    np_mem = (uint8_t*) node_mem;
    for (i = 0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    node_data_t* dp = &node[i][j];
	    node_t* np = (node_t*) np_mem;
	    dp->i     = i;
	    dp->j     = j;
	    dp->np    = np;
	    dp->up_fd      = -1;
	    dp->left_fd    = -1;
	    dp->down_fd    = -1;
	    dp->right_fd   = -1;
	    dp->tty_fd     = tty_fd;
	    dp->stdin_fd   = (file_fd >= 0) ? file_fd : 0;
	    dp->stdout_fd  = 1;
	    
	    // dp->stack = (void*) (np_mem + PAGE(NODE_SIZE));

	    memset(np, 0, sizeof(node_t));
	    np->io = IMASK;
	    np->id = (i<<5)|(j);	    
	    np->flags      = g_flags;  // specific for node?
	    np->delay      = delay;    // specific for node?
	    
	    if ((i == 0) && (j == 0)) {
		np->p = IOREG_STDIO;
	    }
	    else
		np->p = IOREG_RDLU;
	    np->b = IOREG_TTY;
	    np->user = dp;	    
	    np->read_ioreg  = f18_read_ioreg;
	    np->write_ioreg = f18_write_ioreg;

	    np_mem += (PAGE(NODE_SIZE));
	}
    }
    // create all the socket pairs needed
    for (i = 0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    if (j > 0) {  // create left/right
		int lr[2];
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, lr) < 0) {
		    perror("socketpair lr");
		    exit(1);
		}
		node[i][j-1].right_fd = lr[0];
		node[i][j].left_fd = lr[1];
	    }
	    if (i > 0) {  // create up/down
		int ud[2];
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, ud) < 0) {
		    perror("socketpair ud");
		    exit(1);
		}
		node[i-1][j].down_fd = ud[0];
		node[i][j].up_fd = ud[1];
	    }
	}
    }

    for (i=0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    node_data_t* dp  = &node[i][j];
	    pthread_attr_init(&dp->attr);
	    pthread_attr_setstacksize(&dp->attr, PAGE(STACK_SIZE));
	    if (g_flags & FLAG_VERBOSE) {
		size_t size;
		pthread_attr_getstacksize(&dp->attr, &size);
		fprintf(stderr, "thread stack size: %ld\n", size);
	    }
	    VERBOSE(dp->np, "about to start node %p [%d,%d]\r\n",
		    dp->np, dp->i, dp->j);
	    if (pthread_create(&dp->thread,&dp->attr,f18_emu_start,(void*) dp) <0) {
		perror("pthread_create"); 
		exit(1);
	    }
	}
    }

    pthread_join(node[0][0].thread, &status);

    // while(1) {
	// printf("Z\n");
    //  sleep(10);
    // }
    if (tty_fd >= 0)
	tty_reset(tty_fd); // atexit?
    exit(0);
}
