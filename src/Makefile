MUTEX_OBJS = 	f18_emu.o f18_mutex.o f18_scan.o
SOCKET_OBJS = 	f18_emu.o f18_socket.o f18_scan.o
CFLAGS = -g -DDEBUG -Wall
LDFLAGS = -g -lpthread

../bin/f18.socket: $(SOCKET_OBJS)
	$(CC) -o $@ $(SOCKET_OBJS) $(LDFLAGS)

../bin/f18.mutex: $(MUTEX_OBJS)
	$(CC) -o $@ $(MUTEX_OBJS) $(LDFLAGS) 

f18_emu.o:	f18_emu.c f18.h
	$(CC) $(CFLAGS) -c f18_emu.c

f18_scan.o:	f18_scan.c f18.h f18_scan.h
	$(CC) $(CFLAGS) -c f18_scan.c

f18_mutex.o:	f18_mutex.c f18.h
	$(CC) $(CFLAGS) -c f18_mutex.c

f18_socket.o:	f18_socket.c f18.h
	$(CC) $(CFLAGS) -c f18_socket.c
