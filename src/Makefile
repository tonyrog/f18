OBJS = 	f18.o f18_emu.o
CFLAGS = -g -DDEBUG
LDFLAGS = -g -lpthread

../bin/f18: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS) 

f18_emu.o:	f18_emu.c f18.h
	$(CC) $(CFLAGS) -c f18_emu.c

f18.o:	f18.c f18.h
	$(CC) $(CFLAGS) -c f18.c
