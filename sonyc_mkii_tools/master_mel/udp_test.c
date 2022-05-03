#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MASTER_MEL_DEFAULT_UDP_SEND_PORT   61393

#include "my_socket.c"

// A quick and dirty server test

int main(int argc, char **argv) {
	uint8_t buf[4096];
	int recvlen;
	int fd;
	
	fd = my_socket();
	if (fd < 0) exit(0);

	my_bind_socket(fd, MASTER_MEL_DEFAULT_UDP_SEND_PORT);
	
	printf("Listening on UDP port %d\n", MASTER_MEL_DEFAULT_UDP_SEND_PORT);
	while(1) {
		recvlen = recvfrom(fd, buf, sizeof(buf), 0, NULL, NULL);
		if (recvlen > 0) {
			fwrite(buf, recvlen, 1, stdout);
		}
	}
}
