#ifndef __F18_NODE_H__
#define __F18_NODE_H__

// reg_node serdes_node analog_node
#include "f18.h"
#include "f18_channel.h"
#include "f18_debug.h"
#include "f18_byte_queue.h"
#include <pthread.h>

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

// Default handlers for reading/writing IO registers
extern uint18_t f18_read_ioreg(node_t* np, uint18_t ioreg);
extern void f18_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value);

// f18_pty.c
extern int set_blocking(int fd, int on);
extern int set_exclusive(int fd, int on);
extern int open_pty(char* name, size_t max_namelen);

#endif
