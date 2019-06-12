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
} node_data_t;

node_data_t node[8][18];

int select_ports(node_t* np, uint18_t ioreg, int is_input,
		 int* fds, uint18_t* io_rd, uint18_t* io_wr)
{
    if ((ioreg < IOREG_START) || (ioreg > IOREG_END))
	return 0;
    if (io_rd) io_rd[0] = 0;
    if (io_wr) io_wr[0] = 0;
    switch(ioreg) {
    case IOREG_STDIO:
	if ((fds[0] = is_input ? np->stdin_fd : np->stdout_fd) < 0)
	    return 0;
	return 1;
    case IOREG_STDIN:
	if (!is_input || (np->stdin_fd < 0))
	    return 0;
	fds[0] = np->stdin_fd;
	return 1;
    case IOREG_STDOUT:
	if (is_input || (np->stdout_fd < 0))
	    return 0;
	fds[0] = np->stdout_fd;
	return 1;
    case IOREG_TTY:
	if (np->tty_fd < 0)
	    return 0;
	fds[0] = np->tty_fd;
	return 1;
    default: {
	int i = 0;
	if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	    if ((ioreg & F18_RIGHT_BIT) && (np->right_fd >= 0)) {
		fds[i] = np->right_fd;
		if (io_rd) io_rd[i] = F18_IO_RIGHT_RD;
		if (io_wr) io_wr[i] = F18_IO_RIGHT_WR;
		i++;
	    }
	    if (!(ioreg & F18_DOWN_BIT) && (np->down_fd >= 0)) {
		fds[i] = np->down_fd;
		if (io_rd) io_rd[i] = F18_IO_DOWN_RD;
		if (io_wr) io_wr[i] = F18_IO_DOWN_WR;
		i++;
	    }
	    if ((ioreg & F18_LEFT_BIT) && (np->left_fd >= 0)) {
		fds[i] = np->left_fd;
		if (io_rd) io_rd[i] = F18_IO_LEFT_RD;
		if (io_wr) io_wr[i] = F18_IO_LEFT_WR;
		i++;
	    }
	    if (!(ioreg & F18_UP_BIT) && (np->up_fd >= 0)) {
		fds[i] = np->up_fd;
		if (io_rd) io_rd[i] = F18_IO_UP_RD;
		if (io_wr) io_wr[i] = F18_IO_UP_WR;
		i++;
	    }
	}
	return i;
    }
    }
}

static void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    uint18_t io_wr[4];
    int fds[4];
    int nports;
    int i, r;

    if ((nports = select_ports(np, ioreg, 0, fds, NULL, io_wr)) == 0) {
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
	char buf[8];
	int  len;
	uint18_t dd = io_wr[i];
	int fd = fds[i];

	len = snprintf(buf, sizeof(buf), "%05x\n", value);
	if (len >= sizeof(buf)) {
	    fprintf(stderr, "warning: output truncated\n");
	    len = sizeof(buf)-1;
	}

	switch(dd) {
	case 0:
	    r = write(fd, buf, len);
	    break;
	case F18_IO_RIGHT_WR:
	    if (np->flags & FLAG_WR_BIN_RIGHT)
		r = write(fd, &value, sizeof(value));
	    else
		r = write(fd, buf, len);
	    break;
	case F18_IO_DOWN_WR:
	    if (np->flags & FLAG_WR_BIN_DOWN)
		r = write(fd, &value, sizeof(value));
	    else
		r = write(fd, buf, len);
	    break;
	case F18_IO_LEFT_WR:
	    if (np->flags & FLAG_WR_BIN_LEFT)
		r = write(fd, &value, sizeof(value));
	    else
		r = write(fd, buf, len);
	    break;
	case F18_IO_UP_WR:
	    if (np->flags & FLAG_WR_BIN_UP)
		r = write(fd, &value, sizeof(value));
	    else
		r = write(fd, buf, len);
	    break;
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



int parse_mnemonic(char* word, int n)
{
    int i;
    for (i=0; i<32; i++) {
	int len = strlen(f18_ins_name[i]);
	if ((n == len) && (memcmp(word, f18_ins_name[i], n) == 0))
	    return i;
    }
    return -1;
}

struct {
    char* name;
    uint18_t value;
} symbol[] =  {
    { "stdio",   IOREG_STDIO },
    { "stdin",   IOREG_STDIN },
    { "stdout",  IOREG_STDOUT },
    { "tty",     IOREG_TTY },

    { "io",      IOREG_IO },
    { "data",    IOREG_DATA },
    { "---u",    IOREG____U },
    { "--l-",    IOREG___L_ },
    { "--lu",    IOREG___LU },
    { "-d--",    IOREG__D__ },
    { "-d--u",   IOREG__D_U },
    { "-dl-",    IOREG__DL_ },
    { "-dlu",    IOREG__DLU },
    { "r---",    IOREG_R___ },
    { "r--u",    IOREG_R__U },
    { "r-l-",    IOREG_R_L_ },
    { "r-lu",    IOREG_R_LU },
    { "rd--",    IOREG_RD__ },
    { "rd-u",    IOREG_RD_U },
    { "rdl-",    IOREG_RDL_ },
    { "rdlu",    IOREG_RDLU },
    { NULL, 0 }
};

int parse_symbol(char** pptr, uint18_t* valuep)
{
    int i = 0;
    char*ptr = *pptr;

    while(symbol[i].name != NULL) {
	int n = strlen(symbol[i].name);
	if (strncmp(ptr, symbol[i].name, n) == 0) {
	    if ((ptr[n] == '\0') || isblank(ptr[n])) {
		*pptr = ptr + n;
		*valuep = symbol[i].value;
		return 1;
	    }
	}
	i++;
    }
    return 0;
}

#define TOKEN_ERROR     -1
#define TOKEN_EMPTY     0
#define TOKEN_MNEMONIC1 1
#define TOKEN_MNEMONIC2 2
#define TOKEN_VALUE     3

//
// parse:
//   
//   ( '(' .* ')' )* <mnemonic>
//   ( '(' .* ')' )* <mnemonic>':'<dest>
//   ( '(' .* ')' )* <hex>
//   ( '(' .* ')' )* \<blank> .*
//

int parse_ins(char** pptr, uint18_t* insp, uint18_t* dstp)
{
    char* ptr = *pptr;
    char* word;
    uint18_t value = 0;
    int n = 0;
    int ins;
    int has_dest = 0;

    while(isblank(*ptr) || (*ptr == '(')) {
	while(isblank(*ptr)) ptr++;
	if (*ptr == '(') {
	    ptr++;
	    while(*ptr && (*ptr != ')'))
		ptr++;
	    if (*ptr) ptr++;
	}
    }

    if ( (*ptr == '\\') && (isblank(*(ptr+1)) || (*(ptr+1)=='\0')) ) {
	while(*ptr != '\0') ptr++;  // skip rest
    }
    word = ptr;
    // fprintf(stderr, "WORD [%s]", word);
    while (*ptr && !isblank(*ptr) && (*ptr != ':')) { ptr++; n++; }
    if (n == 0) return TOKEN_EMPTY;
    // first check mnemonic
    ins = parse_mnemonic(word, n);
    // check reset is destination
    switch(ins) {
    case -1:
	has_dest = 0;
	ptr = word;
	break;
    case INS_PJUMP:
    case INS_PCALL:
    case INS_NEXT:
    case INS_IF:
    case INS_MINUS_IF:
	if (*ptr == ':') { // force?
	    has_dest = 1;
	    ptr++;
	}
	break;
    default:
	has_dest = 0;
	break;
    }

    // parse number or dest
    if (parse_symbol(&ptr, &value))
	n = 1;
    else {
	n = 0;
	while(isxdigit(*ptr)) {
	    value <<= 4;
	    if ((*ptr >= '0') && (*ptr <= '9'))
		value += (*ptr-'0');
	    else if ((*ptr >= 'A') && (*ptr <= 'F'))
		value += ((*ptr-'A')+10);
	    else
		value += ((*ptr-'a')+10);
	    ptr++;
	    n++;
	}
    }
    *pptr = ptr;
    if (ins >= 0) {
	*insp = ins;
	*dstp = value;
	if (has_dest && (n > 0))
	    return TOKEN_MNEMONIC2;
	return TOKEN_MNEMONIC1;
    }
    if ((n == 0) || !(isblank(*ptr) || (*ptr=='\0'))  )
	return TOKEN_ERROR;
    *insp = value;
    return TOKEN_VALUE;
}



static uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
{
    uint18_t io_rd[4];
    uint18_t dd = 0;
    int fds[4];
    int fd;
    fd_set readfds;
    int nfds;
    char buf[256];
    char* ptr;
    uint18_t ins;
    uint18_t insx;
    uint18_t dest;
    uint18_t value;
    int nports;
    int i;
    int r;

    if (ioreg == IOREG_IO) {
	uint18_t io_wr[4];
	fd_set writefds;
	struct timeval tm = {0, 0};

	if ((nports = select_ports(np, IOREG_RDLU, 1, fds, io_rd, io_wr))==0)
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
	return value;
    }

    if ((nports = select_ports(np, ioreg, 1, fds, io_rd, NULL)) == 0) {
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
    // read line from fd until '\n' is found or buffer overflow
    // VERBOSE(np,"read line fd=%d\n", fd);
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

    // Interpreter mode channel
    i = 0;
    while(i < (sizeof(buf)-1)) {
	r = read(fd, &buf[i], 1);
	if (r == 0) {  // input stream has closed
	    if (fd == 0)  // it was stdin !
		np->flags |= FLAG_TERMINATE;  // lets terminate
	    break;
	}
	else if (r < 0)
	    goto error;
	else if (buf[i] == '\n')
	    break;
	i++;
    }
    buf[i] = '\0';
    VERBOSE(np,"read fd=%d [%s]\n", fd, buf);
    ptr = buf;
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY: goto again;
    case TOKEN_MNEMONIC1: ins = (insx << 13); break;
    case TOKEN_MNEMONIC2:
	// instruction part is encoded (^IMASK) but dest is not (why)
	ins = (insx << 13) ^ IMASK;                // encode instruction
	ins = (ins & ~MASK10) | (dest & MASK10);  // set address bits
	return ins;
    case  TOKEN_VALUE:
	return (insx & MASK18);  // value not encoded
    default: goto error;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY: // assume rest of opcode are nops (warn?)
	ins = (ins | (INS_NOP<<8) | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	return ins;
    case TOKEN_MNEMONIC1: ins |= (insx  << 8); break;
    case TOKEN_MNEMONIC2:
	ins = (ins | (insx << 8)) ^ IMASK;      // encode instruction
	ins = (ins & ~MASK8) | (dest & MASK8);  // set address bits
	return ins;
    default: goto error;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP<<3) | (INS_NOP>>2)) ^ IMASK;
	return ins;
    case TOKEN_MNEMONIC1: ins |= (insx << 3); break;
    case TOKEN_MNEMONIC2:
	ins = (ins | (insx << 3)) ^ IMASK;      // encode instruction
	ins = (ins & ~MASK3) | (dest & MASK3);  // set address bits
	return ins;
    default: goto error;
    }
    i = parse_ins(&ptr, &insx, &dest);
    switch(i) {
    case TOKEN_EMPTY:
	ins = (ins | (INS_NOP>>2)) ^ IMASK;
	return ins;
    case TOKEN_MNEMONIC1:
	if ((insx & 3) != 0)
	    fprintf(stderr, "scan error: bad slot3 instruction used %s\n",
		    f18_ins_name[insx]);
	ins = (ins | (insx >> 2)) ^ IMASK; // add op and encode
	return ins;
    default: goto error;
    }

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
	    "    -D regs,ram,r,s  Dump registers,ram,return-stack,data-stack\n"
	    "    -d <delay>  Set delay between instructions (in usecs)\n"
	    "    -l VxH      Set processor mesh layout (max 8x18)\n");
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
//  -v verbose   (if debug compiled)
//  -t trace     (if debug comipled)
//  -d delay     Set delay between instructions
//  -l VxH       processor layout (max 8x18)
//

int main(int argc, char** argv)
{
    node_t n;
    int fd;
    int c;
    int i,j;
    useconds_t delay = 0;
    char* opt_layout = NULL;
    uint32_t h=1, v=1;
    void* node_mem;
    uint8_t* np_mem;
    size_t alloc_size;

    g_page_size = sysconf(_SC_PAGESIZE);  // must be first!
    g_flags = 0;
    
    while((c=getopt(argc, argv, "vtl:d:D:")) != -1) {
	switch(c) {
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
	    // dp->stack = (void*) (np_mem + PAGE(NODE_SIZE));

	    memset(np, 0, sizeof(node_t));
	    np->flags      = g_flags;  // specific for node?
	    np->delay      = delay;    // specific for node?
	    np->up_fd      = -1;
	    np->left_fd    = -1;
	    np->down_fd    = -1;
	    np->right_fd   = -1;
	    np->tty_fd     = tty_fd;
	    np->stdin_fd   = 0;
	    np->stdout_fd  = 1;
	    if ((i == 0) && (j == 0))
		np->p = IOREG_STDIO;
	    else
		np->p = IOREG_RDLU;
	    np->b = IOREG_TTY;
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
		node[i][j-1].np->right_fd = lr[0];
		node[i][j].np->left_fd = lr[1];
	    }
	    if (i > 0) {  // create up/down
		int ud[2];
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, ud) < 0) {
		    perror("socketpair ud");
		    exit(1);
		}
		node[i-1][j].np->down_fd = ud[0];
		node[i][j].np->up_fd = ud[1];
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

    while(1) {
	// printf("Z\n");
	sleep(10);
    }
    tty_reset(tty_fd); // atexit?
    exit(0);
}
