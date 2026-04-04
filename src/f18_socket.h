#ifndef __F18_SOCKET_H__
#define __F18_SOCKET_H__

#include <sys/socket.h>
#include <sys/un.h>

// Socket connection mode
typedef enum {
    SOCK_MODE_NONE = 0,
    SOCK_MODE_SERVER,
    SOCK_MODE_CLIENT
} sock_mode_t;

#define MAX_SOCKET_NAMELEN 108

// Socket connection state
typedef struct {
    sock_mode_t mode;
    int listen_fd;       // Server: listening socket
    int conn_fd;         // Connected socket (server after accept, client after connect)
    char path[MAX_SOCKET_NAMELEN];      // Socket path (max for sun_path)
    // int node_id;         // Associated node (701 or 001)
    int connected;       // 1 if connection established
} f18_socket_t;

// Initialize socket structure
extern void f18_socket_init(f18_socket_t* sp);

// Create server socket, bind and listen
// Returns 0 on success, -1 on error
extern int f18_socket_server(f18_socket_t* sp, const char* path);

// Connect to server socket
// Returns 0 on success, -1 on error
extern int f18_socket_client(f18_socket_t* sp, const char* path);
extern int f18_socket_connect(f18_socket_t* sp);

// Accept connection (for server mode)
// Returns 0 on success, -1 on error (non-blocking, returns -1 with EAGAIN if no connection)
extern int f18_socket_accept(f18_socket_t* sp);

// Close socket
extern void f18_socket_close(f18_socket_t* sp);

// Read 18-bit word (blocks until data available)
// Returns 0 on success, -1 on error/disconnect
extern int f18_socket_read(f18_socket_t* sp, uint32_t* word);

// Write 18-bit word (blocks until written)
// Returns 0 on success, -1 on error/disconnect
extern int f18_socket_write(f18_socket_t* sp, uint32_t word);

// Generate socket path for a node
// e.g., "/tmp/f18_serdes_701.sock"
extern void f18_socket_path(char* buf, size_t buflen, int node_id);

#endif
