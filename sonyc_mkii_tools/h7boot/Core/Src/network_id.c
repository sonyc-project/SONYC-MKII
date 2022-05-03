#include <stdint.h>
#include <string.h> // for memcpy()

#include "network_id.h"

#define MY_UID_PTR ((const uint8_t *)0x1FF1E800)

// from http://www.cse.yorku.ca/~oz/hash.html
static uint16_t djb2_hash(const uint8_t *str, uint32_t len) {
	uint16_t hash = 5381; // magic value
	uint32_t i=0;
	uint8_t c;

	while (i<len) {
		c = *str++;
		i++;
		hash = (hash * 33) + c;
	}

	return hash;
}

// DEPRECATED
// Copies CPU serial into ptr
// The copy size should only ever be 12 bytes on this platform.
void GetCPUSerial(uint8_t * ptr, unsigned num_of_bytes ) {
	if (num_of_bytes > 12) num_of_bytes = 12;
	memcpy(ptr, MY_UID_PTR, num_of_bytes);
}

// NEW VERSION
const uint8_t * get_cpu_uid(void) {
	return MY_UID_PTR;
}

// Returns size of UID in BYTES
// For STM32H7, it is 96-bits
const uint32_t get_cpu_uid_len(void) {
	return 12;
}

uint16_t get_cpu_uid_hash16(void) {
	const uint8_t *UID = get_cpu_uid();
	uint32_t len = get_cpu_uid_len();
	return djb2_hash(UID, len);
}
