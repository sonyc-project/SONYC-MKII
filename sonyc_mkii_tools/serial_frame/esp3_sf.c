
/*
Serial Frames implemented with ESP3 packet structure
Implemented from EnOceanSerialProtocol3.pdf
https://www.enocean.com/esp
*/

/*
ESP3 packet structure through the serial port.
Protocol bytes are generated and sent by the application
Sync = 0x55
CRC8H
CRC8D
1 2 1 1 1 u16DataLen + u8OptionLen 1
+------+------------------+---------------+-----------+-----------+-------------/------------+-----------+
| 0x55 | u16DataLen | u8OptionLen | u8Type | CRC8H | DATAS | CRC8D |
+------+------------------+---------------+-----------+-----------+-------------/------------+-----------+
DATAS structure:
u16DataLen u8OptionLen
+--------------------------------------------+----------------------+
| Data | Optional |
+--------------------------------------------+----------------------+
*/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "serial_frame.h"

#ifndef FRAME_MAX_SIZE
#define FRAME_MAX_SIZE 2048 // Only used for decoding. Host end should set larger for audio etc.
#endif

#define htons(x) __builtin_bswap16(x)

// Toggle the 5th bit (starting from 0)
//enum { FLAG_FLAG=0x7E, FLAG_ESC=0x7D, TOGGLE_BIT=0x20 };
enum { ESP3_SYNC=0x55 };

typedef enum {
	START=0, SYNC_ON, HEADER_CHECK, DATAX, DATAY, CRCD, DONE
} frame_state_t;

static uint8_t out[FRAME_MAX_SIZE];	// Working buffer
static uint32_t in_idx;				// index into INPUT buffer
static uint32_t out_idx;			// index into OUT buffer
static frame_state_t state;	// State of decoding, preserved over calls
static unsigned data_count;

static const uint8_t u8CRC8Table[256] = {
	0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
	0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
	0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
	0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
	0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
	0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
	0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
	0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
	0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
	0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
	0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
	0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
	0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
	0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
	0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
	0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
	0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
	0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
	0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
	0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
	0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
	0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
	0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
	0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
	0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
	0x76, 0x71, 0x78, 0x7f, 0x6A, 0x6d, 0x64, 0x63,
	0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
	0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
	0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
	0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8D, 0x84, 0x83,
	0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
	0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};

#define proccrc8(u8CRC, u8Data) (u8CRC8Table[u8CRC ^ u8Data])

static uint8_t compute_crc8(const uint8_t *buf, unsigned len) {
	uint8_t u8CRC = 0;
	for (int i = 0 ; i < len ; i++)
		u8CRC = proccrc8(u8CRC, buf[i]);
	return u8CRC;
}

// Returns true on CRC match
static bool check_crc8(uint8_t *buf, unsigned len, uint8_t pkt_crc) {
	uint8_t my_crc = compute_crc8(buf, len);
	return (my_crc == pkt_crc);
}

// Resets frame processing state
void serial_frame_reset(void) { out_idx = 0; state = START; data_count = 0; }

// Call on input serial stream
// Returns: -1 on error, 0 on non-event, else index of *next* byte in input buffer
int serial_frame_decode(const uint8_t *in, uint32_t len_in, serial_frame_t *frame) {
	frame_err_t err = NO_ERR;
	frame_flag_t flag = NONE_FLAG;
	int ret = 0;

	// Get the stupid stuff out of the way...
	if (len_in == 0) {
		err = NO_FRAME;
		goto out;
	}

	if (in == NULL || frame == NULL) {
		err = BAD_PARM;
		ret = -1;
		goto out;
	}

	// Reset input buffer stats
	in_idx = 0;

	uint16_t data_len=0;
	uint8_t type=0;
	//uint8_t opt_len=1;
	uint8_t dest;
	uint8_t crcd;

	// One byte at a time
	while(state != DONE) {

		if (in_idx >= len_in) goto out;

		// Find the SYNC byte
		if (state == START && in[in_idx] == ESP3_SYNC) {
			state = SYNC_ON;
			out[out_idx++] = ESP3_SYNC; // Copy the SYNC byte to keep offsets consistent with the spec
		}
		else if (state == SYNC_ON) {
			out[out_idx++] = in[in_idx];
			if (out_idx == 6) state = HEADER_CHECK;
		}
		else if (state == HEADER_CHECK) {
			uint8_t crc8_header;
			bool crc_check_ret;
			crc8_header = out[5];
			crc_check_ret = check_crc8( &out[1], 4, crc8_header );
			if (!crc_check_ret) serial_frame_reset();
			else {
				data_len = (out[1]<<8) | out[2];
				type = out[4];
				if (data_len == 0) state = DATAY;
				else state = DATAX;
			}
		}
		else if (state == DATAX) {
			out[out_idx++] = in[in_idx];
			data_count++;
			if (data_count == data_len)
				state = DATAY;
		}
		else if (state == DATAY) {
			out[out_idx++] = in[in_idx];
			dest = in[in_idx];
			state = CRCD;
		}
		else if (state == CRCD) {
			out[out_idx++] = in[in_idx];
			crcd = in[in_idx];
			state = DONE;
		}

		in_idx++;
	}

#ifdef _DEBUG
	if (state != DONE) return -1; // Sanity check, should never happen
#endif
	flag |= FRAME_FOUND;
	ret = in_idx;
	memset(frame, 0, sizeof(serial_frame_t)); // Reset the frame
	frame->dest = dest;
	frame->type = type;
	frame->sz = data_len;

	if (data_len == 0) {
		flag |= NO_PAYLOAD;
		frame->sz  = 0;
		frame->buf = NULL;
		serial_frame_reset();
		goto out;
	}

	frame->buf = (uint8_t *) serial_frame_malloc(frame->sz); // CALLER MUST FREE
	if (frame->buf != NULL) {
		memcpy(frame->buf, &out[6], frame->sz);
	}
	else {
		err = MEM_ERR;
		ret = -1;
		goto out;
	}
	serial_frame_reset();

out:
	frame->err = err;
	frame->flag = flag;
	return ret;
}

// Typically would be used for re-encoding
int sf_encode_from_struct(serial_frame_t *f, uint8_t *buf, uint32_t buf_max) {
	uint8_t *in = f->buf;
	uint32_t len_in = f->sz;
	uint8_t dest = f->dest;
	uint32_t pkt_type = f->type;
	return serial_frame_encode(in, len_in, buf_max, buf, dest, pkt_type);
}

// Returns what the size of a frame with the encoded struct would be
int sf_encode_from_struct_count(serial_frame_t *f) {
	uint8_t *in = f->buf;
	uint32_t len_in = f->sz;
	uint8_t dest = f->dest;
	uint32_t pkt_type = f->type;
	return serial_frame_encode_count(in, len_in, dest, pkt_type);
}

// [FLAG DEST TYPE DATA-N TIMESTAMP CRC32 FLAG]
// Params: Input buffer (payload) to be encoded, its length, and max sized output buffer
// Returns: Length of encoded buffer
int serial_frame_encode(const uint8_t *in, uint32_t len_in, uint32_t buf_max, uint8_t *buf, uint8_t dest, uint32_t pkt_type) {
	unsigned bytes = 0;
	uint8_t crc_H, crc_D;

	if (in == NULL && len_in > 0) return -1;
	if (len_in > 0xFFFF) return -1; // Too big
	if (len_in + 8 > buf_max) return -1; // Provided buffer too small. Overhead is 8 bytes.

	// Starting SYNC byte
	buf[bytes++] = ESP3_SYNC;

	// Data Length, ESP3 format is big-endian (network byte order)
	buf[bytes++] = (htons(len_in))    &0xFF;
	buf[bytes++] = (htons(len_in)>>8) &0xFF;

	// Optional Length always 1 because we put our 'dest' byte here
	buf[bytes++] = 1;

	// Type, only 1 byte.
	if (pkt_type & 0xFFFFFF00) return -1;
	buf[bytes++] = pkt_type & 0xFF;

	// CRC8 of 4 header bytes (not including sync)
	crc_H = compute_crc8(&buf[1], 4);
	buf[bytes++] = crc_H;

	// The Data
	for(int i=0; i<len_in; i++) { buf[bytes++] = in[i]; }

	// Shove the dest byte in the optional data section
	buf[bytes++] = dest;

	// CRC8 of data + optional data
	crc_D = compute_crc8(&buf[6], len_in+1);
	buf[bytes++] = crc_D;

	return bytes;
}

// Returns: Length if given data was encoded
// A little janky, wasn't feeling well when I made this
int serial_frame_encode_count(const uint8_t *in, uint32_t len_in, uint8_t dest, uint32_t pkt_type) {
	return len_in + 8;
}
