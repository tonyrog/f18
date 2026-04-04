#ifndef __F18_SERDES_H__
#define __F18_SERDES_H__

//
// F18 SERDES Node (701 and 001)
//
// SERDES nodes provide high-speed serial communication between
// F18 chips via 2-wire (clock + data) interface over Unix sockets.
//

#include "f18.h"
#include "f18_node.h"
#include "f18_socket.h"

typedef enum {
    SERDES_MODE_NONE=0,
    SERDES_MODE_SERVER=1,
    SERDES_MODE_CLIENT=2,
} serdes_mode_t;

// SERDES node - "inherits" from reg_node_t
typedef struct {
    reg_node_t rn;           // must be first (inheritance)
    f18_socket_t socket;     // socket connection
    serdes_mode_t mode;                // server / client
    char path[MAX_SOCKET_NAMELEN];
    int transmitting;        // 1 if currently transmitting
} serdes_node_t;

// SERDES magic value for receive: T = 0x3FFFE
#define SERDES_RX_MAGIC  0x3FFFE

// IO bit for transmit enable (bit 17)
#define SERDES_TX_ENABLE 0x20000

// Initialize SERDES node
extern void serdes_node_init(serdes_node_t* sp, int mode, const char* path);

// Setup SERDES socket connection
extern int serdes_setup(serdes_node_t* sp);

// SERDES read ioreg - handles SERDES protocol with fallback to f18_read_ioreg
extern uint18_t serdes_read_ioreg(node_t* np, uint18_t ioreg);

// SERDES write ioreg - handles SERDES protocol with fallback to f18_write_ioreg
extern void serdes_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value);

#endif
