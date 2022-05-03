#pragma once

#define MAX_BANKS 2
#define MAX_SECTORS 8

#define FIRST_SECTOR 0
#define LAST_SECTOR 15

typedef struct {
	uint32_t bank;		// 1 or 2
	uint32_t sector;	// 0-7
	uint32_t len;		// number of sectors
} h7_flash_region_t;

const h7_flash_region_t FLASH_BOOTLOADER_REGION		= {1,0,1};
const h7_flash_region_t FLASH_PROGRAM_REGION_BANK1	= {1,1,7};
const h7_flash_region_t FLASH_PROGRAM_REGION_BANK2	= {2,0,7};
const h7_flash_region_t FLASH_CSHARP_REGION			= {2,7,1};

// Shares a sector with FLASH_PROGRAM_REGION so cannot be erased standalone
// and only programmed once per full erase
// const uint32_t FLASH_CONFIG_ADDR = 0x081DF000;
// const uint32_t FLASH_CONFIG_SIZE = 4096; // 4 kByte
