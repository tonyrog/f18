IOREG_OBJ = f18_mutex.o
#IOREG_OBJ = f18_socket.o
OBJS = f18_emu.o $(IOREG_OBJ) f18_exec.o f18_scan.o
CFLAGS = -g -DDEBUG -Wall -DMUTEX_IMPL
#CFLAGS = -g -DDEBUG -Wall -DSOCKET_IMPL
LDFLAGS = -g -lpthread

../bin/f18: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

f18_exec.o:	f18_exec.c f18.h f18_node.h
	$(CC) $(CFLAGS) -c f18_exec.c

f18_emu.o:	f18_emu.c f18.h f18_node.h
	$(CC) $(CFLAGS) -c f18_emu.c

f18_scan.o:	f18_scan.c f18.h f18_scan.h
	$(CC) $(CFLAGS) -c f18_scan.c

f18_mutex.o:	f18_mutex.c f18.h f18_node.h
	$(CC) $(CFLAGS) -c f18_mutex.c

f18_socket.o:	f18_socket.c f18.h f18_node.h
	$(CC) $(CFLAGS) -c f18_socket.c

clean:
	rm -f $(OBJS) ../bin/f18
