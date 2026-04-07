APP=$(shell basename `cd ..; pwd`)
ERLC=$(shell which erlc)
ERL=$(shell which erl)
EBIN=../ebin
ERLC_FLAGS=-MMD -MF .$<.d -I ../.. +debug_info
ERL_SOURCES=$(wildcard *.erl) $(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)
ERL_OBJECTS=$(ERL_SOURCES:%.erl=$(EBIN)/%.beam)
ALL_OBJECTS=$(ERL_OBJECTS)
ERL_MODULES=$(ERL_SOURCES:%.erl=%)
comma=,
empty=
space = $(empty) $(empty)
MODULES=$(subst $(space),$(comma),$(ERL_MODULES))
VERSION=$(shell git describe --always --tags)

OBJS = f18_strings.o f18_emu.o f18_channel.o f18_exec.o f18_asm.o f18_rom.o f18_dis.o f18_config.o f18_pty.o f18_debug.o f18_tui.o f18_sym.o f18_voc.o f18_byte_queue.o f18_socket.o f18_serdes.o f18_async.o f18_epoll.o

CFLAGS = -MMD -MF .$<.d  -g -DDEBUG -Wall
LDFLAGS = -g -lpthread -lncursesw

.PRECIOUS: $(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)

all:  $(ALL_OBJECTS) ../bin/f18

../bin/f18: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o:	%.c
	$(CC) $(CFLAGS) -c $<

$(EBIN)/%.beam: %.erl
	$(ERLC) -o $(EBIN) $(ERLC_FLAGS) $<

f18_strings.c:	f18_strings.tab
	$(ERL) -noinput -pa ../ebin -s f18_strings generate -s erlang halt

clean:
	rm -f $(OBJS) ../bin/f18

-include .*.d
