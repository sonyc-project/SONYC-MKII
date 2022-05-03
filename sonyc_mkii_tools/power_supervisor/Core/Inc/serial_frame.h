#pragma once
#include <stdint.h>

#ifndef FRAME_MAX_SIZE
#define FRAME_MAX_SIZE 2048 // Only used for decoding. Host end should set larger for audio etc.
#endif

#define FRAME_MIN_SIZE 20 // Size of a 0 payload frame

typedef enum {
	NO_ERR			= 0,
	NO_FRAME 		= 1,
	TOO_BIG			= 2,
	BAD_PARM		= 3,
	UNK_ERR			= 4,
	MALFORM			= 5,
	MEM_ERR			= 6,
} frame_err_t;

typedef enum {
	NONE_FLAG	= 0x00,
	FRAME_FOUND = 0x01,
	PARTIAL		= 0x02,
	NO_PAYLOAD	= 0x04,
	CRC_ERROR	= 0x08,
} frame_flag_t;

typedef struct {
	uint32_t sz;		// Payload size (size of buf)
	uint32_t type;		// Payload type
	uint32_t crc32;		// CRC (already checked)
	uint64_t time_us;	// Sender timestamp in microseconds
	uint8_t *buf;		// The decoded buffer
	frame_flag_t flag;	// Flags
	frame_err_t err;	// Error codes, if any
	uint8_t  dest;		// Dest types
} serial_frame_t;

// Frame types, DATA_STRING is typically JSON
enum {
	FRAME_TYPE_DEBUG_STRING		=0,
	FRAME_TYPE_DATA_STRING		=1,
	FRAME_TYPE_BOOTLOADER_BIN	=2,
	FRAME_TYPE_H7_PROTOBUF		=3,
	FRAME_TYPE_BASE_PROTOBUF	=4,
	FRAME_TYPE_F1_PROTOBUF		=5,
	FRAME_TYPE_BIN_AUDIO		=6,
	FRAME_TYPE_ACK				=7,
	FRAME_TYPE_NACK				=8,
	FRAME_TYPE_HELLO			=9,
	FRAME_TYPE_DEBUG_STRING_BMS	=10,
	FRAME_TYPE_BMS_STATS_v7		=11,
};

// Dest types
enum { DEST_BASE=0, DEST_H7=1, DEST_BMS=2 };

int serial_frame_encode_count(const uint8_t *in, uint32_t len_in, uint8_t dest, uint32_t pkt_type);
int serial_frame_encode(const uint8_t *in, uint32_t len_in, uint32_t buf_max, uint8_t *buf, uint8_t dest, uint32_t pkt_type);
int sf_encode_from_struct_count(serial_frame_t *f);
int sf_encode_from_struct(serial_frame_t *f, uint8_t *buf, uint32_t buf_max);
int serial_frame_decode(const uint8_t *in, uint32_t len_in, serial_frame_t *frame);
void serial_frame_reset(void);
void * serial_frame_malloc(size_t size);

// Provided externally e.g., in main.c
void Error_Handler(void);
