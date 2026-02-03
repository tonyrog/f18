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
    ((reg_node_t*)node[i][j-1])->dmask |= DIR_BIT(RIGHT);
    ((reg_node_t*)node[i][j])->dmask |= DIR_BIT(LEFT);
}

void init_up_down(int i, int j)
{
    ((reg_node_t*)node[i-1][j])->dmask |= DIR_BIT(DOWN);
    ((reg_node_t*)node[i][j])->dmask |= DIR_BIT(UP);		
}

void init_node(int i, int j)
{
    reg_node_t* np = (reg_node_t*) node[i][j];
    
    pthread_mutex_init(&np->lock, NULL);
    pthread_cond_init(&np->cond, NULL);
    np->dmask = 0;
    np->wmask = 0;
    np->rmask = 0;
    np->data = 0;
    np->completed = 0;
}

// Invert direction: UP<->DOWN, LEFT<->RIGHT
static const uint18_t invert_dir[4] = { DOWN, RIGHT, UP, LEFT };

static node_t* get_remote(node_t* np, int dir)
{
    int i = ID_TO_ROW(np->id);
    int j = ID_TO_COLUMN(np->id);
    switch(dir) {
    case UP:    return node[i-1][j];
    case LEFT:  return node[i][j-1];
    case DOWN:  return node[i+1][j];
    case RIGHT: return node[i][j+1];
    default: return NULL;
    }
}

// Decode ioreg into DIR_BIT mask of target directions,
// filtered by available directions (dmask).
static uint18_t select_dirs(node_t* np, uint18_t ioreg)
{
    uint18_t dirs = 0;
    if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	uint18_t dmask = ((reg_node_t*)np)->dmask;
	if ((ioreg & F18_RIGHT_BIT) && (dmask & DIR_BIT(RIGHT)))
	    dirs |= DIR_BIT(RIGHT);
	if ((!(ioreg & F18_DOWN_BIT)) && (dmask & DIR_BIT(DOWN)))
	    dirs |= DIR_BIT(DOWN);
	if ((ioreg & F18_LEFT_BIT) && (dmask & DIR_BIT(LEFT)))
	    dirs |= DIR_BIT(LEFT);
	if ((!(ioreg & F18_UP_BIT)) && (dmask & DIR_BIT(UP)))
	    dirs |= DIR_BIT(UP);
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

void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    reg_node_t* dp = (reg_node_t*) np;
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

    dirs = select_dirs(np, ioreg);
    if (dirs == 0) {
	fprintf(stderr, "[%d,%d]: io error when writing ioreg=%x, no directions\n", ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg);
	return;
    }

    // Phase 1: probe - try to find a reader already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	reg_node_t* rp = (reg_node_t*) get_remote(np, dir);
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
	reg_node_t* rp = (reg_node_t*) get_remote(np, dir);
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


uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg)
{
    reg_node_t* dp = (reg_node_t*) np;
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

    dirs = select_dirs(np, ioreg);
    if (dirs == 0) {
	fprintf(stderr, "[%d,%d]: io error when reading ioreg=%x, no directions\n", ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg);	
	return 0;
    }

    // Phase 1: probe - try to find a writer already waiting
    for (dir = 0; dir < 4; dir++) {
	if (!(dirs & DIR_BIT(dir))) continue;
	reg_node_t* rp = (reg_node_t*) get_remote(np, dir);
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
	reg_node_t* rp = (reg_node_t*)get_remote(np, dir);
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
