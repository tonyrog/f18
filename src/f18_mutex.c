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
    pthread_mutex_t lock;
    pthread_cond_t cond;

    uint18_t dmask;    // DIR_BIT(x) available directions

    // Rendezvous state (protected by this node's lock)
    uint18_t wmask;    // DIR_BIT(x) directions wanting to write
    uint18_t rmask;    // DIR_BIT(x) directions wanting to read
    uint18_t data;     // write: value to send / read: received value
    int      completed; // transfer completed flag

    int tty_fd;
    int stdin_fd;
    int stdout_fd;
} node_data_t;

node_data_t node[8][18];

// Invert direction: UP<->DOWN, LEFT<->RIGHT
static const uint18_t invert_dir[4] = { DOWN, RIGHT, UP, LEFT };

static node_data_t* get_remote(node_data_t* dp, int dir)
{
    switch(dir) {
    case UP:    return &node[dp->i-1][dp->j];
    case LEFT:  return &node[dp->i][dp->j-1];
    case DOWN:  return &node[dp->i+1][dp->j];
    case RIGHT: return &node[dp->i][dp->j+1];
    default: return NULL;
    }
}

// Decode ioreg into DIR_BIT mask of target directions,
// filtered by available directions (dmask).
static uint18_t select_dirs(node_data_t* dp, uint18_t ioreg)
{
    uint18_t dirs = 0;
    ioreg = ~ioreg ^ (dp->dmask << 8);
    if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	if (ioreg & F18_RIGHT_BIT)  dirs |= DIR_BIT(RIGHT);
	if (!(ioreg & F18_DOWN_BIT)) dirs |= DIR_BIT(DOWN);
	if (ioreg & F18_LEFT_BIT)   dirs |= DIR_BIT(LEFT);
	if (!(ioreg & F18_UP_BIT))  dirs |= DIR_BIT(UP);
    }
    return dirs;
}

//
// Rendezvous protocol for inter-node communication.
//
// Each node has: lock, cond, wmask, rmask, data, completed.
//   wmask: DIR_BIT mask of directions this node wants to write to
//   rmask: DIR_BIT mask of directions this node wants to read from
//   data:  value to write (set by writer) / received value (set for reader)
//   completed: set when a transfer has been done for this node
//
// Protocol (3 phases):
//   1. Probe: try to find a matching partner (lock only remote)
//   2. Announce + re-probe: set own mask, then probe again
//   3. Wait: sleep on cond until partner completes the transfer
//
// Only one lock is held at a time => no deadlock.
// First to claim wins for multiport (clears all mask bits).
//

static void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    node_data_t* dp = np->user;
    uint18_t dirs;
    int dir;

    if ((ioreg < IOREG_START) || (ioreg > IOREG_END)) {
	fprintf(stderr, "io error when writing ioreg=%x, not mapped\n", ioreg);
	return;
    }

    // Handle stdio/stdout/tty writes
    if ((ioreg == IOREG_STDIO) || (ioreg == IOREG_STDOUT)) {
	if (dp->stdout_fd >= 0) {
	    char buf[8];
	    int len = snprintf(buf, sizeof(buf), "%05x\n", value);
	    write(dp->stdout_fd, buf, len);
	}
	return;
    }
    if (ioreg == IOREG_TTY) {
	if (dp->tty_fd >= 0) {
	    char c = value;
	    write(dp->tty_fd, &c, 1);
	}
	return;
    }

    dirs = select_dirs(dp, ioreg);
    if (dirs == 0) {
	fprintf(stderr, "[%d,%d]: io error when writing ioreg=%x, no directions\n", ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg);
	return;
    }

    // Phase 1: probe - try to find a reader already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	node_data_t* rp = get_remote(dp, dir);
	uint18_t idir = invert_dir[dir];

	pthread_mutex_lock(&rp->lock);
	if (rp->rmask & DIR_BIT(idir)) {
	    // Reader is waiting for data from our direction - deliver!
	    rp->data = value;
	    rp->rmask = 0;          // first writer wins, clear all
	    rp->completed = 1;
	    pthread_cond_signal(&rp->cond);
	    pthread_mutex_unlock(&rp->lock);
	    return;
	}
	pthread_mutex_unlock(&rp->lock);
    }

    // Phase 2: announce ourselves (store value + set wmask) then re-probe
    pthread_mutex_lock(&dp->lock);
    dp->data = value;
    dp->wmask = dirs;
    dp->completed = 0;
    pthread_mutex_unlock(&dp->lock);

    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	node_data_t* rp = get_remote(dp, dir);
	uint18_t idir = invert_dir[dir];

	pthread_mutex_lock(&rp->lock);
	if (rp->rmask & DIR_BIT(idir)) {
	    rp->data = value;
	    rp->rmask = 0;
	    rp->completed = 1;
	    pthread_cond_signal(&rp->cond);
	    pthread_mutex_unlock(&rp->lock);
	    // Clear our announcement
	    pthread_mutex_lock(&dp->lock);
	    dp->wmask = 0;
	    dp->completed = 1;
	    pthread_mutex_unlock(&dp->lock);
	    return;
	}
	pthread_mutex_unlock(&rp->lock);
    }

    // Phase 3: wait for a reader to find us and complete the transfer
    pthread_mutex_lock(&dp->lock);
    while (!dp->completed)
	pthread_cond_wait(&dp->cond, &dp->lock);
    dp->wmask = 0;
    pthread_mutex_unlock(&dp->lock);
}


static uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
{
    node_data_t* dp = np->user;
    uint18_t dirs;
    uint18_t value;
    int dir, r;

    if ((ioreg < IOREG_START) || (ioreg > IOREG_END)) {
	fprintf(stderr, "io error when reading ioreg=%x, not mapped\n", ioreg);
	return 0;
    }

    // Handle stdio/stdin/tty reads
    if ((ioreg == IOREG_STDIO) ||
	(ioreg == IOREG_STDIN) ||
	(ioreg == IOREG_TTY)) {
	if ((ioreg == IOREG_TTY) && (dp->tty_fd >= 0)) {
	    if ((r = scan_line(dp->tty_fd, &value)) < 0) {
		fprintf(stderr, "io error when reading tty=%x\n", ioreg);
		return 0;
	    }
	    return value;
	}
	else if (dp->stdin_fd >= 0) {
	    if ((r = scan_line(dp->stdin_fd, &value)) < 0) {
		VERBOSE(np, "io error reading %05x r = %d\n", ioreg, r);
		if ((r == -2) || (r == -3)) {
		    np->flags |= FLAG_TERMINATE;
		    return 0;
		}
		fprintf(stderr, "io error when reading stdin=%x\n", ioreg);
		return 0;
	    }
	    return value;
	}
	else
	    return 0;
    }

    dirs = select_dirs(dp, ioreg);
    if (dirs == 0) {
	fprintf(stderr, "[%d,%d]: io error when reading ioreg=%x, no directions\n", ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg);	
	return 0;
    }

    // Phase 1: probe - try to find a writer already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	node_data_t* rp = get_remote(dp, dir);
	uint18_t idir = invert_dir[dir];

	pthread_mutex_lock(&rp->lock);
	if (rp->wmask & DIR_BIT(idir)) {
	    // Writer is waiting to send in our direction - grab data!
	    value = rp->data;
	    rp->wmask = 0;          // first reader wins, clear all
	    rp->completed = 1;
	    pthread_cond_signal(&rp->cond);
	    pthread_mutex_unlock(&rp->lock);
	    return value;
	}
	pthread_mutex_unlock(&rp->lock);
    }

    // Phase 2: announce ourselves (set rmask) then re-probe
    pthread_mutex_lock(&dp->lock);
    dp->rmask = dirs;
    dp->completed = 0;
    pthread_mutex_unlock(&dp->lock);

    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	node_data_t* rp = get_remote(dp, dir);
	uint18_t idir = invert_dir[dir];

	pthread_mutex_lock(&rp->lock);
	if (rp->wmask & DIR_BIT(idir)) {
	    value = rp->data;
	    rp->wmask = 0;
	    rp->completed = 1;
	    pthread_cond_signal(&rp->cond);
	    pthread_mutex_unlock(&rp->lock);
	    // Clear our announcement
	    pthread_mutex_lock(&dp->lock);
	    dp->rmask = 0;
	    dp->completed = 1;
	    pthread_mutex_unlock(&dp->lock);
	    return value;
	}
	pthread_mutex_unlock(&rp->lock);
    }

    // Phase 3: wait for a writer to find us and complete the transfer
    pthread_mutex_lock(&dp->lock);
    while (!dp->completed)
	pthread_cond_wait(&dp->cond, &dp->lock);
    value = dp->data;  // writer stored value here for us
    dp->rmask = 0;
    pthread_mutex_unlock(&dp->lock);
    return value;
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
    
    VERBOSE(dp->np, "node %p [%d,%d] started\r\n", dp->np, dp->i, dp->j);
    f18_emu(dp->np);
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

	    dp->wmask = 0;
	    dp->rmask = 0;
	    dp->data = 0;
	    dp->completed = 0;

	    // dp->stack = (void*) (np_mem + PAGE(NODE_SIZE));
	    dp->tty_fd     = tty_fd;
	    dp->stdin_fd   = (file_fd >= 0) ? file_fd : 0;
	    dp->stdout_fd  = 1;
	    
	    memset(np, 0, sizeof(node_t));
	    np->io = IMASK;
	    np->id = (i<<5)|(j);
	    np->flags      = g_flags;  // specific for node?
	    np->delay      = delay;    // specific for node?

	    if ((i == 0) && (j == 0))
		np->p = IOREG_STDIO;
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
		node[i][j-1].dmask |= DIR_BIT(RIGHT);
		node[i][j].dmask |= DIR_BIT(LEFT);
	    }
	    if (i > 0) {  // create up/down
		node[i-1][j].dmask |= DIR_BIT(DOWN);
		node[i][j].dmask |= DIR_BIT(UP);		
	    }
	}
    }

    for (i=0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    node_data_t* dp  = &node[i][j];

	    pthread_mutex_init(&dp->lock, NULL);
	    pthread_cond_init(&dp->cond, NULL);
	    
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
    //	sleep(10);
    // }
    if (tty_fd >= 0)
	tty_reset(tty_fd); // atexit?
    exit(0);
}
