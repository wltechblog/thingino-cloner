/**
 * Embedded Firmware Database
 * 
 * This module provides access to embedded SPL and U-Boot binaries for all
 * supported Ingenic processors. The binaries are compiled directly into the
 * executable, eliminating the need to distribute separate firmware files.
 * 
 * Source: references/cloner-2.5.43-ubuntu_thingino/firmwares/
 */

#ifndef FIRMWARE_DATABASE_H
#define FIRMWARE_DATABASE_H

#include <stddef.h>
#include <stdint.h>

/**
 * Firmware binary structure
 */
typedef struct {
    const char *processor;      // Processor name (e.g., "t31x", "t41")
    const uint8_t *spl_data;    // SPL binary data
    size_t spl_size;            // SPL binary size in bytes
    const uint8_t *uboot_data;  // U-Boot binary data
    size_t uboot_size;          // U-Boot binary size in bytes
} firmware_binary_t;

/**
 * Get firmware binaries for a specific processor
 * 
 * @param processor Processor name (e.g., "t31x", "t41", "a1")
 * @return Pointer to firmware_binary_t structure, or NULL if not found
 */
const firmware_binary_t* firmware_get(const char *processor);

/**
 * List all available firmware binaries
 * 
 * @param count Output parameter for number of firmwares
 * @return Pointer to array of firmware_binary_t structures
 */
const firmware_binary_t* firmware_list(size_t *count);

/**
 * Check if firmware is available for a processor
 * 
 * @param processor Processor name
 * @return 1 if available, 0 if not
 */
int firmware_available(const char *processor);

#endif // FIRMWARE_DATABASE_H

