#pragma once

// Use 1-31 or >= 128 to avoid printable chars
enum {
	HELLO_OPT			=128,
	ERASE_START_OPT		=129,
	ERASE_END_OPT		=130,
	BMS_HELLO_OPT		=131,
	BMS_PROG_OPT		=132,
	DATA_FILE_OPT		=133,
	BOTH_FILE_OPT		=134,
};

#define APPLICATION_START_ADDR 0x08020400

// Commands are powers of 2 for easy flags
typedef enum {
	boot_cmd_null		=0x0,			// Not used
	boot_cmd_hello		=0x1,
	boot_cmd_erase		=0x2,
	boot_cmd_program	=0x4,
	boot_cmd_boot		=0x8,
	boot_cmd_bms_hello	=0x10,
	boot_cmd_bms_prog	=0x20,
	boot_cmd_fake		=0x80000000,	// To force compiler to use 4-byte enums
} boot_cmd_t;

typedef struct __attribute__((packed)) {
	uint32_t arg0;
	uint32_t arg1;
	uint32_t arg2;
	boot_cmd_t cmd;
} boot_cmd_packet_t;

/*
boot_cmd_null:
No args. Used to signal an empty bootloader packet and provoke App to reboot to bootloader.

boot_cmd_hello:
No args. Bootloader will repy with info string(s) as debug_string frame and finish with ACK.

boot_cmd_erase:
Arg0: Start sector. Arg2: End sector. Both inclusive.
STM32H7 2MB, Bank1: 0-7 Bank2: 8-15 , 128 kiB sectors.
Replies with ACK when done and debug_info frames for status

boot_cmd_program:
Arg0: Write address. Arg1: 1 if final frame. Arg2. Unused.
Replies with ACK when done and debug_info frames for status

boot_cmd_boot:
No args. ACK, reset, and jump to application.

*/