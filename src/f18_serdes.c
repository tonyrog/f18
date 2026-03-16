//
// F18 SERDES Node Implementation
//
// SERDES (Serializer-Deserializer) nodes provide high-speed serial
// communication between F18 chips via 2-wire (clock + data) interface.
//
// Protocol:
// - To receive: put 0x3FFFE in T, read Up port, suspends until word received
// - To transmit: write first word to Data, set bit 17 of io to 1,
//                subsequent words written to Up port
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

#include "f18.h"
#include "f18_node.h"
#include "f18_serdes.h"

// Initialize SERDES node (call after basic reg_node_t setup)
void serdes_node_init(serdes_node_t* sp, int node_id)
{
    // reg_node_t part should already be initialized
    // Just init the SERDES-specific parts
    f18_socket_init(&sp->socket);
    sp->socket.node_id = node_id;
    sp->transmitting = 0;

    // Set read/write handlers to SERDES versions
    sp->rn.n.read_ioreg = serdes_read_ioreg;
    sp->rn.n.write_ioreg = serdes_write_ioreg;
}

int serdes_setup(serdes_node_t* sp, const char* mode)
{
    char path[128];

    f18_socket_path(path, sizeof(path), sp->rn.n.id);

    if (strcmp(mode, "server") == 0) {
	return f18_socket_server(&sp->socket, path);
    } else if (strcmp(mode, "client") == 0) {
	return f18_socket_client(&sp->socket, path);
    } else {
	fprintf(stderr, "serdes_setup: invalid mode '%s' (use 'server' or 'client')\n", mode);
	return -1;
    }
}

// Helper: check if ioreg targets Up port for this node
static int is_up_port(node_t* np, uint18_t ioreg)
{
    if ((ioreg & F18_DIR_MASK) == F18_DIR_BITS) {
	uint18_t dirs = dirbits(ID_TO_ROW(np->id), ID_TO_COLUMN(np->id), ioreg);
	return (dirs & DIR_BIT(UP)) != 0;
    }
    return 0;
}

// Read from SERDES (receive word from remote)
// When T = 0x3FFFE and reading Up port, receive a word via socket
uint18_t serdes_read_ioreg(node_t* np, uint18_t ioreg)
{
    serdes_node_t* sp = (serdes_node_t*)np;
    uint32_t word;

    // Check if this is a SERDES receive operation
    // The node should have T = 0x3FFFE when initiating receive
    if (np->reg.t == SERDES_RX_MAGIC && is_up_port(np, ioreg)) {
	// SERDES receive - read from socket
	PRINTF("[%03d] SERDES: receiving...\n", np->id);

	// Wait for connection if server
	if (sp->socket.mode == SOCK_MODE_SERVER && !sp->socket.connected) {
	    f18_socket_accept(&sp->socket);
	}

	if (!sp->socket.connected) {
	    PRINTF("[%03d] SERDES: not connected, returning 0\n", np->id);
	    return 0;
	}

	if (f18_socket_read(&sp->socket, &word) == 0) {
	    PRINTF("[%03d] SERDES: received 0x%05x\n", np->id, word);
	    return word & MASK18;
	} else {
	    PRINTF("[%03d] SERDES: read error\n", np->id);
	    return 0;
	}
    }

    // Not a SERDES operation, use normal handler
    return f18_read_ioreg(np, ioreg);
}

// Write to SERDES (transmit word to remote)
// First word written to Data, set bit 17 of io to enable transmit
// Subsequent words written to Up port
void serdes_write_ioreg(node_t* np, uint18_t ioreg, uint18_t value)
{
    serdes_node_t* sp = (serdes_node_t*)np;

    // Check if writing to IO register (enable transmit)
    if (ioreg == IOREG_IO) {
	if (value & SERDES_TX_ENABLE) {
	    sp->transmitting = 1;
	    PRINTF("[%03d] SERDES: transmit enabled\n", np->id);
	} else {
	    sp->transmitting = 0;
	}
	np->iow = value;
	return;
    }

    // Check if writing to Data (first transmit word) while transmitting
    if ((ioreg == IOREG_DATA || ioreg == IOREG_LDATA) && sp->transmitting) {
	PRINTF("[%03d] SERDES: transmit word 0x%05x via Data\n", np->id, value);

	// Wait for connection if server
	if (sp->socket.mode == SOCK_MODE_SERVER && !sp->socket.connected) {
	    f18_socket_accept(&sp->socket);
	}

	if (sp->socket.connected) {
	    f18_socket_write(&sp->socket, value);
	}
	return;
    }

    // Check if writing to Up port (subsequent transmit words) while transmitting
    if (is_up_port(np, ioreg) && sp->transmitting) {
	PRINTF("[%03d] SERDES: transmit word 0x%05x via Up\n", np->id, value);

	// Wait for connection if server
	if (sp->socket.mode == SOCK_MODE_SERVER && !sp->socket.connected) {
	    f18_socket_accept(&sp->socket);
	}

	if (sp->socket.connected) {
	    f18_socket_write(&sp->socket, value);
	}
	return;
    }

    // Not a SERDES operation, use normal handler
    f18_write_ioreg(np, ioreg, value);
}
