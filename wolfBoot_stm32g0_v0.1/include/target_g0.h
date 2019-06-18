#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_


/* Example flash partitioning.
 * Ensure that your firmware entry point is
 * at FLASH_AREA_IMAGE_0_OFFSET + 0x100
 */
#   define WOLFBOOT_SECTOR_SIZE			0x3000
#   define WOLFBOOT_PARTITION_BOOT_ADDRESS      0xC800

#ifdef EXT_FLASH
s
/* Test configuration with 1MB external memory */
/* (Addresses are relative to the beginning of the ext)*/

#   define WOLFBOOT_PARTITION_SIZE		0x80000
#   define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x00000
#   define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x80000

#else

/* Test configuration with internal memory - 2kB page granularity*/
#   define WOLFBOOT_PARTITION_SIZE		0x8000
#   define WOLFBOOT_PARTITION_UPDATE_ADDRESS    0x14800
#   define WOLFBOOT_PARTITION_SWAP_ADDRESS      0x1C800

#endif

#endif /* H_TARGETS_TARGET_ */
