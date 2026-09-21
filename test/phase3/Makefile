CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra -fcommon
CPPFLAGS += -I../../inc
LDLIBS += -pthread
OBJS = ../../build/tju_packet.o ../../build/kernel.o ../../build/tju_tcp.o ../../build/tju_congestion.o

.PHONY: all clean
all: bin/transfer_client bin/transfer_server

bin:
	mkdir -p $@

bin/transfer_%: transfer_%.c transfer_common.h $(OBJS) | bin
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(OBJS) -o $@ $(LDLIBS)

clean:
	rm -f bin/transfer_client bin/transfer_server
