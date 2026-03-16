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
/*
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
*/

/*
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
*/

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

#define BITS_PER_WORD     30  // 3 bytes * 10 bits (start + 8 data + stop)
#define SAMPLES_PER_BIT   10

// Initialize async_reader sync buffer
void async_reader_init(async_reader_t* ap)
{
    byte_queue_init(&ap->bq);
    ap->sample_count = 1;
    ap->bit_count = 0;    
    ap->first_bit_received = 0;
}

// read_ioreg for node 708 - synchronous bit delivery
// Called when 708 reads from IO register
uint18_t read_ioreg_708(node_t* np, uint18_t ioreg)
{
    uint18_t ior_val;
    int pin17;
    
    // Check if this ioreg read includes GPIO direction
    // For 708, io_addr is set (0x91), so we check if ioreg matches
    uint18_t iodir = np->io_addr ?
	dirbits(ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), np->io_addr) : 0;
    uint18_t dirs = ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) ?
	dirbits(ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg) : 0;

    // IOREG_IO (0x15D) is "---D" = GPIO only, handle specially
    int is_gpio_read = (ioreg == IOREG_IO) || (iodir && (dirs & iodir));

    PRINTF("read_ioreg_708: ioreg=0x%03x iodir=0x%x dirs=0x%x gpio=%d\n",
	   ioreg, iodir, dirs, is_gpio_read);

    // If not a GPIO read, use default handler
    if (!is_gpio_read) {
	PRINTF("read_ioreg_708: not GPIO, fallback\n");
	return f18_read_ioreg(np, ioreg);
    }

    pin17 = byte_queue_curr(&r708.bq);
    if (r708.sample_count <= 0) {  // next bit
	if (r708.bit_count < BITS_PER_WORD) {
	    // Within word or starting new word
	    pin17 = byte_queue_deq(&r708.bq);  // May block if empty
	    r708.bit_count++;
	    // After bit 5 (sync pattern), add half-bit delay for center sampling
	    switch(r708.bit_count) {
	    case 8:  // sample 1.5 bit (stretch B0 instead of B0,START
	    case 12:
	    case 22:
		r708.sample_count = SAMPLES_PER_BIT + (SAMPLES_PER_BIT / 2);
		PRINTF("708/ bit=%d, pin=%d count=%d\n",
		       r708.bit_count, pin17, r708.sample_count);
		break;
	    default:  // sample 1 bit
		r708.sample_count = SAMPLES_PER_BIT;
		PRINTF("708/ bit=%d,pin=%d,count=%d\n",
		       r708.bit_count, pin17, r708.sample_count);
	    }
	}
	else {
	    // After 30 bits - get next word (block if needed)
	    r708.bit_count = 0;  // Reset for next word
	    PRINTF("708/ word done, waiting for next\n");
	    pin17 = byte_queue_deq(&r708.bq);  // Block until next frame
	    r708.bit_count++;
	    r708.sample_count = SAMPLES_PER_BIT;
	}
    }
    r708.sample_count--;

    // Build IOR value with current PIN17 state
    ior_val = __atomic_load_n(&np->ior, __ATOMIC_SEQ_CST);
    if (pin17)
	ior_val |= F18_IO_PIN17;
    else
	ior_val &= ~F18_IO_PIN17;
    return ior_val;
}

// READ from GPIO - fills bit buffer for 708 to consume
void async_reader(async_reader_t* ap)
{
    printf("async_reader: started baud=%d (sync buffer mode)\n", ap->baud);

    tcflush(ap->fd, TCIFLUSH);
    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	uint8_t w18[3];
	int n;

	if ((n = read(ap->fd, w18, 3)) == 3) {
	    uint8_t bits[30];  // 3 bytes * 10 bits each
	    int bi = 0;
	    int i, j;

	    // Build all 30 bits first
	    for (i = 0; i < 3; i++) {
		uint8_t b0 = ~w18[i];  // invert all bits

		// Start bit (HIGH after inversion)
		bits[bi++] = 1;

		// 8 data bits (LSB first)
		for (j = 0; j < 8; j++) {
		    bits[bi++] = b0 & 1;
		    b0 >>= 1;
		}
		// Stop bit (LOW after inversion)
		bits[bi++] = 0;
	    }

	    // Push all 30 bits atomically
	    byte_queue_enq_batch(&ap->bq, bits, 30);

	    PRINTF("async_reader: deliverd bytes 0x%02x%02x%02x\n",
		   w18[2], w18[1], w18[0]);
	}
	else if (n == 0) {
	    // No data, retry
	    continue;
	}
	else if (n < 0) {
	    if (errno == EINTR)
		continue;
	    ERRORF("async_reader: read error %d (%s)\n",
		   errno, strerror(errno));
	    return;
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
