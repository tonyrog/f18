//
// F18 Unix Domain Socket for SERDES communication
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>

#include "f18_socket.h"

void f18_socket_init(f18_socket_t* sp)
{
    memset(sp, 0, sizeof(*sp));
    sp->mode = SOCK_MODE_NONE;
    sp->listen_fd = -1;
    sp->conn_fd = -1;
    sp->connected = 0;
}

void f18_socket_path(char* buf, size_t buflen, int node_id)
{
    snprintf(buf, buflen, "/tmp/f18_serdes_%03d.sock", node_id);
}

int f18_socket_server(f18_socket_t* sp, const char* path)
{
    struct sockaddr_un addr;
    int fd;

    // Check that path ends in .sock as a safe guard gainst removing
    // random files by misstake.

    if (strcmp(path+strlen(path)-5, ".sock") != 0) {
	fprintf(stderr, "socket file name does not have .sock suffix!\n");
	exit(1);
    }
    unlink(path);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 1) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    sp->mode = SOCK_MODE_SERVER;
    sp->listen_fd = fd;
    sp->conn_fd = -1;
    sp->connected = 0;
    strncpy(sp->path, path, sizeof(sp->path) - 1);

    printf("SERDES server listening on %s\n", path);
    return fd;
}

int f18_socket_connect(f18_socket_t* sp)
{
    struct sockaddr_un addr;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sp->path, sizeof(addr.sun_path) - 1);

    if (connect(sp->conn_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return -1;  // No connection yet (non-blocking)
        }
        perror("connect");
        return -1;
    }
    sp->connected = 1;
    printf("SERDES client connected to %s\n", sp->path);
    return sp->conn_fd;
}

int f18_socket_client(f18_socket_t* sp, const char* path)
{
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    sp->mode = SOCK_MODE_CLIENT;
    sp->connected = 0;
    strncpy(sp->path, path, sizeof(sp->path) - 1);
    sp->listen_fd = -1;
    sp->conn_fd = fd;
    return f18_socket_connect(sp);
}

int f18_socket_accept(f18_socket_t* sp)
{
    int fd;

    if ((sp->mode != SOCK_MODE_SERVER) || (sp->listen_fd < 0)) {
        return -1;
    }

    if (sp->connected) {
        return 0;  // Already connected
    }

    fd = accept(sp->listen_fd, NULL, NULL);
    if (fd < 0) {
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            return -1;  // No connection yet (non-blocking)
        }
        perror("accept");
        return -1;
    }

    sp->conn_fd = fd;
    sp->connected = 1;
    printf("SERDES server accepted connection\n");
    return 0;
}

void f18_socket_close(f18_socket_t* sp)
{
    if (sp->conn_fd >= 0) {
        close(sp->conn_fd);
        sp->conn_fd = -1;
    }
    if (sp->listen_fd >= 0) {
        close(sp->listen_fd);
        sp->listen_fd = -1;
    }
    if ((sp->mode == SOCK_MODE_SERVER) && sp->path[0]) {
        unlink(sp->path);
    }
    sp->connected = 0;
    sp->mode = SOCK_MODE_NONE;
}

// Wire format: 3 bytes for 18-bit word (little-endian)
// byte[0] = bits 0-7
// byte[1] = bits 8-15
// byte[2] = bits 16-17 (upper 6 bits unused)

int f18_socket_read(f18_socket_t* sp, uint32_t* word)
{
    uint8_t buf[3];
    ssize_t n;
    size_t total = 0;

    if (!sp->connected || (sp->conn_fd < 0)) {
        return -1;
    }

    // Read exactly 3 bytes
    while (total < 3) {
        n = read(sp->conn_fd, buf + total, 3 - total);
        if (n <= 0) {
            if (n == 0) {
                // Connection closed
                sp->connected = 0;
            }
            return -1;
        }
        total += n;
    }
    *word = buf[0] | (buf[1] << 8) | ((buf[2] & 0x03) << 16);
    return 0;
}

int f18_socket_write(f18_socket_t* sp, uint32_t word)
{
    uint8_t buf[3];
    ssize_t n;
    size_t total = 0;

    if (!sp->connected || sp->conn_fd < 0) {
        return -1;
    }

    buf[0] = word & 0xFF;
    buf[1] = (word >> 8) & 0xFF;
    buf[2] = (word >> 16) & 0x03;

    // Write exactly 3 bytes
    while (total < 3) {
        n = write(sp->conn_fd, buf + total, 3 - total);
        if (n <= 0) {
            if (n == 0) {
                sp->connected = 0;
            }
            return -1;
        }
        total += n;
    }
    return 0;
}
