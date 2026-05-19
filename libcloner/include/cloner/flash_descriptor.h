#ifndef FLASH_DESCRIPTOR_H
#define FLASH_DESCRIPTOR_H

#include <stdint.h>

/* Forward declaration - full definition in src/flash/spi_nor_db.h */
#ifndef SPI_NOR_CHIP_T_DEFINED
#define SPI_NOR_CHIP_T_DEFINED
typedef struct spi_nor_chip spi_nor_chip_t;
#endif

/* Flash descriptor sizes */
#define FLASH_DESCRIPTOR_SIZE     972 /* T31/xburst1: GBD only */
#define FLASH_DESCRIPTOR_SIZE_T32 976 /* T32: GBD with +4 shifted SFC (blocksize+freq fields) */
#define FLASH_DESCRIPTOR_SIZE_XB2 984 /* T40/T41/xburst2: 12-byte RDD(0xC9) + 972 GBD */
#define FLASH_DESCRIPTOR_SIZE_A1  992 /* A1: 12-byte RDD(0x84) + 980 GBD (shifted SFC) */

/** Generate 972-byte descriptor for xburst1 (T20/T31) */
int flash_descriptor_create(const spi_nor_chip_t *chip, uint8_t *buffer);

/** Generate 976-byte descriptor for T32 (shifted SFC with blocksize+freq) */
int flash_descriptor_create_t32(const spi_nor_chip_t *chip, uint8_t *buffer);

/** Generate 984-byte descriptor for xburst2 (T40/T41) */
int flash_descriptor_create_xb2(const spi_nor_chip_t *chip, uint8_t *buffer);

/** Generate 992-byte descriptor for A1 */
int flash_descriptor_create_a1(const spi_nor_chip_t *chip, uint8_t *buffer);

/** Read descriptors (same as write but with SPI_NO_ERASE flag) */
int flash_descriptor_create_read(const spi_nor_chip_t *chip, uint8_t *buffer);
int flash_descriptor_create_t32_read(const spi_nor_chip_t *chip, uint8_t *buffer);
int flash_descriptor_create_xb2_read(const spi_nor_chip_t *chip, uint8_t *buffer);
int flash_descriptor_create_a1_read(const spi_nor_chip_t *chip, uint8_t *buffer);

/** Legacy helpers */
int flash_descriptor_create_win25q128(uint8_t *buffer);
int flash_descriptor_create_t31x_writer_full(uint8_t *buffer);
int flash_descriptor_create_a1_writer_full(uint8_t *buffer);

/** Send partition marker (ILOP, 172 bytes) to device */
thingino_error_t flash_partition_marker_send(usb_device_t *device);

/** Send raw bulk partition marker for T10/T20/T21/T30 */
thingino_error_t flash_partition_marker_send_raw(usb_device_t *device);

/** Send flash descriptor to device (any size) */
thingino_error_t flash_descriptor_send_sized(usb_device_t *device, const uint8_t *descriptor, uint32_t size);

/** Send 972-byte descriptor (T31 default) */
thingino_error_t flash_descriptor_send(usb_device_t *device, const uint8_t *descriptor);

#endif /* FLASH_DESCRIPTOR_H */
