APP=$(shell basename `cd ..; pwd`)
ERLC=$(shell which erlc)
ERLC_FLAGS=-MMD -MF .$<.d -I ../.. +debug_info
ERL_SOURCES=$(wildcard *.erl) $(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)
ERL_OBJECTS=$(ERL_SOURCES:%.erl=%.beam)
ALL_OBJECTS=$(ERL_OBJECTS)
ERL_MODULES=$(ERL_SOURCES:%.erl=%)
comma=,
empty=
space = $(empty) $(empty)
MODULES=$(subst $(space),$(comma),$(ERL_MODULES))
VERSION=$(shell git describe --always --tags)

OBJS = f18_emu.o f18_channel.o f18_exec.o f18_scan.o f18_rom.o f18_dis.o f18_config.o f18_pty.o
CFLAGS = -MMD -MF .$<.d  -g -DDEBUG -Wall 
LDFLAGS = -g -lpthread

.PRECIOUS: $(YRL_SRC:%.yrl=%.erl) $(XRL_SRC:%.xrl=%.erl)

all:  $(ALL_OBJECTS) ../bin/f18

../bin/f18: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o:	%.c
	$(CC) $(CFLAGS) -c $<

%.beam: %.erl
	$(ERLC) $(ERLC_FLAGS) $<

clean:
	rm -f $(OBJS) ../bin/f18

-include .*.d
