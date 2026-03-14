//
//  F18 channel
//
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <ctype.h>
#include <memory.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>

#include "f18.h"
#include "f18_scan.h"
#include "f18_node.h"
#include "f18_debug.h"

extern node_t* node[8][18];
extern async_reader_t r708;
extern async_writer_t w708;

// Invert direction: UP<->DOWN, LEFT<->RIGHT
static const uint18_t invert_dir[5] = { DOWN, RIGHT, UP, LEFT, GPIO };

#define READ  1
#define WRITE 2

void f18_chan_init(chan_t* chan)
{
    pthread_mutex_init(&chan->lock, NULL);
    pthread_cond_init(&chan->cond, NULL);
    chan->wmask = 0;
    chan->rmask = 0;
    chan->data = 0;
    chan->completed = 0;
    chan->wait = 0;
    chan->terminate = 0;        
}

void f18_chan_terminate(chan_t* chan)
{
    pthread_mutex_lock(&chan->lock);
    chan->terminate = 1;
    pthread_cond_broadcast(&chan->cond);
    pthread_mutex_unlock(&chan->lock);
}

// Decode ioreg into DIR_BIT mask of target directions,
// filtered by available directions (dmask).
static uint18_t select_dirs(node_t* np, uint18_t ioreg)
{
    uint18_t dirs = 0;
    if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	uint18_t dmask = ((reg_node_t*)np)->dmask;
	uint18_t iodir  = dirbits(ID_TO_ROW(np->id),
				  ID_TO_COLUMN(np->id),
				  ioreg);
	dirs = (iodir & dmask);
    }
    return dirs;
}

// update ior status for node "owning" channel 'chan'
// Uses atomic update for speed, signals condition only if PIN17 changes
static void set_ior(chan_t* chan, uint18_t mask)
{
    reg_node_t* np = chan_to_reg_node(chan);
    uint18_t old_ior;

    // Atomic fetch-and-or for fast update
    old_ior = __atomic_fetch_or(&np->n.ior, mask, __ATOMIC_SEQ_CST);

    // Signal condition only if PIN17 changed LOW→HIGH (for wakeup)
    if ((mask & F18_IO_PIN17) && !(old_ior & F18_IO_PIN17)) {
        pthread_mutex_lock(&chan->lock);
        pthread_cond_broadcast(&chan->cond);
        pthread_mutex_unlock(&chan->lock);
    }
}

static void clr_ior(chan_t* chan, uint18_t mask)
{
    reg_node_t* np = chan_to_reg_node(chan);
    uint18_t old_ior;

    // Atomic fetch-and-and for fast update
    old_ior = __atomic_fetch_and(&np->n.ior, ~mask, __ATOMIC_SEQ_CST);

    // Signal condition only if PIN17 changed HIGH→LOW (for wakeup with WD=1)
    if ((mask & F18_IO_PIN17) && (old_ior & F18_IO_PIN17)) {
        pthread_mutex_lock(&chan->lock);
        pthread_cond_broadcast(&chan->cond);
        pthread_mutex_unlock(&chan->lock);
    }
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

int f18_chan_write(chan_t* chan, uint18_t dir, uint18_t value)
{
    dir = invert_dir[dir];

    pthread_mutex_lock(&chan->lock);

    if (chan->rmask & DIR_BIT(dir)) {
	chan->data = value;
	chan->rmask = 0;          // first writer wins, clear all
	chan->completed = 1;
	pthread_cond_signal(&chan->cond);
	pthread_mutex_unlock(&chan->lock);
	return 1;
    }
    pthread_mutex_unlock(&chan->lock);
    return 0;
}

int f18_chan_read(chan_t* chan, uint18_t dir, uint18_t* value_ptr)
{
    dir = invert_dir[dir];

    pthread_mutex_lock(&chan->lock);

    if (chan->wmask & DIR_BIT(dir)) {
	// Writer is waiting to send in our direction - grab data!
	*value_ptr = chan->data;
	PRINTF("[%03x] chan_read: got value=%d from chan, dir=%d, wmask was %x\n", chan_to_reg_node(chan)->n.id, chan->data, dir, chan->wmask);
	chan->wmask = 0;          // first reader wins, clear all
	chan->completed = 1;
	pthread_cond_signal(&chan->cond);
	pthread_mutex_unlock(&chan->lock);
	return 1;
    }
    pthread_mutex_unlock(&chan->lock);
    return 0;
}

// initialize transfer of read/write
void f18_init_transfer(chan_t* chan, int rw,
		       uint18_t rdirs, uint18_t wdirs,
		       uint18_t value)
{
    pthread_mutex_lock(&chan->lock);
    if (rw & READ)
	chan->rmask = rdirs;
    if (rw & WRITE) {
	chan->data = value;
	chan->wmask = wdirs;
    }
    chan->completed = 0;
    pthread_mutex_unlock(&chan->lock);
}

void f18_complete_transfer(chan_t* chan, int rw)
{
    pthread_mutex_lock(&chan->lock);
    if (rw & READ)
	chan->rmask = 0;
    if (rw & WRITE)
	chan->wmask = 0;
    chan->completed = 1;
    pthread_mutex_unlock(&chan->lock);
}

// wait for a reader/writer to find us and complete the transfer    

uint18_t f18_wait_transfer(chan_t* chan, int rw)
{
    uint18_t value = 0;
    sys_enter_blocked_port();
    pthread_mutex_lock(&chan->lock);
    chan->wait = 1;

    while (!chan->completed && !chan->terminate)
	pthread_cond_wait(&chan->cond, &chan->lock);
    chan->wait = 0;
    if (rw & READ) {
	chan->rmask = 0;
	value = chan->data;
    }
    if (rw & WRITE) {
	chan->wmask = 0;
    }
    pthread_mutex_unlock(&chan->lock);
    sys_leave_blocked_port();
    return value;
}

void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    reg_node_t* dp = (reg_node_t*) np;
    chan_t* rp;    
    uint18_t dirs;
    int dir;

    if ((ioreg < IOREG_START) || (ioreg > IOREG_END)) {
	ERRORF("[%03d]: io error when writing ioreg=%x, not mapped\n",
	       np->id, ioreg);
	return;
    }

    if (ioreg == IOREG_IO) {
	// write pins 17,5,3,1, WD, phan 9,7
	if (dp->ioc != NULL) {
	    if (f18_chan_write(dp->ioc, GPIO, value))
		;
	    else {
		f18_init_transfer(&dp->chan, WRITE, 0, DIR_BIT(GPIO), value);
		if (f18_chan_write(dp->ioc, GPIO, value)) {
		    f18_complete_transfer(&dp->chan, WRITE);
		}
		else {
		    f18_wait_transfer(&dp->chan, WRITE);
		}
	    }
	}
	else {
	    np->iow = value;
	}
	return;
    }

    dirs = select_dirs(np, ioreg);
    
    if (dirs == 0) {
	ERRORF("[%03d]: io error when writing ioreg=%x, no directions\n",
	       np->id, ioreg);
	return;
    }

    // try to find a reader already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir)))
	    continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	if (f18_chan_write(rp, dir, value))
	    return;
    }

    // setup for transfer to dirs
    f18_init_transfer(&dp->chan, WRITE, 0, dirs, value);

    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir)))
	    continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	if (f18_chan_write(rp, dir, value)) {
	    f18_complete_transfer(&dp->chan, WRITE);
	    return;
	}
    }

    // Phase 3: wait for a reader to find us and complete the transfer
    dp->debug.blocked_addr = ioreg;
    dp->debug.blocked_dir = 1;  // write
    f18_wait_transfer(&dp->chan, WRITE);
    dp->debug.blocked_addr = 0;
}

// check neighbours and pins and update io read mask
uint18_t read_io(reg_node_t* dp)
{
    uint18_t status;
    uint18_t mask;
    uint32_t old_val, new_val;    
    uint18_t dirs;
    int dir;
    chan_t* rp;

    dirs = dp->dmask; // select_dirs((node_t*)dp, IOREG_RDLU);

    // Fast path: no port neighbors, just GPIO - skip mutex locks entirely
    if (dirs == 0)
	return __atomic_load_n(&dp->n.ior, __ATOMIC_SEQ_CST);

    status = mask = 0;
    for (dir = 0; dir < 4; dir++) {
	uint18_t idir;
	if (!(dirs & DIR_BIT(dir))) continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	idir = invert_dir[dir];
	pthread_mutex_lock(&rp->lock);
	if (rp->wmask & DIR_BIT(idir)) {   // neighbour is writing to us
	    status |= F18_IO_DIR_WR(dir);  // Xw=1 (active high)
	    mask   |= F18_IO_DIR_WR(dir);
	}
	if ((rp->rmask & DIR_BIT(idir))) { // neighbour is reading from us
	    mask   |= F18_IO_DIR_RD(dir);  // Xr-=1 (idle)	    
	}
	pthread_mutex_unlock(&rp->lock);
    }

    // Atomic read-modify-write using compare-and-swap loop
    if (mask) {
        do {
            old_val = __atomic_load_n(&dp->n.ior, __ATOMIC_SEQ_CST);
            new_val = (old_val & ~mask) | (status & mask);
        } while (!__atomic_compare_exchange_n(&dp->n.ior, &old_val, new_val,
                                              0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
	return new_val;
    }
    return __atomic_load_n(&dp->n.ior, __ATOMIC_SEQ_CST);
}

uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
{
    reg_node_t* dp = (reg_node_t*) np;
    chan_t* rp;    
    uint18_t dirs;
    uint18_t value;
    uint18_t iodir;
    int dir;

    if ((ioreg < IOREG_START) || (ioreg > IOREG_END)) {
	ERRORF("[%03d]: io error when reading ioreg=%x, not mapped\n",
	       np->id, ioreg);
	return 0;
    }

    if (ioreg == IOREG_IO)
	return read_io(dp);

    dirs = select_dirs(np, ioreg);
    iodir  = np->io_addr ?
	dirbits(ID_TO_ROW(np->id),ID_TO_COLUMN(np->id),np->io_addr) : 0;
    dirs |= iodir;

    VERBOSE(np, "read_ioreg addr=%03x dirs=%c%c%c%c\n",
	    ioreg,
	    (dirs & DIR_BIT(RIGHT) ? 'R' : '_'),
	    (dirs & DIR_BIT(DOWN)  ? 'D' : '_'),
	    (dirs & DIR_BIT(LEFT)  ? 'L' : '_'),
	    (dirs & DIR_BIT(UP)    ? 'U' : '_'));
    if (dirs == 0) {
	ERRORF("[%03d]: io error when reading ioreg=%x, no directions\n",
	      np->id, ioreg);
	return 0;
    }

    // IOREG_DATA (x141) / IOREG_LDATA (x171): no handshake, return IOR immediately
    if (ioreg == IOREG_DATA || ioreg == IOREG_LDATA)
	return read_io(dp);

    // GPIO wakeup read: suspend until PIN17 matches WD state
    // WD=0 (reset): wait until PIN17 is HIGH
    // WD=1: wait until PIN17 is LOW
    // Applies to ANY read that includes the GPIO direction
    // FLAG_GPIO_POLL: skip wait, return IOR immediately (for poll loops)
    if (iodir && (dirs & iodir)) {
	if (np->flags & FLAG_GPIO_POLL) {
	    // Poll mode: return current IOR immediately
	    return read_io(dp);
	}
	int wd_state = (np->iow & F18_IO_WD) ? 1 : 0;  // WD bit as written
	int want_high = !wd_state;  // WD=0 means wait for HIGH

	pthread_mutex_lock(&dp->chan.lock);
	while (!(np->flags & FLAG_TERMINATE)) {
	    uint32_t ior_val = __atomic_load_n(&np->ior, __ATOMIC_SEQ_CST);
	    int pin17_high = (ior_val & F18_IO_PIN17) ? 1 : 0;
	    if (pin17_high == want_high) {
		// PIN17 matches desired state - wakeup!
		pthread_mutex_unlock(&dp->chan.lock);
		return ior_val;  // Return IOR (caller should drop this)
	    }
	    // Wait for PIN17 to change
	    pthread_cond_wait(&dp->chan.cond, &dp->chan.lock);
	}
	pthread_mutex_unlock(&dp->chan.lock);
	return 0;
    }

    // Phase 1: probe - try to find a writer already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir)))
	    continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	if (f18_chan_read(rp, dir, &value))
	    return value;
    }

    f18_init_transfer(&dp->chan, READ, dirs, 0, 0);

    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir)))
	    continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	if (f18_chan_read(rp, dir, &value)) {
	    f18_complete_transfer(&dp->chan, READ);
	    return value;
	}
    }

    dp->debug.blocked_addr = ioreg;
    dp->debug.blocked_dir = 0;  // read
    value = f18_wait_transfer(&dp->chan, READ);
    dp->debug.blocked_addr = 0;

    if (np->flags & FLAG_TERMINATE)
	return 0;
    return value;
}

/* main program for
 * read/write async serial data
 */

// #define BAUD       300
#define NDATABITS  8
#define NSTOPBITS  1
#define UDELAY(B) ((unsigned long)((1/((double)(B)))*1000000))

char digit[] = "0123456789abcdef";
char* ltoa(long value, char* ptr, size_t minlen, size_t maxlen, int radix)
{
    char* end_ptr;
    int sign = 0;
    int len = minlen;

    if (radix > 16)
	return NULL;
    end_ptr = ptr;
    ptr = ptr + maxlen - 1;
    *ptr = '\0';
    if (value < 0) {
	sign = 1;
	value = -value;
    }
    if (ptr > end_ptr) {
	do {
	    int d = value % radix;
	    value = value / radix;
	    *--ptr = digit[d];
	    len--;
	} while((value > 0) || (len > 0));
    }
    if (sign)
	*--ptr = '-';
    return ptr;
}

// #define CLOCK_SOURCE CLOCK_MONOTONIC_RAW
#define CLOCK_SOURCE CLOCK_MONOTONIC

// Helper: get current time in nanoseconds (monotonic)
static inline int get_time_ns(struct timespec* tp)
{
    return clock_gettime(CLOCK_SOURCE, tp);
}

// Helper: sleep until tp + ns
static inline int sleep_until_ns(struct timespec* tp, long long ns)
{
    long long ns1 = tp->tv_nsec + ns;
    struct timespec until;

    until.tv_sec = tp->tv_sec + (ns1 / 1000000000LL);
    until.tv_nsec = ns1 % 1000000000LL;

    return clock_nanosleep(CLOCK_SOURCE,TIMER_ABSTIME,&until,NULL);
}

// READ from GPIO (emulated serial port/socket whatever)
void async_reader(async_reader_t* ap)
{
    int count = 0;
    // int wi = 0;
    uint32_t bits = 0;
    // uint18_t word = 0;
    // uint18_t sync = 0;
    // No scaling needed - ROM auto-calibrates from sync pattern
    // Just use consistent timing within each 18-bit word
    long long bit_time_ns = 1000000000 / ap->baud;
    struct timespec word_start_time;
    int bit_in_word;        // Bit counter within 18-bit word (0-29 for 3 bytes)

    printf("async_reader: started baud=%d, bit_time=%lld ns\n",
	   ap->baud, bit_time_ns);

    tcflush(ap->fd, TCIFLUSH);
    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	int n;

	bit_in_word = 0;
	while((count > 0) && !ap->chan.terminate) {
	    uint18_t bit = ((bits >> 29) & 1);
	    chan_t* rp = ap->out;
	    long long word_ns;

	    // Invert for F18: UART mark (1) = PIN17 LOW, UART space (0) = PIN17 HIGH
	    // F18 expects: idle=LOW, start bit=HIGH (inverted from TTL UART)
	    if (bit)
		set_ior(rp, F18_IO_PIN17);  // space/start → HIGH
	    else
		clr_ior(rp, F18_IO_PIN17);  // mark/idle → LOW

	    // Absolute time within 18-bit word (3 bytes = 30 bits)
	    bit_in_word++;
	    word_ns = bit_in_word*bit_time_ns;
	    sleep_until_ns(&word_start_time, word_ns);

	    bits <<= 1;
	    count--;
	}

    again:
	if (!ap->chan.terminate) {
	    uint8_t w18[3];
	    if ((n = read(ap->fd, w18, 3)) == 3) {
		int i;
		get_time_ns(&word_start_time);
		bits = 0;
		
		for (i = 0; i < 3; i++) {
		    uint8_t b0 = ~w18[i];  // invert all bits (again)
		    int n = 8;
		    // reverse bits (as sent from uart)
		    bits = (bits << 1) | 1;  // start bit
		    while(n--) {
			bits = (bits << 1) | (b0 & 1);
			b0 >>= 1;
		    }
		    bits = (bits << 1) | 0;  // stop bit
		}
		count = 30;
	    }
	    else if (n == 0) {
		PRINTF("async_reader: read 0 bytes\n");
		goto again;
	    }
	    else if (n < 0) {
		ERRORF("async_reader: read error %d (%s)\n",
		       errno, strerror(errno));
		return;
	    }
	}
    }
}

// WRITE to GPIO (emulated serial port/socket whatever)
// when 708 write value to ioreg 'io' then it ends up here
//
void async_writer(async_writer_t* ap)
{
    int count = 0;
    uint18_t bits = 0;

    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	uint8_t b;
	int i;
	chan_t* rp = ap->in;  // like 708 gpio output

	while((count < 10) && !ap->chan.terminate) {
	    uint18_t value;

	    if (f18_chan_read(rp, GPIO, &value))
		;
	    else
	    {
		f18_init_transfer(&ap->chan, READ, DIR_BIT(GPIO), 0, 0);
		if (f18_chan_read(rp, GPIO, &value)) {
		    f18_complete_transfer(&ap->chan, READ);
		}
		else {
		    value = f18_wait_transfer(&ap->chan, READ);
		    if (ap->chan.terminate)
			break;
		}
	    }
	    // FIXME: may implement multiplt pins (configire in async_writer)
	    // emulate bit sending 11 = 0, 10 => 1
	    PRINTF("async_writer: got value=%d, count=%d\n", value, count);
	    if (value == 3) {
		bits = (bits << 1) | 0;
		count++;
	    }
	    else if (value == 2) {
		bits = (bits << 1) | 1;
		count++;
	    }
	    else {
		PRINTF("async_writer: unexpected value %d\n", value);
	    }
	}
	if (!ap->chan.terminate) {
	    // reverse bits
	    b = 0;
	    bits >>= 1; // skip "stop" bit
	    for (i = 0; i < 8; i++) {
		b = (b << 1) | (bits & 1);
		bits >>= 1;
	    }
	    if (ap->fd >= 0) {
		PRINTF("async_writer: output byte %02x '%c'\n",
		       b, (b >= 32 && b < 127) ? b : '?');
		// fix sending
		// write(ap->fd, &b, 1);
	    }
	    bits = 0;
	    count = 0;
	}
    }
}
