#ifndef H_TARGETS_TARGET_
#define H_TARGETS_TARGET_


/* Example flash partitioning.
 * Ensure that your firmware entry point is
 * at FLASH_AREA_IMAGE_0_OFFSET + 0x100
 */
#define WOLFBOOT_SECTOR_SIZE			    0x100
#define WOLFBOOT_PARTITION_SIZE			    0x1B000

#define WOLFBOOT_PARTITION_BOOT_ADDRESS     0x08000
#define WOLFBOOT_PARTITION_UPDATE_ADDRESS   0x23000
#define WOLFBOOT_PARTITION_SWAP_ADDRESS     0x3E000

#endif
