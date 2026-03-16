#ifndef __F18_ASYNC_H__
#define __F18_ASYNC_H__

//
// F18 Async Boot Node (708)
//
// Node 708 has special async serial boot capability.
// This module handles the async I/O for that node.
//

#include "f18.h"
#include "f18_node.h"
#include "f18_byte_queue.h"
#include <pthread.h>

// Async reader node (receives serial data for 708)
typedef struct _async_reader_t {
    node_t n;                // dummy node for id
    chan_t chan;             // channel for communication
    chan_t* out;             // write to (708's channel)
    pthread_t thread;
    pthread_attr_t attr;
    int fd;
    int baud;
    byte_queue_t bq;
    volatile int sample_count;        // @b reads since last bit change
    volatile int first_bit_received;  // Block until first start bit
    volatile int bit_count;           // bits received in current word (0-29)
} async_reader_t;

// Async writer node (sends serial data from 708)
typedef struct _async_writer_t {
    node_t n;                // dummy node for id
    chan_t chan;             // channel for communication
    chan_t* in;              // read from (708's channel)
    pthread_t thread;
    pthread_attr_t attr;
    int fd;
    int baud;
} async_writer_t;

// Initialize async reader
extern void async_reader_init(async_reader_t* ap);

// Async reader thread function
extern void async_reader(async_reader_t* ap);

// Async writer thread function
extern void async_writer(async_writer_t* ap);

// Custom read_ioreg for node 708 - handles sync buffer reads
extern uint18_t read_ioreg_708(node_t* np, uint18_t ioreg);

// Global async nodes
extern async_reader_t r708;
extern async_writer_t w708;

#endif
