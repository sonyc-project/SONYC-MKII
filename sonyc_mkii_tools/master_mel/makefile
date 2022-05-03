CFLAGS = -Wall -Wextra -Wno-unused-parameter -I../Inc
CCOPTIMIZE = -O2

CC=gcc
AR=ar
LD=ld
OBJCOPY=objcopy

all: master_mel

# todo: try https://stackoverflow.com/questions/2394609/makefile-header-dependencies

master_mel: master_mel.o serial_frame.o
	$(CC) $(CFLAGS) $(CCOPTIMIZE) $^ -o $@

master_mel.o: master_mel.c my_socket.c master_mel.h ../Inc/serial_frame.h ../Inc/bootloader.h ../Inc/memory_map.h
	$(CC) $(CFLAGS) -c $(CCOPTIMIZE) $< -o $@

serial_frame.o: ../serial_frame/serial_frame.c ../Inc/serial_frame.h
	$(CC) $(CFLAGS) -DFRAME_MAX_SIZE=131072 -c $(CCOPTIMIZE) $< -o $@

udp_test: udp_test.c my_socket.c
	$(CC) $(CFLAGS) $(CCOPTIMIZE) $< -o $@

clean:
	rm -f master_mel udp_test *.o
