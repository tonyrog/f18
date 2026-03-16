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

// Invert direction: UP<->DOWN, LEFT<->RIGHT
static const uint18_t invert_dir[5] = { DOWN, RIGHT, UP, LEFT, GPIO };

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
