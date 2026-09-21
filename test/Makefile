BUILD_DIR = ../build
CC ?= gcc
CFLAGS ?= -pthread -g -ggdb -DDEBUG -fcommon -I../inc
CHECK_CFLAGS ?= -std=gnu99 -pthread -fcommon -Wall -Wextra -Werror -I../inc
CHECK_DIR ?= /tmp/tju_tcp_check

OBJS = $(BUILD_DIR)/tju_packet.o \
	   $(BUILD_DIR)/kernel.o \
	   $(BUILD_DIR)/tju_tcp.o \
	   $(BUILD_DIR)/tju_congestion.o \
	#    $(BUILD_DIR)/timer.o \
	#    $(BUILD_DIR)/SKB.o \
	#    $(BUILD_DIR)/debug.o 


default:all

all: test_client test_server rdt_server rdt_client close_client

clean:
	-rm -f test_client test_server rdt_server rdt_client close_client transport_cc test_congestion_unit client.log server.log rdt_server.log rdt_client.log

# Keep the test build self-contained: the online evaluator invokes
# `make -C test` directly and may start with an empty ../build directory.
$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.o: ../src/%.c ../inc/*.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

test_client: test_client.c $(OBJS)
	gcc -pthread -g -ggdb -DDEBUG -I../inc ./test_client.c -o test_client $(OBJS)

test_server: test_server.c $(OBJS)
	gcc -pthread -g -ggdb -DDEBUG -I../inc ./test_server.c -o test_server $(OBJS)

rdt_server: test_rdt_server.c $(OBJS)
	gcc -pthread -g -ggdb -DDEBUG -I../inc ./test_rdt_server.c -o rdt_server $(OBJS)
rdt_client: test_rdt_client.c $(OBJS)
	gcc -pthread -g -ggdb -DDEBUG -I../inc ./test_rdt_client.c -o rdt_client $(OBJS)

close_client: test_close_client.c $(OBJS)
	gcc -pthread -g -ggdb -DDEBUG -I../inc ./test_close_client.c -o close_client $(OBJS)

$(CHECK_DIR):
	mkdir -p $@

$(CHECK_DIR)/test_congestion_unit: test_congestion_unit.c ../src/tju_congestion.c ../inc/tju_congestion.h | $(CHECK_DIR)
	$(CC) $(CHECK_CFLAGS) test_congestion_unit.c ../src/tju_congestion.c -o $@

$(CHECK_DIR)/transport_cc: test_transport_cc.c ../src/tju_tcp.c ../src/tju_congestion.c ../src/tju_packet.c ../inc/*.h | $(CHECK_DIR)
	$(CC) $(CHECK_CFLAGS) test_transport_cc.c ../src/tju_congestion.c ../src/tju_packet.c -o $@

.PHONY: check
check: $(CHECK_DIR)/test_congestion_unit $(CHECK_DIR)/transport_cc
	$<
	$(CHECK_DIR)/transport_cc
