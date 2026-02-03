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
#include "f18_node.h"

extern void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value);
extern uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg);

extern void init_node(int i, int j);
extern void init_up_down(int i, int j);
extern void init_left_right(int i, int j);


#ifndef SIGRETTYPE		/* allow override via Makefile */
#define SIGRETTYPE void
#endif

// NOTE! STACK_SIZE is the smallest possible page number I found working
#define PAGE_SIZE   g_page_size
#define PAGE(x)     ((((x)+g_page_size-1)/g_page_size)*g_page_size)
#define NODE_SIZE   sizeof(reg_node_t)
#define STACK_SIZE  (2*PAGE_SIZE+PTHREAD_STACK_MIN)

static int tty_fd = -1;
static struct termios tty_smode;
static struct termios tty_rmode;
static size_t  g_page_size = 0;
static uint18_t g_flags = 0;

static SIGRETTYPE ctl_c(int);
static SIGRETTYPE suspend(int);
static SIGRETTYPE (*orig_ctl_c)(int);

node_t* node[8][18];

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
    node_t* np = (node_t*) arg;
    
    VERBOSE(np, "node [%d,%d] started\r\n",
	    ID_TO_ROW(np->id), ID_TO_COLUMN(np->id));
    f18_emu(np);
    VERBOSE(np, "node [%d,%d] stopped\r\n",
	    ID_TO_ROW(np->id), ID_TO_COLUMN(np->id));
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

    alloc_size = v*h*(PAGE(NODE_SIZE));
    if (g_flags & FLAG_VERBOSE) {
	fprintf(stderr, "page size %ld\n", g_page_size);
	fprintf(stderr, "alloc size: %ld\n", alloc_size);
	fprintf(stderr, "stack size: %ld bytes\n", PAGE(STACK_SIZE));
	fprintf(stderr, "node size: %ld bytes\n", PAGE(NODE_SIZE));
	fprintf(stderr, "sizeof(node_t): %lu\n", sizeof(node_t));
	fprintf(stderr, "sizeof(reg_node_t): %lu\n", sizeof(reg_node_t));
	fprintf(stderr, "layout: %d x %d\n", v, h);
    }

    if (posix_memalign(&node_mem, g_page_size, alloc_size)) {
	perror("posix_memalign (node_mem) failed");
	exit(1);
    }

    // reset global node connection 8x18 array pointers!
    memset(node, 0, sizeof(node));

    // start moving memory into the node data
    np_mem = (uint8_t*) node_mem;
    for (i = 0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    reg_node_t* np = (reg_node_t*) np_mem;
	    node[i][j]= (node_t*) np;
	    
	    np->n.id = MAKE_ID(i,j);

	    // fixme! (SERDES node)
	    np->tty_fd     = tty_fd;
	    np->stdin_fd   = (file_fd >= 0) ? file_fd : 0;
	    np->stdout_fd  = 1;
	    
	    init_node(i, j);

	    np->n.io     = IMASK;
	    np->n.flags  = g_flags;  // specific for node?
	    np->n.delay  = delay;    // specific for node?
	    
	    if ((i == 0) && (j == 0)) {
		np->n.p = IOREG_STDIO;
	    }
	    else
		np->n.p = IOREG_RDLU;
	    np->n.b = IOREG_TTY;
	    np->n.read_ioreg  = f18_read_ioreg;
	    np->n.write_ioreg = f18_write_ioreg;

	    np_mem += (PAGE(NODE_SIZE));
	}
    }
    // create all the socket pairs needed
    for (i = 0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    if (j > 0)   // create left/right
		init_left_right(i, j);
	    if (i > 0)  // create up/down
		init_up_down(i, j);
	}
    }

    for (i=0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    reg_node_t* np = (reg_node_t*) node[i][j];

	    pthread_attr_init(&np->attr);
	    pthread_attr_setstacksize(&np->attr, PAGE(STACK_SIZE));
	    if (g_flags & FLAG_VERBOSE) {
		size_t size;
		pthread_attr_getstacksize(&np->attr, &size);
		fprintf(stderr, "thread stack size: %ld\n", size);
	    }
	    VERBOSE(np, "about to start node %p [%d,%d]\r\n",
		    np, ID_TO_ROW(np->n.id), ID_TO_COLUMN(np->n.id));
	    if (pthread_create(&np->thread,&np->attr,f18_emu_start,(void*) np) <0) {
		perror("pthread_create"); 
		exit(1);
	    }
	}
    }

    // fixme: use correct node as terminal node
    pthread_join(((reg_node_t*)node[0][0])->thread, &status);

    // fixme wait for condition running == 0 

    // while(1) {
	// printf("Z\n");
    //  sleep(10);
    // }
    if (tty_fd >= 0)
	tty_reset(tty_fd); // atexit?
    exit(0);
}
