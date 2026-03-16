#ifndef __BYTE_QUEUE_H__
#define __BYTE_QUEUE_H__

#include <stdint.h>
#include <pthread.h>

// Buffer size for bits (must be power of 2)
#define BYTE_QUEUE_SIZE 256
#define BYTE_QUEUE_MASK (BYTE_QUEUE_SIZE - 1)

typedef struct _byte_queue_t
{
    uint8_t bytes[BYTE_QUEUE_SIZE]; // circular byte buffer (0=LOW, 1=HIGH)
    volatile int head;           // writes here
    volatile int tail;           // reads here
    volatile int curr;           // current value bytes[head]
    int terminate;
    pthread_mutex_t lock;
    pthread_cond_t  cond;         // signal when new bits available
} byte_queue_t;

extern void byte_queue_init(byte_queue_t* qp);
extern void byte_queue_enq(byte_queue_t* qp, int value);
extern void byte_queue_enq_batch(byte_queue_t* qp, uint8_t* values, int count);
extern int byte_queue_deq(byte_queue_t* qp);
extern int byte_queue_curr(byte_queue_t* qp);
extern int byte_queue_available(byte_queue_t* qp);

#endif
