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
#include <stdatomic.h>
#include <pthread.h>

#include "f18.h"
#include "f18_scan.h"
#include "f18_node.h"
#include "f18_debug.h"
#include "f18_tui.h"

#define MAX_SCAN_HEAP_SIZE 256 // symbols table & names
#define MAX_LINE_LEN 80

extern int open_pty(char* name, size_t max_namelen);

extern void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value);
extern uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg);

extern void init_node(int i, int j);

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
static uint18_t g_id    = 999;
uint32_t g_v = 1, g_h = 1;  // Non-static for TUI access
static char* g_step_spec = NULL;  // -I step node specification

// System thread state tracking (atomics for counters, mutex/cond for wait)
static _Atomic int num_active = 0;
static _Atomic int num_blocked_port = 0;
static _Atomic int num_blocked_ext = 0;
static _Atomic int num_terminated = 0;
static pthread_mutex_t sys_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  sys_cond = PTHREAD_COND_INITIALIZER;

static void check_done(void)
{
    if (num_active == 0 && num_blocked_ext == 0) {
	pthread_mutex_lock(&sys_lock);
	pthread_cond_signal(&sys_cond);
	pthread_mutex_unlock(&sys_lock);
    }
}

void sys_thread_started(void)
{
    // num_active is pre-set in main() before thread creation
}

void sys_thread_terminated(void)
{
    num_active--;
    num_terminated++;
    check_done();
}

void sys_enter_blocked_port(void)
{
    num_blocked_port++;
    num_active--;
    check_done();
}

void sys_leave_blocked_port(void)
{
    num_active++;
    num_blocked_port--;
}

void sys_enter_blocked_ext(void)
{
    num_blocked_ext++;
    num_active--;
}

void sys_leave_blocked_ext(void)
{
    num_active++;
    num_blocked_ext--;
    check_done();
}

static SIGRETTYPE ctl_c(int);
static SIGRETTYPE suspend(int);
static SIGRETTYPE (*orig_ctl_c)(int);

node_t* node[8][18];

async_reader_t r708;
async_writer_t w708;

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
	    "    -n               No execute, just load, dump etx\n"
	    "    -I               id of node to dump, 888 for map\n"    
	    "    -D <comma-list>  Dump data and registers\n"
	    "       reg           registers\n"
	    "       ram           RAM\n"
	    "       rom           ROM\n"
            "       rs            return stack\n"
	    "       ds            data stack\n"
	    "    -d <delay>       Set delay between instructions (in usecs)\n"
	    "    -l VxH           Set processor mesh layout (max 8x18)\n"
	    "    -f load-file     Load node RAM from file (testing)\n"
	    "    -b <baud>        Set async boot baud rate\n"
	    "    -P               GPIO poll mode (no wakeup wait)\n"
	);
    exit(1);
}

// Global emulator speed in instructions per microsecond
double g_emu_speed = 0.0;

// Helper: get current time in nanoseconds (monotonic)
static inline long long get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// Benchmark: measure emulator speed using unext loop on node[0][0]
// Must be called after nodes are initialized
// Returns instructions per microsecond
double f18_benchmark(node_t* np, int iterations)
{
    long long nt0, nt1;
    // struct timeval t0, t1;
    double loop1;
    double loop2;
    uint18_t saved_ram[3];
    f18_regs_t saved_reg;
    uint18_t ins;

    // Save current state
    saved_ram[0] = np->ram[0];
    saved_ram[1] = np->ram[1];
    saved_ram[2] = np->ram[2];    
    saved_reg = np->reg;

    // Code at RAM[0]: unext ; . .  (loop R times, then return)
    // unext ; . . 
    // Word = (0x04<<13)|(0x00<<8)|(0x1C<<3)|0x04 = 0x080E4
    // XOR with IMASK: 0x080E4 ^ 0x15555 = 0x1D5B1
    //
    ins = MAKE_INS(INS_UNEXT,INS_RETURN,INS_NOP,INS_NOP);
    np->ram[0] = ins ^ IMASK; // 0x1D5B1;

    // Initialize registers for benchmark
    // unext does POP_r when R=0, then ; does another POP_r
    // So we need RP=2: unext pops rs[1], ; pops rs[0]=termination
    np->reg.p = 0;              // Start at RAM[0]
    np->reg.r = iterations;     // Loop count for unext
    np->reg.rp = 2;             // unext POP→RP=1, ; POP→RP=0
    np->rs[0] = 0x3FFFF;        // Magic termination value (popped by ;)
    np->rs[1] = 0;              // Dummy (popped by unext)
    np->reg.sp = 0;
    np->reg.t = 0;
    np->reg.s = 0;
    np->reg.b = IOREG_IO;

    printf("Benchmark: loop1 running %d unext iterations...\n", iterations);

    // gettimeofday(&t0, NULL);
    nt0 = get_time_ns();
    f18_emu(np);
    // gettimeofday(&t1, NULL);
    nt1 = get_time_ns();

    // loop1 = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    loop1 = (nt1 - nt0) / 1000;

    printf("Benchmark1: %d iterations in %.0f us (%.2f MIPS)\n",
           iterations, loop1,
           loop1 > 0 ? (double)iterations / loop1 : 0);

    // No sync
    // loop:
    //   @ drop . .
    //   . drop next:loop
    // done:
    //   ;
    ins = MAKE_INS(INS_FETCH,INS_NOP,INS_NOP,INS_NOP);
    np->ram[0] = ins ^ (IMASK & 0x3FFFF);
    ins = MAKE_INS_J3(INS_NOP,INS_DROP,INS_NEXT,0);
    np->ram[1] = ins ^ (IMASK & 0x3FFF8);
    ins = MAKE_INS_J3(INS_RETURN,INS_NOP,INS_NOP,INS_NOP);
    np->ram[2] = ins ^ (IMASK & 0x1FFFF);

    np->reg.p = 0;              // Start at RAM[0]
    np->reg.r = iterations;     // Loop count for unext
    np->reg.rp = 2;             // unext POP→RP=1, ; POP→RP=0
    np->rs[0] = 0x3FFFF;        // Magic termination value (popped by ;)
    np->rs[1] = 0;              // Dummy (popped by unext)
    np->reg.sp = 0;
    np->reg.t = 0;
    np->reg.s = 0;
    np->reg.a = 0;
    np->reg.b = IOREG_IO;

    printf("Benchmark2: loop2 running %d next iterations...\n", iterations);
    
    // gettimeofday(&t0, NULL);
    nt0 = get_time_ns();    
    f18_emu(np);
    // gettimeofday(&t1, NULL);
    nt1 = get_time_ns();
    
    // loop2 = (t1.tv_sec - t0.tv_sec) * 1000000.0 + (t1.tv_usec - t0.tv_usec);
    loop2 = (nt1 - nt0) / 1000;
    
    printf("Benchmark2: %d iterations in %.0f us (%.2f MIPS)\n",
           iterations, loop2,
	   loop2 > 0 ? (double)iterations / loop2 : 0);
    
    // Restore state
    np->ram[0] = saved_ram[0];
    np->ram[1] = saved_ram[1];
    np->ram[2] = saved_ram[2];
    np->reg = saved_reg;

    if (loop2 > 0) {
        return (double)iterations / loop2;
    }
    return 100.0;  // Default if measurement fails
}

void* f18_emu_start(void *arg)
{
    node_t* np = (node_t*) arg;

    sys_thread_started();
    VERBOSE(np, "node started%s\n", "");
    f18_emu(np);
    VERBOSE(np, "node stopped%s\n", "");
    // FIXME: cleanup neighbours etc ... possible? needed?
    sys_thread_terminated();
    return NULL;
}

void* async_reader_start(void *arg)
{
    sys_thread_started();
    async_reader(arg);
    sys_thread_terminated();
    return NULL;
}

void* async_writer_start(void *arg)
{
    sys_thread_started();
    async_writer(arg);
    sys_thread_terminated();
    return NULL;
}

char node_map[3*8][5*18] = { {' '}, };

void draw_com_map()
{
    int v, h;

    for (v = 0; v < 8; v++) {
	for (h = 0; h < 18; h++) {
	    int i = v*3;
	    int j = h*5;
	    uint9_t comm = ConfigMap[v][h].comm;
	    uint9_t io_addr = ConfigMap[v][h].io_addr;
	    uint9_t dbits   = dirbits(v, h, comm);
	    uint9_t ibits   = io_addr ? dirbits(v, h, io_addr) : 0;

	    memcpy(&node_map[i][j],   "     ", 5);
	    memcpy(&node_map[i+1][j], "     ", 5);
	    memcpy(&node_map[i+2][j], "     ", 5);
	    node_map[i+1][j+2] = 'x';
	    
	    if (dbits & DIR_BIT(UP))
		node_map[i+0][j+2] = 'N';
	    else if (ibits & DIR_BIT(UP))
		node_map[i+0][j+2] = 'G';	    

	    if (dbits & DIR_BIT(LEFT))
		node_map[i+1][j+0] = 'W';
	    else if (ibits & DIR_BIT(LEFT))
		node_map[i+1][j+0] = 'G';

	    if (dbits & DIR_BIT(RIGHT))
		node_map[i+1][j+4] = 'E';
	    else if (ibits & DIR_BIT(RIGHT))
		node_map[i+1][j+4] = 'G';


	    if (dbits & DIR_BIT(DOWN))
		node_map[i+2][j+2] =  'S';
	    else if (ibits & DIR_BIT(DOWN))
		node_map[i+2][j+2] = 'G';
	}
    }

    printf("    ");
    for (h = 0; h < 18; h++)
	printf("  %02d  ", h);
    printf("\n");

    printf("   +");
    for (h = 0; h < 18; h++)
	printf("-----+");
    printf("\n");

    // draw 7 up 0 down
    for (v = 7; v >= 0; v--) {
	int i, j, k, l;
	
	for (l = 0; l < 3; l++) {
	    if (l == 1)
		printf(" %d |", v);
	    else
		printf("   |");
	    i = v*3 + l;
	    for (h = 0; h < 18; h++) {
		j = h*5;	    
		for (k = 0 ; k < 5; k++)
		    printf("%c", node_map[i][j+k]);
		printf("|");	    
	    }
	    printf("\n");
	}
	printf("   +");
	for (h = 0; h < 18; h++)
	    printf("-----+");
	printf("\n");	
    }
}

struct {
    clockid_t id;
    const char* name;
}  clock_source[] =
{
    {CLOCK_REALTIME, "REALTIME"},
    {CLOCK_REALTIME_COARSE, "REALTIME_COARSE"},
    {CLOCK_MONOTONIC, "MONOTONIC"},
    {CLOCK_MONOTONIC_COARSE, "MONOTONIC_COARSE"},
    {CLOCK_MONOTONIC_RAW, "MONOTONIC_RAW"},
    {0, NULL}
};

void check_clock()
{
    int i;
    
    i = 0;
    while(clock_source[i].name != NULL) {
	struct timespec res;
	clock_getres(clock_source[i].id, &res);
	printf("res: %s: %lds, %ldns\n", clock_source[i].name,
	       res.tv_sec, res.tv_nsec);
	i++;
    }
    
    i = 0;
    while(clock_source[i].name != NULL) {
	struct timespec tim;
	clock_gettime(clock_source[i].id, &tim);
	printf("time: %s: %lds, %ldns\n",  clock_source[i].name,
	       tim.tv_sec, tim.tv_nsec);
	i++;
    }
}


int main(int argc, char** argv)
{
    int fd;
    int c;
    int i,j;
    useconds_t delay = 0;
    char* opt_layout = NULL;
    uint32_t h=18, v=8;
    void* node_mem;
    uint8_t* np_mem;
    size_t alloc_size;
    int interactive = 0;
    char* filename = NULL;
    int file_fd = -1;
    uint18_t id = 999;
    int noexec = 0;
    int baud = 9600;
    
    g_page_size = sysconf(_SC_PAGESIZE);  // must be first!
    g_flags = 0;

    check_clock();
    
    while((c = getopt(argc, argv, "ivtnPl:b:d:I:D:f:G")) != -1) {
	switch(c) {
	case 'i': interactive = 1; break;
	case 'n': noexec = 1; break;
	case 'f': filename = optarg; break;
	case 'v': g_flags |= FLAG_VERBOSE; break;
	case 't': g_flags |= FLAG_TRACE; break;
	case 'P': g_flags |= FLAG_GPIO_POLL; break;  // GPIO poll mode (no wakeup wait)
	case 'l': opt_layout = optarg; break;
	case 'G': g_flags |= FLAG_DEBUG_ENABLE; break;
	case 'd': {
	    char* endptr = NULL;
	    if ((delay = strtol(optarg, &endptr,0)) == 0) {
		if (endptr && (*endptr != '\0'))
		    usage(basename(argv[0]));
	    }
	    break;
	}
	case 'I':
	    id = atoi(optarg);
	    g_step_spec = optarg;  // Also save for debugger step nodes
	    break;
	case 'b':
	    baud = atoi(optarg);
	    break;
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
		else if (strncmp(ptr, "rom", 3) == 0) {
		    if ((ptr[3] == '\0') || (ptr[3] == ',')) {
			g_flags |= FLAG_DUMP_ROM;
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

    if (id == 888) {   // draw comm map
	draw_com_map();
	exit(0);
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

    g_v = v;
    g_h = h;
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
	    f18_rom_type_t rt;

	    memset(np, 0, sizeof(reg_node_t));
	    
	    node[i][j]= (node_t*) np;
	    // printf("node[%d][%02d] = %p\n", i, j, np);
	    rt = RomTypeMap[i][j];
	    np->n.rom_type = rt;
	    np->n.rom = RomMap[rt].addr;
	    np->n.id = MAKE_ID(i,j);
	    if ((np->n.id == 708) && (rt == async_boot)) {
		char pty_name[256];
		int master, slave;
		
		memset(&r708, 0, sizeof(r708));
		memset(&w708, 0, sizeof(w708));

		f18_chan_init(&r708.chan);
		f18_chan_init(&w708.chan);
		
		if ((master = open_pty(pty_name, sizeof(pty_name))) < 0) {
		    fprintf(stderr, "unable to open a pty error=%s (%d)\n",
			    strerror(errno), errno);
		    exit(1);
		}
		printf("PTY_NAME=%s\n", pty_name);
		// open a slave and keep it open so we avoid EIO from
		// linux clients
		slave = open(pty_name, O_RDWR | O_NOCTTY);
		(void) slave;

		r708.fd = master;
		r708.baud = baud;
		w708.fd = STDOUT_FILENO;  // Write to stdout for testing
		w708.baud = baud;		
	    }

	    np->dmask = 0;
	    np->imask = 0;
	    
	    f18_chan_init(&np->chan);

	    np->neighbour[0] = NULL;
	    np->neighbour[1] = NULL;
	    np->neighbour[2] = NULL;
	    np->neighbour[3] = NULL;
	    np->neighbour[4] = NULL;	    

	    np->n.ior    = IMASK;  // default read value
	    np->n.iow    = 0;      // write cache
	    if (id == 999)
		np->n.flags  = g_flags;
	    else if (np->n.id == id)
		np->n.flags  = g_flags; // specific for node?

	    np->n.delay  = delay;    // specific for node?

	    np->n.reg.p = ConfigMap[i][j].reset;
	    np->n.io_addr = ConfigMap[i][j].io_addr;
	    np->dmask = dirbits(i,j,ConfigMap[i][j].comm);
	    np->imask = (ConfigMap[i][j].io_addr ?
			 dirbits(i,j,ConfigMap[i][j].io_addr) : 0);
	    
	    np->n.reg.b = IOREG_IO;
	    np->n.read_ioreg  = f18_read_ioreg;
	    np->n.write_ioreg = f18_write_ioreg;

	    np_mem += (PAGE(NODE_SIZE));
	}
    }

    // lets load node if -f was given
    if (file_fd >= 0) {
	uint18_t nid = 0xfff;
	uint18_t addr = 0;
	node_t* np = NULL;
	f18_symbol_table_t symtab;
	uint8_t heap[MAX_SCAN_HEAP_SIZE];
	int line = 0;
	char linebuf[MAX_LINE_LEN+1];
	int r;

	// temporary symbol table memory used while loading node
	INIT_SYMTAB(&symtab, heap, MAX_SCAN_HEAP_SIZE);

	while((r = f18_scan_line(file_fd,&line,linebuf,sizeof(linebuf),
				 &addr, &nid, np->ram, &symtab)) >= 0) {
	    switch(r) {
	    case META_NODE:
		if (np != NULL) { // find main in symtab
		    if ((i = find_symbol_by_name("main", &symtab)) >= 0)
			np->reg.p = symtab.symbol[i].value;
		    // printf("set p = %03x\n", np->reg.p);
		    np->symtab = copy_symbols(&symtab);
		}
		// reinitialize
		INIT_SYMTAB(&symtab, heap, MAX_SCAN_HEAP_SIZE);
		np = node[ID_TO_ROW(nid)][ID_TO_COLUMN(nid)];
		addr = 0;
		break;
	    case META_ORG:
		// printf("load: set org=%03x\n", addr);
		break;
	    default:
		// printf("load: %03d ram[%03x]=%06x\n", nid, addr, data);
		addr++;
	    }
	}
	if (r < 0) {
	    switch(r) {
	    case -1:
		fprintf(stderr, "%s:%d: syntax error: %s\n",
			filename, line, linebuf);
		break;
	    case -2:
	    case -3:
		break;
	    }
	}
	if (np != NULL) { // find main in symtab
	    if ((i = find_symbol_by_name("main", &symtab)) >= 0) {
		np->reg.p = symtab.symbol[i].value;
		// printf("set p = %03x\n", np->reg.p);
	    }
	    np->symtab = copy_symbols(&symtab);
	}
    }

    // init neighbours channels
    for (i=0; i < v; i++) {
	for (j = 0; j < h; j++) {
	    reg_node_t* np = (reg_node_t*) node[i][j];

	    if (i < v-1)
		np->neighbour[UP]   = &((reg_node_t*)node[i+1][j])->chan;
	    else if (i == v-1) {
		if (np->n.id == 708) {
		    np->neighbour[UP] = &r708.chan;    // read async
		    r708.out = &np->chan;              //
		    r708.n.id = 808;
		    np->ioc = &w708.chan;              // io control channel
		    w708.n.id = 908;
		    w708.in = &np->chan;              // write from 708
		}
	    }
	    if (i > 0)
		np->neighbour[DOWN] = &((reg_node_t*)node[i-1][j])->chan;
	    if (j > 0)
		np->neighbour[LEFT] = &((reg_node_t*)node[i][j-1])->chan;
	    if (j < h-1)
		np->neighbour[RIGHT] = &((reg_node_t*)node[i][j+1])->chan;

	    // FIXME add io channels !
	}
    }

    if (id != 999) {
	int i = ID_TO_ROW(id);
	int j = ID_TO_COLUMN(id);
	reg_node_t* np = (reg_node_t*) node[i][j];

	if (g_flags & FLAG_DUMP_ROM) {
	    f18_rom_type_t rt;
	    i = ID_TO_ROW(id);
	    j = ID_TO_COLUMN(id);
	    rt = ConfigMap[i][j].rom;
	    
	    printf("Dump ROM\n");
	    printf("  type=\"%s\"\n", RomMap[rt].name);
	    printf("  version=\"%s\n", RomMap[rt].vers);
	    printf("  size=%ld\n", RomMap[rt].size);
	    printf("  io_type=%d\n", ConfigMap[i][j].io_type);
	    printf("  io_addr=%03x\n", ConfigMap[i][j].io_addr);
	    printf("  comm=%03x\n", ConfigMap[i][j].comm);
	    printf("  reset=%03x\n", ConfigMap[i][j].reset);
	    
	    f18_disasm(RomMap[rt].addr, SymTabMap[i][j], ROM_START,
		       RomMap[rt].size);
	}
	if (g_flags & FLAG_DUMP_RAM) {
	    f18_disasm(np->n.ram, np->n.symtab, RAM_START, (RAM_END-RAM_START)+1);
	}
    }
    if (noexec)
	exit(0);

    // Initialize debugger if -G flag set
    if (g_flags & FLAG_DEBUG_ENABLE) {
	debug_init();
	if (g_step_spec)
	    debug_parse_step_nodes(g_step_spec);
	// Set debugger flag on step nodes
	for (i = 0; i < (int)v; i++) {
	    for (j = 0; j < (int)h; j++) {
		reg_node_t* np = (reg_node_t*) node[i][j];
		if (debug_is_step_node(np->n.id))
		    np->n.flags |= FLAG_DEBUG_ENABLE;
	    }
	}
    }

    // Benchmark emulator speed using node[0][0]
//    g_emu_speed = f18_benchmark(node[0][0], 262143);  // 0x3FFFF iterations
//    printf("Emulator speed: %.2f MIPS\n", g_emu_speed);

    // Set num_active before creating threads to avoid race where main
    // thread checks the termination condition before threads have started
    // Check if node 708 exists (row 7, col 8) before including async threads
    int has_node_708 = (v > 7 && h > 8 && node[7][8] != NULL);
    num_active = v * h + (has_node_708 ? 2 : 0);

    // Start async I/O threads only if node 708 exists
    if (has_node_708) {
	pthread_attr_init(&r708.attr);
	pthread_attr_setstacksize(&r708.attr, PAGE(STACK_SIZE));
	if (pthread_create(&r708.thread,&r708.attr,async_reader_start,
			   (void*) &r708) <0) {
	    perror("pthread_create");
	    exit(1);
	}

	pthread_attr_init(&w708.attr);
	pthread_attr_setstacksize(&w708.attr, PAGE(STACK_SIZE));
	if (pthread_create(&w708.thread,&w708.attr,async_writer_start,
			   (void*)&w708) <0) {
	    perror("pthread_create");
	    exit(1);
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
		// fprintf(stderr, "thread stack size: %ld\n", size);
	    }
	    VERBOSE(np, "about to start node%s\n", "");
	    if (pthread_create(&np->thread,&np->attr,f18_emu_start,(void*) np) <0) {
		perror("pthread_create"); 
		exit(1);
	    }
	}
    }

    // Wait until no threads are active and none are waiting on external I/O
    if (g_flags & FLAG_DEBUG_ENABLE) {
	// Run debugger TUI main loop
	// extern void debug_tui_main(void);
	debug_tui_main();
    } else {
	pthread_mutex_lock(&sys_lock);
	while (num_active > 0 || num_blocked_ext > 0)
	    pthread_cond_wait(&sys_cond, &sys_lock);
	pthread_mutex_unlock(&sys_lock);
    }

    // Signal all nodes to terminate and wake blocked threads
    for (i = 0; i < (int)g_v; i++) {
	for (j = 0; j < (int)g_h; j++) {
	    reg_node_t* np = (reg_node_t*) node[i][j];

	    np->n.flags |= FLAG_TERMINATE;  // emulator loop
	    f18_chan_terminate(&np->chan);  // signal termination
	}
    }
    // Terminate and join async threads only if node 708 exists
    if (g_v > 7 && g_h > 8 && node[7][8] != NULL) {
	f18_chan_terminate(&r708.chan);
	f18_chan_terminate(&w708.chan);
    }

    // Join all threads
    for (i = 0; i < (int)g_v; i++)
	for (j = 0; j < (int)g_h; j++)
	    pthread_join(((reg_node_t*)node[i][j])->thread, NULL);

    if (g_v > 7 && g_h > 8 && node[7][8] != NULL) {
	pthread_join(r708.thread, NULL);
	pthread_join(w708.thread, NULL);
    }

    if (g_flags & FLAG_DEBUG_ENABLE)
	debug_cleanup();

    if (tty_fd >= 0)
	tty_reset(tty_fd);
    exit(0);
}
