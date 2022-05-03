
/*

This is meant to be #include'd in master_mel.c (after includes) and is not standalone.
Making another file only to reduce clutter. --NPS

*/

static int my_socket(void) {
	int fd;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("cannot create socket");
		return -1;
	}
	return fd;
}

static int my_bind_socket(int fd, short int port) __attribute__ ((unused));
static int my_bind_socket(int fd, short int port) {
	struct sockaddr_in myaddr;
	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	//myaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	myaddr.sin_addr.s_addr = inet_addr("127.0.0.1");
	myaddr.sin_port = htons(port);

	if (bind(fd, (struct sockaddr *)&myaddr, sizeof(myaddr)) < 0) {
		perror("bind failed");
		return -1;
	}
	return 0;
}

// Send to given port on localhost
static int my_socket_send(int socket, uint16_t port, const void *buffer, size_t len) __attribute__ ((unused));
static int my_socket_send(int socket, uint16_t port, const void *buffer, size_t len) {
	int ret;
	struct sockaddr_in myaddr;

	memset((char *)&myaddr, 0, sizeof(myaddr));
	myaddr.sin_family = AF_INET;
	myaddr.sin_port = htons(port);
	myaddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	ret = sendto(socket, buffer, len, 0, (struct sockaddr *)&myaddr, sizeof(myaddr));
	if (ret < 0)
		perror("sendto failed");
	return ret;
}
