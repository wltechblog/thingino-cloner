#ifndef FLASH_DESCRIPTOR_H
#define FLASH_DESCRIPTOR_H

#include <stdint.h>

// Flash descriptor structure size
#define FLASH_DESCRIPTOR_SIZE 972

// Flash descriptor header
typedef struct {
    uint32_t magic1;        // 0x00: "GBD\x00" = 0x00474244
    uint32_t count;         // 0x04: Number of entries (20 = 0x14)
    uint32_t flag1;         // 0x08: Unknown flag
    uint32_t flag2;         // 0x0C: Unknown flag
    uint32_t flag3;         // 0x10: Unknown flag
    uint32_t reserved1;     // 0x14: Reserved
    uint32_t reserved2;     // 0x18: Reserved
    uint32_t magic2;        // 0x1C: "ILOP" = 0x494C4F50
} __attribute__((packed)) flash_descriptor_header_t;

// Flash chip information
typedef struct {
    char name[24];          // Flash chip name (e.g., "WIN25Q128JVSQ")
    uint32_t id;            // Flash ID (e.g., 0xEF4018 for Winbond W25Q128)
    uint8_t params[200];    // Flash parameters and commands
} __attribute__((packed)) flash_chip_info_t;

/**
 * Create flash descriptor for WIN25Q128JVSQ (16MB NOR flash)
 *
 * @param buffer Output buffer (must be at least FLASH_DESCRIPTOR_SIZE bytes)
 * @return 0 on success, -1 on error
 */
int flash_descriptor_create_win25q128(uint8_t *buffer);

/**
 * Create flash descriptor for T31x NOR writer_full (GD25Q127CSIG, full_image).
 *
 * This loads the exact 972-byte descriptor captured from the vendor cloner
 * (t31x_sfc_nor_writer_full.cfg) and is used only for the T31x firmware
 * write path.
 *
 * @param buffer Output buffer (must be at least FLASH_DESCRIPTOR_SIZE bytes)
 * @return 0 on success, -1 on error
 */
int flash_descriptor_create_t31x_writer_full(uint8_t *buffer);

/**
 * Create flash descriptor for A1 NOR writer_full (XM25QH128B, full_image).
 *
 * This loads the exact 972-byte descriptor captured from the vendor A1 write
 * and contains the "nor" string that tells the A1 burner to use NOR flash mode.
 *
 * @param buffer Output buffer (must be at least FLASH_DESCRIPTOR_SIZE bytes)
 * @return 0 on success, -1 on error
 */
int flash_descriptor_create_a1_writer_full(uint8_t *buffer);

/**
 * Send flash partition marker ("ILOP" header, 172 bytes) to device.
 *
 * This is the small header chunk the vendor burner sends just before the
 * 972-byte flash descriptor in the T31x writer_full flow. It is sliced from
 * the same descriptor blob loaded by flash_descriptor_create_t31x_writer_full().
 */
thingino_error_t flash_partition_marker_send(usb_device_t *device);

/**
 * Send flash descriptor to device
 *
 * @param device USB device handle
 * @param descriptor Flash descriptor buffer (972 bytes)
 * @return THINGINO_SUCCESS on success, error code otherwise
 */
thingino_error_t flash_descriptor_send(usb_device_t *device, const uint8_t *descriptor);

#endif // FLASH_DESCRIPTOR_H

