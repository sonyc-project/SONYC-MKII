
#include <stdbool.h>

#include "main.h"
#include "message.h"

#include "h7boot.pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include "serial.h" // delete me

int message_hander(uint32_t header, uint8_t *buf, uint32_t sz) {
	Example message = Example_init_zero;
	bool status;
	int32_t number;

	if (header != 0) __BKPT(); // sanity check for now
	pb_istream_t stream = pb_istream_from_buffer(buf, sz);
	status = pb_decode_ex(&stream, Example_fields, &message, PB_DECODE_DELIMITED);
	if (!status) { __BKPT(); return -1; }
	number = (int32_t) message.value;
	debug_printf("Your number is %ld\r\n", number);
	return 0;
}