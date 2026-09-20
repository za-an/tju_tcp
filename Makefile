TOP_DIR = .
INC_DIR = $(TOP_DIR)/inc
SRC_DIR = $(TOP_DIR)/src
BUILD_DIR = $(TOP_DIR)/build

CC=gcc
FLAGS = -pthread -g -ggdb -DDEBUG -fcommon -I$(INC_DIR)
OBJS = $(BUILD_DIR)/tju_packet.o \
	   $(BUILD_DIR)/kernel.o \
	   $(BUILD_DIR)/tju_tcp.o \
	   $(BUILD_DIR)/tju_congestion.o



default:all

all: server client

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(wildcard $(INC_DIR)/*.h)
	mkdir -p $(BUILD_DIR)
	$(CC) $(FLAGS) -c -o $@ $<

clean:
	-rm -f ./build/*.o client server

server: $(OBJS) $(SRC_DIR)/server.c
	$(CC) $(FLAGS) ./src/server.c -o server $(OBJS)

client: $(OBJS) $(SRC_DIR)/client.c
	$(CC) $(FLAGS) ./src/client.c -o client $(OBJS) 

.PHONY: all clean check
check: all
	$(MAKE) -C test check



	
