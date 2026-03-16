#ifndef __F18_CHANNEL_H__
#define __F18_CHANNEL_H__

//
// F18 Channel - inter-node communication via rendezvous
//

#include <pthread.h>
#include "f18.h"

// Transfer direction flags
#define READ  1
#define WRITE 2

// Rendezvous channel state
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;

    // Rendezvous state (protected by lock)
    uint18_t wmask;     // DIR_BIT(x) directions wanting to write
    uint18_t rmask;     // DIR_BIT(x) directions wanting to read
    uint18_t data;      // write: value to send / read: received value
    int      completed; // transfer completed flag
    int      wait;      // 1 when in cond_wait
    int      terminate; // 1 when time to terminate user thread
} chan_t;

// Initialize channel
extern void f18_chan_init(chan_t* chan);

// Signal channel to terminate
extern void f18_chan_terminate(chan_t* chan);

// Try to write to channel (non-blocking)
// Returns 1 if successful, 0 if no reader waiting
extern int f18_chan_write(chan_t* chan, uint18_t dir, uint18_t value);

// Try to read from channel (non-blocking)
// Returns 1 if successful, 0 if no writer waiting
extern int f18_chan_read(chan_t* chan, uint18_t dir, uint18_t* value_ptr);

// Initialize transfer state
extern void f18_init_transfer(chan_t* chan, int rw,
			      uint18_t rdirs, uint18_t wdirs,
			      uint18_t value);

// Mark transfer as complete
extern void f18_complete_transfer(chan_t* chan, int rw);

// Wait for transfer to complete (blocking)
extern uint18_t f18_wait_transfer(chan_t* chan, int rw);

#endif
