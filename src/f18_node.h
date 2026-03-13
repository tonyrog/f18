#ifndef __F18_NODE_H__
#define __F18_NODE_H__

// reg_node serdes_node analog_node
#include "f18.h"
#include "f18_debug.h"

// Randvous state 
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

typedef struct {
    node_t    n;         // "inheritance" do not move
    chan_t chan;         // we can reach reg_node_t from chan!

    pthread_t thread;
    pthread_attr_t attr;

    uint18_t dmask;    // DIR_BIT(x) available directions
    uint18_t imask;    // DIR_BIT(x) io_addr direction
    chan_t* neighbour[4]; // neighbour channels
    chan_t* ioc;          // configured ioreg io output!
    node_debug_t debug;    // debugger state for this node
} reg_node_t;

// used to debug channel stuff
#define chan_to_reg_node(cp) \
    ((reg_node_t*) (((uint8_t*)(cp))-sizeof(node_t)))

// async nodes it is a io  RX/TX
// channel is normally connect to node like 708 aync_boot
//
typedef struct _async_reader_t {
    node_t n;              // currently a dummy node
    chan_t chan;            // 708 put data here...
    chan_t* out;            // write to
    pthread_t thread;
    pthread_attr_t attr;
    int fd;
    int baud;
} async_reader_t;

extern void async_reader(async_reader_t* ap);

typedef struct _async_writer_t {
    node_t n;       // currently a dummy node    
    chan_t chan;    // 708 get data from here...
    chan_t* in;     // read from
    pthread_t thread;
    pthread_attr_t attr;
    int fd;
    int baud;
} async_writer_t;

extern void async_writer(async_writer_t* ap);

extern void f18_chan_init(chan_t* chan);
extern void f18_chan_terminate(chan_t* chan);
extern int f18_chan_write(chan_t* chan, uint18_t dir, uint18_t value);
extern int f18_chan_read(chan_t* chan, uint18_t dir, uint18_t* value_ptr);
extern void f18_init_transfer(chan_t* chan, int rw,
			      uint18_t rdirs, uint18_t wdirs,
			      uint18_t value);
extern void f18_complete_transfer(chan_t* chan, int rw);
extern uint18_t f18_wait_transfer(chan_t* chan, int rw);

// f18_pty.c 
extern int set_blocking(int fd, int on);
extern int set_exclusive(int fd, int on);
extern int open_pty(char* name, size_t max_namelen);

#endif
