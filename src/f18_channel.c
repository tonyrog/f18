//
//  F18 channel
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
#include "f18_node.h"

extern node_t* node[8][18];

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
    // printf("f18_chan_write: value=%d to [%03d] dir=%d\n",
    // value, chan_to_reg_node(chan)->n.id, dir);
    
    pthread_mutex_lock(&chan->lock);
    
    if (chan->rmask & DIR_BIT(dir)) {
	// Reader is waiting for data from our direction - deliver!
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

//    printf("f18_chan_read: from [%03d] dir=%d\n",
//	   chan_to_reg_node(chan)->n.id, dir);
    
    pthread_mutex_lock(&chan->lock);
    
    if (chan->wmask & DIR_BIT(dir)) {
	// Writer is waiting to send in our direction - grab data!
	*value_ptr = chan->data;
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
    if (rw & WRITE)
	chan->wmask = 0;
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
	ERROR(np, "io error when writing ioreg=%x, not mapped\n", ioreg);
	return;
    }

    if ((ioreg == IOREG_IO) && (np->id == 708) &&
	(np->rom_type == async_boot)) {
	dirs = DIR_BIT(GPIO);
    }
    else
	dirs = select_dirs(np, ioreg);
    
    if (dirs == 0) {
	ERROR(np, "io error when writing ioreg=%x, no directions\n", ioreg);
	return;
    }

    // try to find a reader already waiting
    for (dir = 0; dir < 5; dir++) {
	if (!(dirs & DIR_BIT(dir)))
	    continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	if (f18_chan_write(rp, dir, value))
	    return;
    }

    // setup for transfer to dirs
    f18_init_transfer(&dp->chan, WRITE, 0, dirs, value);

    for (dir = 0; dir < 5; dir++) {
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
    f18_wait_transfer(&dp->chan, WRITE);
}

uint18_t read_status(reg_node_t* dp)
{
    uint18_t status = 0;
    uint18_t dirs;
    int dir;
    chan_t* rp;
    
    dirs = select_dirs((node_t*)dp, IOREG_RDLU);
    for (dir = 0; dir < 4; dir++) {
	uint18_t idir;
	if (!(dirs & DIR_BIT(dir))) continue;
	if ((rp = dp->neighbour[dir]) == NULL)
	    continue;
	idir = invert_dir[dir];
	pthread_mutex_lock(&rp->lock);
	if (rp->wmask & DIR_BIT(idir))    // neighbor is writing to us
	    status |= F18_IO_DIR_WR(dir);  // Xw=1 (active high)
	if (!(rp->rmask & DIR_BIT(idir))) // neighbor is NOT reading from us
	    status |= F18_IO_DIR_RD(dir);  // Xr-=1 (idle)
	pthread_mutex_unlock(&rp->lock);
    }
    return status;
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
	ERROR(np,"io error when reading ioreg=%x, not mapped\n",ioreg);
	return 0;
    }

    if (ioreg == IOREG_IO)
	return read_status(dp);

    dirs = select_dirs(np, ioreg);
    iodir  = np->io_addr ?
	dirbits(ID_TO_ROW(np->id),ID_TO_COLUMN(np->id),np->io_addr) : 0;
    dirs |= iodir;
    
    // FIXME: add io to dirs!

    VERBOSE(np, "read_ioreg addr=%03x dirs=%c%c%c%c\n",
	    ioreg,
	    (dirs & DIR_BIT(RIGHT) ? 'R' : '_'),
	    (dirs & DIR_BIT(DOWN)  ? 'D' : '_'),
	    (dirs & DIR_BIT(LEFT)  ? 'L' : '_'),
	    (dirs & DIR_BIT(UP)    ? 'U' : '_'));
    if (dirs == 0) {
	ERROR(np,"io error when reading ioreg=%x, no directions\n", ioreg);
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

    value = f18_wait_transfer(&dp->chan, READ);

    if (np->flags & FLAG_TERMINATE)
	return 0;
    return value;
}

/* main program for
 * read/write async serial data
 */

// READ from GPIO (emulated serial port/socket whatever)
void async_reader(async_reader_t* ap)
{
    int count = 0;
    uint18_t bits = 0;

    set_blocking(ap->fd, 1);

    while(!ap->chan.terminate) {
	int n;
	uint8_t b;
	
	while(count > 0) {  // 1
	    uint18_t value = (bits >> (count-1)) & 1;
	    chan_t* rp = ap->out;
	    int dir    = invert_dir[UP];
	    
	    // NOTE iod should be inverted, DOWN for 708 instead of UP
	    // printf("async_reader: write DOWN channel from [%03d]\n",
	    // chan_to_reg_node(rp)->n.id);	    
	    if (f18_chan_write(rp, dir, value))
		;
	    else {
		f18_init_transfer(&ap->chan, WRITE, DIR_BIT(dir), 0, value);
		if (f18_chan_write(rp, dir, value)) {
		    f18_complete_transfer(&ap->chan, WRITE);
		}
		else {
		    f18_wait_transfer(&ap->chan, WRITE);
		}
	    }
	    count--;
	    // printf("async_reader: bits=%03x, count=%d\n", bits, count);
	}
	if ((n = read(ap->fd, &b, 1)) == 1) {
	    int i;
	    // printf("async_reader: read byte %02x\n", b);
	    bits = (bits << 1) | 0;
	    for (i = 0; i < 8; i++)
		bits = (bits << 1) | (b & 1);
	    bits = (bits << 1) | 1;
	    count += 10;
	}
    }
}

// WRITE to GPIO (emulated serial port/socket whatever)
void async_writer(async_writer_t* ap)
{
    int count = 0;
    uint18_t bits = 0;

    set_blocking(ap->fd, 1);

    while(1) {
	uint8_t b;
	int i;
	chan_t* rp = ap->in;  // like 708 gpio output
	
	while(count < 10) {
	    uint18_t value;
	    int dir    = GPIO;
	    
	    // printf("async_writer: read GPIO channel from [%03d]\n",
	    // chan_to_reg_node(rp)->n.id);
	    if (f18_chan_read(rp, dir, &value))
		;
	    else
	    {
		f18_init_transfer(&ap->chan, READ, DIR_BIT(dir), 0, 0);
		if (f18_chan_read(rp, GPIO, &value)) {
		    f18_complete_transfer(&ap->chan, READ);
		}
		else {
		    value = f18_wait_transfer(&ap->chan, READ);
		}
	    }
	    // emulate bit sending 11 = 0, 10 => 1
	    if (value == 3) {
		bits = (bits << 1) | 0;
		count++;
	    }
	    else if (value == 2) {
		bits = (bits << 1) | 1;
		count++;
	    }
	    // printf("async_writer: bits=%03x, count=%d\n", bits, count);
	}
	// reverse bits
	b = 0;
	bits >>= 1; // skip "stop" bit
	for (i = 0; i < 8; i++) {
	    b = (b << 1) | (bits & 1);
	    bits >>= 1;
	}
	if (ap->fd >= 0) {
	    // printf("async_writer: wrote byte %02x\n", b);
	    write(ap->fd, &b, 1);
	}
	bits = 0;
	count = 0;
    }
}
