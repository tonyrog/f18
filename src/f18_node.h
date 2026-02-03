#ifndef __F18_NODE_H__
#define __F18_NODE_H__

// reg_node serdes_node analog_node
#include "f18.h"

typedef struct {
    node_t    n;         // "inhertance"
    pthread_t thread;
    pthread_attr_t attr;
#ifdef SOCKET_IMPL
    int up_fd;
    int left_fd;
    int down_fd;
    int right_fd;
#endif
#ifdef MUTEX_IMPL
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint18_t dmask;    // DIR_BIT(x) available directions

    // Rendezvous state (protected by this node's lock)
    uint18_t wmask;    // DIR_BIT(x) directions wanting to write
    uint18_t rmask;    // DIR_BIT(x) directions wanting to read
    uint18_t data;     // write: value to send / read: received value
    int      completed; // transfer completed flag
#endif
    int tty_fd;
    int stdin_fd;
    int stdout_fd;    
} reg_node_t;

#endif
