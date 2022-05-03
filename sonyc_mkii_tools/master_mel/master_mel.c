#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
//#include <sys/ioctl.h>
//#include <ctype.h>
//#include <errno.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "serial_frame.h"
#include "master_mel.h"
#include "bootloader.h"

//#define ALWAYS_FLUSH_FILE

#define BUF_SZ 4096
#define MY_STDIN_BUF_SZ 1024

// Must be % 32 , must fit in mote side buffer (2 kByte typ) when encoded
#define PROGRAM_CHUNK_SIZE 256 // For some reason, F1 programming fails if > 512. Should investigate.

//#define DEFAULT_ALLOW_UNSAFE
#define DEFAULT_VERBOSE

#define COND_FCLOSE(x) if (x != NULL) fclose(x)

// random between 49152 and 65535
//#define MASTER_MEL_DEFAULT_UDP_LISTEN_PORT 51210 // NYI
#define MASTER_MEL_DEFAULT_UDP_SEND_PORT   61393

// On Linux (but not cygwin) there are too many newlines in the debug output...
// Probably should decrease the logged string length too...
#ifdef __linux__
static void filter_last_newline(uint8_t *str, int len) { str[len-1] = '\0'; }
#else
static void filter_last_newline(uint8_t *str, int len) { ; }
#endif

#include "my_socket.c"

/* TODO
	Fix all the write() calls which assume buffer goes out in one shot... rookie mistake yeah...
*/

// CTRL-C Handler flag
static volatile bool caught_stop = false;

static int verbose_flag;
static int listen_flag;
static int unsafe_flag;
static int input_stdin_flag;
static int print_data_stdout_flag;
static int print_timestamps_flag;

static int got_ack;		// Global shared ACK flag.
static int got_nack; 	// Global shared NACK flag.

typedef struct {
	FILE *audio_file;
	FILE *data_file;
	FILE *data_and_debug_file;
	int tx_socket_fd;
	unsigned audio_bytes_written;
	unsigned debug_bytes_written;
	unsigned data_bytes_written;
	unsigned frame_count;
} mel_status_t;

static bool parse_frame(serial_frame_t *f, mel_status_t *status);
//static void error_frame(serial_frame_t *f, mel_status_t *status);
static void handle_frame(serial_frame_t *f, mel_status_t *status);
static void handle_frame(serial_frame_t *f, mel_status_t *status);
static void debug_frame_handler(serial_frame_t *f, mel_status_t *status);
static void debug_bms_frame_handler(serial_frame_t *f, mel_status_t *status);
static void data_string_frame_handler(serial_frame_t *f, mel_status_t *status);
static void audio_frame_handler(serial_frame_t *f, mel_status_t *status);

static void get_frames(int fd, uint8_t *buf, serial_frame_t *f, mel_status_t *status);

#define MY_PRINTF(...) \
	do { \
		if (verbose_flag) \
			printf(__VA_ARGS__); \
	} while(0)

// Needed to support serial_frame_decode()
// Caller (us) must handle free()
void * serial_frame_malloc(size_t size) {
	void *p = malloc(size);
	return p;
}

void intHandler(int dummy __attribute__ ((unused))) {
	//caught_stop = true;
	exit(0); // more responsive
}

// Wrapper to ensure full buffer goes out and basic error check
// Returns number of bytes written but does not need to be checked unless for error (-1)
static int my_write_buf(int fd, uint8_t *buf, int len) {
	int ret = write(fd, buf, len);
	int written = ret;
	while (ret < len) {
		if (ret <= 0) {
			perror("write()");
			return -1;
		}
		buf += ret;
		len -= ret;
		ret = write(fd, buf, len);
		written += ret;
	}
	return written;
}

static int set_both_file(const char *path, mel_status_t *status) {
	FILE *f;
	f = fopen(path, "w");
	if (f == NULL) {
		perror("Could not open data file");
		return -1;
	}
	status->data_and_debug_file = f;
	return 0;
}

static int set_data_file(const char *path, mel_status_t *status) {
	FILE *f;
	f = fopen(path, "w");
	if (f == NULL) {
		perror("Could not open data file");
		return -1;
	}
	status->data_file = f;
	return 0;
}

static int set_audio_file(const char *path, mel_status_t *status) {
	FILE *f;
	f = fopen(path, "w");
	if (f == NULL) {
		perror("Could not open audio file");
		return -1;
	}
	status->audio_file = f;
	return 0;
}

static int open_port(const char *COMPORT) {
	int fd; /* File descriptor for the port */
	if (COMPORT == NULL) {
		fprintf(stderr, "NULL serial device\r\n");
		return -1;
	}
	fd = open(COMPORT, O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1) {
		fprintf(stderr,"Device: %s\r\n", COMPORT);
		perror("serial device open error");
	}
	else {
		fcntl(fd, F_SETFL, 0); // Set as blocking
		MY_PRINTF("Opened port %s\n\n", COMPORT);
	}
	return fd;
}

// https://stackoverflow.com/questions/25996171/linux-blocking-vs-non-blocking-serial-read
// https://www.tldp.org/HOWTO/text/Serial-Programming-HOWTO
static void set_mf_attr(int fd) {
	struct termios options;
	tcgetattr(fd, &options);

	options.c_cflag     |= (CLOCAL | CREAD);
	options.c_lflag     &= ~(ICANON | ECHO | ECHOE | ISIG);
	options.c_oflag     &= ~OPOST;

	// Timeouts... lets not mess with it
	options.c_cc[VMIN]  = 1;
	options.c_cc[VTIME] = 0;

	options.c_iflag &= ~(IXON | IXOFF | IXANY);

	//cfsetspeed(&options, 9600); // baud is meaningless for usb-serial
	tcsetattr(fd, TCSANOW, &options);
}

#define CHECK_FLAG(x) {if (flag & x) MY_PRINTF("Flag: " #x "\r\n");}
static bool parse_frame(serial_frame_t *f, mel_status_t *status) {
	if (f == NULL) { MY_PRINTF("NULL!!!\r\n"); return false; }
	frame_flag_t flag = f->flag;
	if (f->err == NO_FRAME) return false;
	if (f->err) MY_PRINTF("Frame Error %d\r\n", f->err);

	CHECK_FLAG(NONE_FLAG);
	//CHECK_FLAG(FRAME_FOUND);
	//CHECK_FLAG(PARTIAL);
	//CHECK_FLAG(NO_PAYLOAD);
	CHECK_FLAG(CRC_ERROR);

	if (flag & CRC_ERROR) {
		MY_PRINTF("DEBUG: PAYLOAD SIZE: %u\r\n", f->sz);
	}

	if (flag & FRAME_FOUND) {
		handle_frame(f, status);
		if (f->buf)
			free(f->buf);
		f->buf = NULL;
		f->sz = 0;
		return true;
	}
	return false;
}

// #define CHUNK_SZ 64
// int main(int argc, char **argv) {
	// FILE *my_file;
	// int ret;
	// my_file = fopen("test.bin", "r");

	// while ( fread(buf, CHUNK_SZ, 1, my_file) == 1 ) {
		// int i=0;
		// bool go = false;
		// do {
			// ret = serial_frame_decode(&buf[i], CHUNK_SZ-i, &f);
			// i += ret;
			// go = parse_frame(&f);
		// } while (go);
	// }
	// fclose(my_file);
	// return 0;
// }

// Called on completed valid frames as they come in from parse_frame()
// parse_frame() will free() after this function returns
static void handle_frame(serial_frame_t *f, mel_status_t *status) {
	switch(f->type) {
		case FRAME_TYPE_DEBUG_STRING_BMS: debug_bms_frame_handler(f, status); break;
		case FRAME_TYPE_DEBUG_STRING: debug_frame_handler(f, status); break;
		case FRAME_TYPE_DATA_STRING: data_string_frame_handler(f, status); break;
		case FRAME_TYPE_BIN_AUDIO: audio_frame_handler(f, status); break;
		//case FRAME_TYPE_HELLO: fprintf(stderr,"Got Hello\r\n"); break;
		case FRAME_TYPE_ACK:  got_ack  = 1; break;
		case FRAME_TYPE_NACK: got_nack = 1; break;
		default: MY_PRINTF("ERROR: Unknown frame type %u\r\n", f->type);
	}
	status->frame_count++;
}

// static void error_frame(serial_frame_t *f, mel_status_t *status) {
	// fprintf(stderr, "Error %u\r\n", f->err);
// }

// Data length does not include terminating null and we don't want it
static void send_data_frame(char *data, unsigned sz, uint8_t *buf, unsigned scratch_buf_sz, int fd) {
	int ret = serial_frame_encode((uint8_t *)data, sz, scratch_buf_sz, buf, DEST_H7, FRAME_TYPE_DATA_STRING);
	if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }
	my_write_buf(fd, buf, ret);
}

static void debug_bms_frame_handler(serial_frame_t *f, mel_status_t *status) {
	fwrite(f->buf, 1, f->sz, stdout);
	status->debug_bytes_written += f->sz;
}

// Debug strings are always printed to console and optionally to file
static void debug_frame_handler(serial_frame_t *f, mel_status_t *status) {
	if (print_timestamps_flag) fprintf(stdout, "%lu: ", (unsigned long)time(NULL));
	filter_last_newline(f->buf, f->sz); // Only applies to Linux environments, otherwise nop
	fwrite(f->buf, 1, f->sz, stdout);

	if (status->data_and_debug_file != NULL) {
		FILE *file_out = status->data_and_debug_file;
		if (print_timestamps_flag) fprintf(file_out, "%lu: ", (unsigned long)time(NULL));
		fwrite(f->buf, 1, f->sz, file_out);
		#ifdef ALWAYS_FLUSH_FILE
		fflush(file_out);
		#endif
	}

	status->debug_bytes_written += f->sz;
}

static void data_string_frame_handler(serial_frame_t *f, mel_status_t *status) {
	if (print_data_stdout_flag) {
		if (print_timestamps_flag) fprintf(stdout, "%lu: ", (unsigned long)time(NULL));
		fwrite(f->buf, 1, f->sz, stdout);
	}

	if (status->tx_socket_fd > 0) {
		my_socket_send(status->tx_socket_fd, MASTER_MEL_DEFAULT_UDP_SEND_PORT, f->buf, f->sz);
	}

	if (status->data_and_debug_file != NULL) {
		FILE *file_out = status->data_and_debug_file;
		if (print_timestamps_flag) fprintf(file_out, "%lu: ", (unsigned long)time(NULL));
		fwrite(f->buf, 1, f->sz, file_out);
		#ifdef ALWAYS_FLUSH_FILE
		fflush(file_out);
		#endif
	}

	if (status->data_file != NULL) { fwrite(f->buf, 1, f->sz, status->data_file); }
	status->data_bytes_written += f->sz;
}

static void audio_frame_handler(serial_frame_t *f, mel_status_t *status) {
	uint32_t ret;
	//MY_PRINTF("%s()\r\n", __func__);
	if (status->audio_file == NULL) return;
	ret = fwrite(f->buf, 1, f->sz, status->audio_file);
	if (ret != f->sz) { // Error
		perror("Audio File fwrite() Error: ");
		return;
	}
	status->audio_bytes_written += f->sz;
}

static void send_cmd_boot(int fd, uint8_t *buf) {
	boot_cmd_packet_t pkt = {0};
	pkt.cmd = boot_cmd_boot;
	uint8_t *data = (uint8_t *)&pkt;

	int ret = serial_frame_encode(data, sizeof(pkt), BUF_SZ, buf, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
	if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }

	ret = write(fd, buf, ret);
	if (ret <= 0) {
		perror("Serial device write error");
		return;
	}
}

static void send_cmd_prog(int fd, uint8_t *buf, FILE *bin, uint32_t addr, uint8_t dest, mel_status_t *status) {
	const int offset = sizeof(boot_cmd_packet_t);
	static uint8_t file_buf[PROGRAM_CHUNK_SIZE+sizeof(boot_cmd_packet_t)];
	serial_frame_t f;
	size_t fread_ret;
	int ret;
	boot_cmd_packet_t pkt = {0};
	pkt.cmd = boot_cmd_program;
	uint8_t *data = (uint8_t *)&pkt;
	pkt.arg0 = addr;

	int pad_size;
	if (dest == DEST_H7)
		pad_size = 32;
	else if (dest == DEST_BMS)
		pad_size = 8;

	if (sizeof(boot_cmd_t) != 4) {
		fprintf(stderr, "Warning, enum size (%zu) is unexpected and I suck, will likely fail, please fix\n", sizeof(boot_cmd_t));
	}

	if (sizeof(boot_cmd_packet_t) != 16) {
		fprintf(stderr, "Warning, boot_cmd_packet_t size (%zu) is unexpected and I suck, will likely fail, please fix\n", sizeof(boot_cmd_packet_t));
	}

	if (bin == NULL) {
		fprintf(stderr, "ABORT: NULL programming file\r\n");
		return;
	}

	do {
		memset(file_buf, 0xFF, sizeof(file_buf)); // In case we have to pad
		fread_ret = fread(&file_buf[offset], 1, PROGRAM_CHUNK_SIZE, bin); // Read file
		if (fread_ret < PROGRAM_CHUNK_SIZE) {
			pkt.arg1 = 1; // Indicate last frame of operation
			int mod_check = fread_ret % pad_size;
			if (mod_check != 0)
				fread_ret += pad_size - mod_check; // Pad to word size (4 byte)
		}
		memcpy(file_buf, data, sizeof(pkt)); // Copy in cmd struct header

		// Create frame from header+data
		ret = serial_frame_encode(file_buf, sizeof(pkt)+fread_ret, BUF_SZ, buf, dest, FRAME_TYPE_BOOTLOADER_BIN);
		if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }

		// Write out the serial port
		ret = write(fd, buf, ret);
		if (ret <= 0) {
			perror("Serial device write error");
			caught_stop = true;
			break;
		}

		// Wait for ACK (or something else)
		while(!got_ack && !got_nack && !caught_stop) { get_frames(fd, buf, &f, status); }
		if (caught_stop) break;
		if (got_nack) { fprintf(stderr, "Programming FAILED\r\n"); break; }
		got_ack = 0;

		// Increment address by bytes written.
		pkt.arg0 += PROGRAM_CHUNK_SIZE;
	} while(fread_ret == PROGRAM_CHUNK_SIZE);
	got_ack = 0;
	got_nack = 0;
	return;
}

static void send_cmd_erase(int fd, uint8_t *buf, int start, int end) {
	boot_cmd_packet_t pkt = {0};
	pkt.cmd = boot_cmd_erase;
	uint8_t *data = (uint8_t *)&pkt;

	if (start == 0 && !unsafe_flag) {
		fprintf(stderr,"WARNING: Ignored request to erase bootloader. Override with --allow-unsafe\r\n");
		fprintf(stderr,"Erasing other sectors as requested...\r\n");
		start = 1;
	}

	if (start > end) {
		fprintf(stderr,"ERROR: Start: %u End: %u , start cannot be > end\r\n", start, end);
		caught_stop = true;
		return;
	}

	pkt.arg0 = start;
	pkt.arg1 = end;

	int ret = serial_frame_encode(data, sizeof(pkt), BUF_SZ, buf, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
	if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }

	ret = write(fd, buf, ret);
	if (ret <= 0) {
		perror("Serial device write error");
		return;
	}
}

// HELLO the bootloader cmd.
// static void send_cmd_bms_hello(int fd, uint8_t *buf) {
	// boot_cmd_packet_t pkt = {0};
	// pkt.cmd = boot_cmd_bms_hello;
	// uint8_t *data = (uint8_t *)&pkt;

	// int ret = serial_frame_encode(data, sizeof(pkt), BUF_SZ, buf, DEST_BMS, FRAME_TYPE_BOOTLOADER_BIN);
	// if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }

	// my_write_buf(fd, buf, ret);
// }

// HELLO the frame. Fix me.
static void send_cmd_hello_bms(int fd, uint8_t *buf) {
	int ret = serial_frame_encode(NULL, 0, BUF_SZ, buf, DEST_BMS, FRAME_TYPE_HELLO);
	if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }
	my_write_buf(fd, buf, ret);
}

static void send_cmd_hello(int fd, uint8_t *buf) {
	boot_cmd_packet_t pkt = {0};
	pkt.cmd = boot_cmd_hello;
	uint8_t *data = (uint8_t *)&pkt;

	int ret = serial_frame_encode(data, sizeof(pkt), BUF_SZ, buf, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
	if (ret < 0) { fprintf(stderr, "ENCODE ERROR\r\n"); return; }

	ret = write(fd, buf, ret);
	if (ret <= 0) {
		perror("Serial device write error");
		return;
	}
}

// Process all frames in the buffer and returns when at least one is found
static void get_frames(int fd, uint8_t *buf, serial_frame_t *f, mel_status_t *status) {
	bool got_frame = false;
	while(1) {
		if (got_frame) break;
		ssize_t ret = read(fd, buf, BUF_SZ);
		int decode_ret;
		bool go = false;
		int i=0;

		if (caught_stop) break;
		if (ret < 0) {
			perror("Serial device read error");
			caught_stop = true;
			break;
		}

		do {
			decode_ret = serial_frame_decode(&buf[i], ret-i, f);
			if (decode_ret < 0) { fprintf(stderr,"DECODE ERROR\r\n"); break; }
			i += decode_ret;
			go = parse_frame(f, status);
			if (go) got_frame = true;
		} while (go);
	}
	return;
}

int main(int argc, char **argv) {
	static uint8_t buf[BUF_SZ];	// Main RX buffer
	static char stdin_buf[MY_STDIN_BUF_SZ]; // For reading STDIN to send as data packets
	struct sigaction act;
	int fd = -1;

	FILE *prog_bin_file = NULL;
	FILE *prog_bms_bin_file = NULL;
	uint32_t program_addr = APPLICATION_START_ADDR;

	int erase_start	= -1;
	int erase_end 	= -1;

	int command_field=0;

#ifdef DEFAULT_ALLOW_UNSAFE
	unsafe_flag = 1;
#endif

	mel_status_t status	= {0};
	serial_frame_t f 	= {0};

#ifdef DEFAULT_VERBOSE
	verbose_flag = 1;
#endif

	// Handle args
	while (1)
    {
		int c;
		int ret;
		static struct option long_options[] =
		{
			/* These options set a flag. */
			{"verbose", no_argument, &verbose_flag, 1}, // moot, default on
			{"listen",  no_argument, &listen_flag, 1},
			{"print-timestamps", no_argument, &print_timestamps_flag, 1},
			{"print-data-stdout", no_argument, &print_data_stdout_flag, 1},
			{"data-debug-file", required_argument, 0, BOTH_FILE_OPT},
			{"send-data",  no_argument, &input_stdin_flag, 1},
			{"allow-unsafe", no_argument, &unsafe_flag, 1},
			{"send-hello", no_argument,	0, HELLO_OPT},
			{"bms-send-hello", no_argument,	0, BMS_HELLO_OPT},
			{"boot", no_argument, 0, 'b'},
			{"program-binary", required_argument, 0, 'p'},
			{"program-bms-binary", required_argument, 0, BMS_PROG_OPT},
			{"erase-sector-start", required_argument, 0, ERASE_START_OPT},
			{"erase-sector-end", required_argument,	0, ERASE_END_OPT},
			{"data-file", required_argument, 0, DATA_FILE_OPT},
			{"audio-file", required_argument, 0, 'u'},
			{"program-addr", required_argument, 0, 'a'},
			{"dev", required_argument, 0, 'd'},
			{"udp", no_argument, 0, UDP_OPT},
			{0, 0, 0, 0}
		};
		/* getopt_long stores the option index here. */
		int option_index = 0;
		c = getopt_long(argc, argv, "d:", long_options, &option_index);

		/* Detect the end of the options. */
		if (c == -1)
			break;

		switch (c)
		{
			case 0:
				/* If this option set a flag, do nothing else now. */
				if (long_options[option_index].flag != 0)
					break;
				MY_PRINTF("option %s", long_options[option_index].name);
				if (optarg)
					MY_PRINTF(" with arg %s", optarg);
				MY_PRINTF("\n");
				break;

			case UDP_OPT:
				status.tx_socket_fd = my_socket();
				break;

			case BMS_HELLO_OPT:
				command_field = command_field | boot_cmd_bms_hello;
				break;

			case ERASE_START_OPT:
				erase_start = atoi(optarg);
				command_field = command_field | boot_cmd_erase;
				break;

			case ERASE_END_OPT:
				erase_end = atoi(optarg);
				command_field = command_field | boot_cmd_erase;
				break;

			case HELLO_OPT:
				command_field = command_field | boot_cmd_hello;
				break;

			case BMS_PROG_OPT:
				prog_bms_bin_file = fopen(optarg, "r");
				if (prog_bms_bin_file == NULL) {
					perror("BMS Bin file error");
					goto out;
				}
				if (command_field & boot_cmd_program) {
					printf("Abort: BMS prog and H7 program mutually exclusive\r\n");
					goto out;
				}
				command_field = command_field | boot_cmd_bms_prog;
				break;

			case 'p':
				prog_bin_file = fopen(optarg, "r");
				if (prog_bin_file == NULL) {
					perror("Bin file error");
					goto out;
				}
				if (command_field & boot_cmd_bms_prog) {
					printf("Abort: BMS prog and H7 program mutually exclusive\r\n");
					goto out;
				}
				command_field = command_field | boot_cmd_program;
				break;

			case 'a':
				program_addr = strtoul(optarg, NULL, 16);
				break;

			case 'd':
				fd = open_port(optarg);
				if (fd < 0) goto out;
				set_mf_attr(fd);
				break;

			case DATA_FILE_OPT:
				ret = set_data_file(optarg, &status);
				if (ret < 0) goto out;
				break;

			case BOTH_FILE_OPT:
				ret = set_both_file(optarg, &status);
				if (ret < 0) goto out;
				break;

			case 'u':
				ret = set_audio_file(optarg, &status);
				if (ret < 0) goto out;
				break;

			case 'b':
				command_field = command_field | boot_cmd_boot;
				break;

			case '?':
				/* getopt_long already printed an error message. */
			break;

			default:
				abort();
		}
    }

	/* Print any remaining command line arguments (not options). */
	if (optind < argc)
	{
		MY_PRINTF("non-option ARGV-elements: ");
		while (optind < argc)
			MY_PRINTF("%s ", argv[optind++]);
		putchar('\n');
		goto out;
	}

	if (fd < 0) {
		fprintf(stderr, "Abort: No serial device given, example: --dev=/dev/ttyS4\r\n");
		goto out;
	}

	// Handle args done

	// Catch ctrl-C to exit cleanly
	act.sa_handler = intHandler;
	sigaction(SIGINT, &act, NULL);

	// Handle any requested commands
	while(command_field && !caught_stop) {
		if (command_field & boot_cmd_hello) {
			send_cmd_hello(fd, buf);			// Send cmd
			// Wait for ACK and print and debug frames
			while(!got_ack && !got_nack && !caught_stop) {
				get_frames(fd, buf, &f, &status);
			}
			got_ack  = 0; // Consume ACK and NACK
			got_nack = 0;
			command_field &= ~boot_cmd_hello;	// Mark handled
		}

		if (command_field & boot_cmd_bms_hello) {
			//send_cmd_bms_hello(fd, buf);		// Send cmd
			send_cmd_hello_bms(fd, buf);
			// Wait for ACK and print and debug frames
			while(!got_ack && !got_nack && !caught_stop) {
				get_frames(fd, buf, &f, &status);
			}
			got_ack  = 0; // Consume ACK and NACK
			got_nack = 0;
			command_field &= ~boot_cmd_bms_hello;	// Mark handled
		}

		if (command_field & boot_cmd_erase) {
			if (erase_start < 0) {
				fprintf(stderr, "Abort: Bad or missing erase arg, need --erase_sector_start, optional --erase_sector_end\r\n");
				break;
			}
			// Assume if no end is given to erase only a single sector
			if (erase_end < 0) erase_end = erase_start;

			send_cmd_erase(fd, buf, erase_start, erase_end);
			while(!got_ack && !got_nack && !caught_stop) {
				get_frames(fd, buf, &f, &status);
			}
			got_ack  = 0; // Consume ACK and NACK
			got_nack = 0;
			command_field &= ~boot_cmd_erase;
		}

		if (command_field & boot_cmd_program) {
			send_cmd_prog(fd, buf, prog_bin_file, program_addr, DEST_H7, &status);
			command_field &= ~boot_cmd_program;
		}

		if (command_field & boot_cmd_bms_prog) {
			send_cmd_prog(fd, buf, prog_bms_bin_file, program_addr, DEST_BMS, &status);
			command_field &= ~boot_cmd_bms_prog;
		}

		if (command_field & boot_cmd_boot) {
			send_cmd_boot(fd, buf);
			while(!got_ack && !got_nack && !caught_stop) {
				get_frames(fd, buf, &f, &status);
			}
			got_ack  = 0; // Consume ACK and NACK
			got_nack = 0;
			command_field &= ~boot_cmd_boot;
		}
	}

	// Send any data from stdint
	while(input_stdin_flag && !caught_stop) {
		char *ret = fgets(stdin_buf, MY_STDIN_BUF_SZ, stdin);
		if (ret == NULL) {
			input_stdin_flag=0;
			break;
		}
		int in_len = strnlen(stdin_buf, MY_STDIN_BUF_SZ);

		// Hack: For terminal input, if last char is a newline, replace with null
		// This is mostly for JSON testing. Use UDP socket mode if you are doing anything else.
		// The JSON strings are expected to be null terminated.
		if (stdin_buf[in_len-1] == '\n')
			stdin_buf[in_len-1] = '\0';

		send_data_frame(stdin_buf, in_len, buf, BUF_SZ, fd);
	}

	// Main RX loop
	while(listen_flag && !caught_stop) {
		ssize_t ret = read(fd, buf, BUF_SZ);
		int decode_ret;
		bool go = false;
		int i=0;

		if (caught_stop) break;
		if (ret < 0 ) {
			perror("Serial device read error");
			break;
		}

		do {
			decode_ret = serial_frame_decode(&buf[i], ret-i, &f);
			if (decode_ret < 0) { fprintf(stderr,"DECODE ERROR\r\n"); break; }
			i += decode_ret;
			go = parse_frame(&f, &status);
		} while (go);
	}

	// uint8_t test_buf[1024];
	// int test_ret = serial_frame_encode(NULL, 0, sizeof(test_buf), test_buf, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
	// MY_PRINTF("TEST BUFFER SIZE: %d\r\n", test_ret);
	// write(fd, test_buf, test_ret);
	// sleep(1);

out:
	MY_PRINTF("\r\nDone!\r\n");
	MY_PRINTF("Got %u frames\r\n", status.frame_count);
	MY_PRINTF("%u debug bytes\r\n", status.debug_bytes_written);
	MY_PRINTF("%u data bytes\r\n", status.data_bytes_written);
	MY_PRINTF("%u audio bytes\r\n", status.audio_bytes_written);

	if (f.buf != NULL)
		free(f.buf); // Leftover from unfinished frame

	if (fd >= 0) close(fd);
	if (status.tx_socket_fd > 0) close(status.tx_socket_fd);
	COND_FCLOSE(status.audio_file);
	COND_FCLOSE(status.data_file);
	COND_FCLOSE(status.data_and_debug_file);
	COND_FCLOSE(prog_bin_file);

	return 0;
}
