
#include "f18_byte_queue.h"

void byte_queue_init(byte_queue_t* qp)
{
    qp->head = 0;
    qp->tail = 0;
    qp->curr = 0;
    pthread_mutex_init(&qp->lock, NULL);
    pthread_cond_init(&qp->cond, NULL);
}

// Add a byte to the buffer (called by async_reader thread)
void byte_queue_enq(byte_queue_t* qp, int value)
{
    pthread_mutex_lock(&qp->lock);

    // Wait if buffer is full
    while (((qp->head + 1) & BYTE_QUEUE_MASK) == qp->tail) {
	pthread_cond_wait(&qp->cond, &qp->lock);
	if (qp->terminate) {
	    pthread_mutex_unlock(&qp->lock);
	    return;
	}
    }

    qp->bytes[qp->head] = value;
    qp->head = (qp->head + 1) & BYTE_QUEUE_MASK;
    pthread_cond_broadcast(&qp->cond);
    pthread_mutex_unlock(&qp->lock);
}

// Add multiple bytes atomically (called by async_reader for a complete 18-bit word)
void byte_queue_enq_batch(byte_queue_t* qp, uint8_t* values, int count)
{
    int i, used, avail;
    pthread_mutex_lock(&qp->lock);

    // Wait until there's room for all bytes
    // used = number of elements in queue, avail = free slots (leaving 1 empty)
    used = (qp->head - qp->tail) & BYTE_QUEUE_MASK;
    avail = BYTE_QUEUE_SIZE - 1 - used;
    while (avail < count && !qp->terminate) {
	pthread_cond_wait(&qp->cond, &qp->lock);
	used = (qp->head - qp->tail) & BYTE_QUEUE_MASK;
	avail = BYTE_QUEUE_SIZE - 1 - used;
    }
    if (qp->terminate) {
	pthread_mutex_unlock(&qp->lock);
	return;
    }

    // Insert all bytes at once
    for (i = 0; i < count; i++) {
	qp->bytes[qp->head] = values[i];
	qp->head = (qp->head + 1) & BYTE_QUEUE_MASK;
    }

    pthread_cond_broadcast(&qp->cond);
    pthread_mutex_unlock(&qp->lock);
}

// Get next byte from buffer (called by 708 via read_ioreg)
// Returns -1 if no byte available (should wait)
int byte_queue_deq(byte_queue_t* qp)
{
    int curr;
    pthread_mutex_lock(&qp->lock);

    if (qp->head == qp->tail) {
	// Buffer empty - wait for bytes
	while ((qp->head == qp->tail) && !qp->terminate) {
	    pthread_cond_wait(&qp->cond, &qp->lock);
	}
	if (qp->terminate) {
	    pthread_mutex_unlock(&qp->lock);
	    return -1;
	}
    }

    curr = qp->bytes[qp->tail];
    qp->tail = (qp->tail + 1) & BYTE_QUEUE_MASK;
    qp->curr = curr;
    pthread_cond_broadcast(&qp->cond);  // signal buffer space available
    pthread_mutex_unlock(&qp->lock);
    return curr;
}

int byte_queue_curr(byte_queue_t* qp)
{
    return qp->curr;
}

// Check if bytes are available without blocking
int byte_queue_available(byte_queue_t* qp)
{
    return qp->head != qp->tail;
}
