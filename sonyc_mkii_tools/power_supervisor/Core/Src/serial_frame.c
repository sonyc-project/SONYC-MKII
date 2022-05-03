
/*
A trivial HDLC-like framing system
Uses 0x7E (flag) and 0x7D (escape) ONLY
Frame format:
[FLAG DEST TYPE DATA-N CRC32 TIMESTAMP FLAG]

Where
FLAG: 0x7E (1-byte) delimits the frame at both ends
DEST: 1-byte destination address
TYPE: 4-byte describes frame type and payload
DATA-N: Variable length payload
CRC: 32-bit CRC, ethernet polynomial (matching STM32H7 CRC hardware)
TIMESTAMP: Sender local clock in microseconds

CRC shall be taken over the DECODED PAYLOAD ONLY
Byte-order is littlen endian

TIMESTAMP avoids CRC computation delay

This is not super complicated as it serves only as a wrapper.
Anything smarter goes into the payload.
It is already too smart.

TYPICAL max packet shall be 4k
ABSOLUTE max packet, after encoding, is 8k

Nathan Stohs 2020-02-27
nathan.stohs@samraksh.com
The Samraksh Company
http://www.samraksh.com
*/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "serial_frame.h"

typedef enum {
	START=0, FLAG_ON, ESC_ON, DONE
} frame_state_t;

// Toggle the 5th bit (starting from 0)
enum { FLAG_FLAG=0x7E, FLAG_ESC=0x7D, TOGGLE_BIT=0x20 };

static uint8_t out[FRAME_MAX_SIZE];	// Working buffer
static uint32_t in_idx;				// index into INPUT buffer
static uint32_t out_idx;			// index into OUT buffer
static frame_state_t state;			// State of decoding, preserved over calls

// TODO
static uint32_t compute_crc32(const uint8_t *buf __attribute__ ((unused)), unsigned len __attribute__ ((unused))) {
	return 0;
}

// Returns true on CRC match
static bool check_crc32(uint8_t *buf, unsigned len, uint32_t pkt_crc) {
	uint32_t my_crc = compute_crc32(buf, len);
	return (my_crc == pkt_crc);
}

// Returns true if progress
static bool decode_start(const uint8_t *in, uint32_t len_in) {
	serial_frame_reset();
	// Find the flag
	while(in_idx < len_in) {
		if (in[in_idx++] == FLAG_FLAG) {
			state = FLAG_ON;
			return true;
		}
	}
	return false;
}

// We have an esc and need to peek at the next byte (if we have it)
static bool process_esc(const uint8_t *in, uint32_t len_in) {
	uint8_t next;
	state = ESC_ON;
	if (in_idx >= len_in) return false; // No next byte available, wait for later
	next = in[in_idx++];

	if (next == FLAG_FLAG) { // aborted frame case
		state = FLAG_ON;
		out_idx = 0; // Toss decoded data from frame
		return true;
	}

	next ^= TOGGLE_BIT;
	out[out_idx++] = next;
	if (out_idx == FRAME_MAX_SIZE) out_idx--; // Lazy, just don't let it overflow
	state = FLAG_ON; // Leaving ESC_ON state
	return true;
}

// Walk the input and copy
static bool decode_frame(const uint8_t *in, uint32_t len_in) {
	if (state == ESC_ON) return process_esc(in, len_in);
	while(in_idx < len_in) {
		uint8_t x = in[in_idx++];
		switch(x) {
			case FLAG_FLAG: state = DONE; return false;
			case FLAG_ESC: return process_esc(in, len_in);
			default: out[out_idx++] = x;
		}
		if (out_idx == FRAME_MAX_SIZE) out_idx--; // Lazy, just don't let it overflow
	}
	return false;
}

// Resets frame processing state
void serial_frame_reset(void) {
	out_idx = 0;
	state = START;
}

// Call on input serial stream
// Returns: -1 on error, 0 on non-event, else index of *next* byte in input buffer
int serial_frame_decode(const uint8_t *in, uint32_t len_in, serial_frame_t *frame) {
	bool go = true;
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

	while(go) {
		switch(state) {
			case START:		go = decode_start(in, len_in);	break;
			case ESC_ON:
			case FLAG_ON: 	go = decode_frame(in, len_in);	break;
			default: break; // Falls through to error case in next switch
		}

		// Ignore zero size frames due to dropped bytes etc
		// state is going to be a little messy until we re-sync
		if (state == DONE && out_idx == 0) {
			go = true;
			state = FLAG_ON;
		}
	}

	// Mop things up
	if (state == START) { // Nothing found
		err = NO_FRAME;
	} else if (state == FLAG_ON || state == ESC_ON) { // In the middle of a frame
		flag |= PARTIAL;
	} else if (state == DONE) { // Full frame!
		bool crc_ok;
		const unsigned dest_size = sizeof(frame->dest); // 1 byte at start
		const unsigned type_size = sizeof(frame->type);	// bytes at start
		const unsigned crc_size  = sizeof(frame->crc32);	// bytes at end
		const unsigned time_size = sizeof(frame->time_us);// bytes after payload
		const unsigned tot_meta_size  = dest_size + type_size + crc_size + time_size;

		uint32_t payload_size = out_idx - tot_meta_size; // Whatever is left over
		if (out_idx <  tot_meta_size) { err = MALFORM; ret = -1; goto out;}

		// Compute offsets
		unsigned dest_offset	= 0;
		unsigned type_offset 	= dest_size + dest_offset;
		unsigned payload_offset = type_size + type_offset;
		unsigned crc_offset  	= payload_size + payload_offset;
		unsigned time_offset 	= crc_size + crc_offset;

		// Reset the frame
		memset(frame, 0, sizeof(serial_frame_t));

		// Write out the meta data
		memcpy(&frame->dest,    &out[dest_offset], dest_size);
		memcpy(&frame->type,    &out[type_offset], type_size);
		memcpy(&frame->crc32,   &out[crc_offset],  crc_size);
		memcpy(&frame->time_us, &out[time_offset], time_size);

		// Caller must check CRC status
		// Caller should serial_frame_reset() on errors until back sync
		crc_ok = check_crc32(&out[payload_offset], payload_size, frame->crc32);
		if (!crc_ok) {
			// Will keep going in case other frames
			flag |= CRC_ERROR;
		}

		if (payload_size) {
			frame->sz = payload_size;
			frame->buf = (uint8_t *) serial_frame_malloc(frame->sz); // CALLER MUST FREE
			if (frame->buf == NULL) {
				frame->err = MEM_ERR;
				ret = -1;
				goto out;
			}
			memcpy(frame->buf, &out[payload_offset], frame->sz);
		}
		else {
			flag |= NO_PAYLOAD;
			// Just in case...
			frame->sz  = 0;
			frame->buf = NULL;
		}

		flag |= FRAME_FOUND;
		ret = in_idx; // CALLER MUST RE-CALL WITH REMAINING DATA (AFTER IDX, IF ANY) TO CHECK FOR MORE FRAMES
		serial_frame_reset(); // Reset everything for next frame
	} else {
		err = UNK_ERR;
		ret = -1;
	}

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
	uint64_t time_us;
	uint32_t crc;
	// union... I know...
	uint8_t *now = (uint8_t *)(&time_us);
	uint8_t *t = (uint8_t *)(&pkt_type);
	uint8_t *c = (uint8_t *)(&crc);

	if (in == NULL && len_in > 0) return -1;

	// Starting delimiter
	buf[bytes++] = FLAG_FLAG;

	// Dest
	for (unsigned i=0; i<sizeof(dest); i++) {
		uint8_t x = dest; // NOTICE BREAKS PATTERN
		if (bytes >= buf_max) return -1;
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  buf[bytes++] = FLAG_ESC; buf[bytes++] = (x ^ TOGGLE_BIT); break;
			default: buf[bytes++] = x;
		}
	}

	// Type
	for (unsigned i=0; i<sizeof(pkt_type); i++) {
		uint8_t x = t[i];
		if (bytes >= buf_max) return -1;
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  buf[bytes++] = FLAG_ESC; buf[bytes++] = (x ^ TOGGLE_BIT); break;
			default: buf[bytes++] = x;
		}
	}

	// Payload
	for (unsigned i=0; i<len_in; i++) {
		uint8_t x = in[i];
		if (bytes >= buf_max) return -1;
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  buf[bytes++] = FLAG_ESC; buf[bytes++] = (x ^ TOGGLE_BIT); break;
			default: buf[bytes++] = x;
		}
	}

	// CRC
	crc = compute_crc32(in, len_in);
	for (unsigned i=0; i<sizeof(crc); i++) {
		uint8_t x = c[i];
		if (bytes >= buf_max) return -1;
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  buf[bytes++] = FLAG_ESC; buf[bytes++] = (x ^ TOGGLE_BIT); break;
			default: buf[bytes++] = x;
		}
	}

	// Time
	time_us = 8; // TODO
	for (unsigned i=0; i<sizeof(time_us); i++) {
		uint8_t x = now[i];
		if (bytes >= buf_max) return -1;
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  buf[bytes++] = FLAG_ESC; buf[bytes++] = (x ^ TOGGLE_BIT); break;
			default: buf[bytes++] = x;
		}
	}

	// End delimiter
	buf[bytes++] = FLAG_FLAG;
	return bytes;
}

// Returns: Length if given data was encoded
// A little janky, wasn't feeling well when I made this
int serial_frame_encode_count(const uint8_t *in, uint32_t len_in, uint8_t dest, uint32_t pkt_type) {
	unsigned bytes = 0;
	uint64_t time_us;
	uint32_t crc;
	// union... I know...
	uint8_t *now = (uint8_t *)(&time_us);
	uint8_t *t = (uint8_t *)(&pkt_type);
	uint8_t *c = (uint8_t *)(&crc);

	if (in == NULL && len_in > 0) return -1;

	// Starting delimiter
	bytes++;

	// Dest
	for (unsigned i=0; i<sizeof(dest); i++) {
		uint8_t x = 123; // dummy dest
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  bytes+=2; break;
			default: bytes++;
		}
	}

	// Type
	for (unsigned i=0; i<sizeof(pkt_type); i++) {
		uint8_t x = t[i];
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  bytes+=2; break;
			default: bytes++;
		}
	}

	// Payload
	for (unsigned i=0; i<len_in; i++) {
		uint8_t x = in[i];
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  bytes+=2; break;
			default: bytes++;
		}
	}

	// CRC
	crc = compute_crc32(in, len_in);
	for (unsigned i=0; i<sizeof(crc); i++) {
		uint8_t x = c[i];
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  bytes+=2; break;
			default: bytes++;
		}
	}

	// Time
	time_us = 8; // TODO
	for (unsigned i=0; i<sizeof(time_us); i++) {
		uint8_t x = now[i];
		switch(x) {
			case FLAG_FLAG:
			case FLAG_ESC:  bytes+=2; break;
			default: bytes++;
		}
	}

	// End delimiter
	bytes++;
	return bytes;
}
