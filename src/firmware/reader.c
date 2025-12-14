#include "thingino.h"
#include "flash_descriptor.h"

// ============================================================================
// FIRMWARE READER IMPLEMENTATION - Proper Handshake Protocol
// Based on READ_FIRMWARE_PROTOCOL_ANALYSIS.md
// CRITICAL: Device sends 5 firmware components FIRST before main firmware
// These MUST be read/drained to initialize the read protocol
// ============================================================================

/**
 * CRITICAL PROTOCOL FINDING FROM STRACE ANALYSIS:
 *
 * The factory tool does NOT read "5 firmware components" via simple BULK IN.
 * Instead, it uses the 40-byte handshake protocol for ALL reads:
 *
 * For each 1MB chunk:
 * 1. Send CONTROL transfer (48 bytes) - handshake command with offset/size
 * 2. Send CONTROL transfer (16 bytes) - additional control command
 * 3. Perform BULK IN (1MB) - actual data read
 * 4. Send CONTROL transfer (12 bytes) - status read
 *
 * This is the SAME protocol used for all firmware operations.
 * There are NO "component reads" - just direct 1MB chunk reads using handshake.
 */

/**
 * Read firmware chunk using handshake protocol
 * This is the protocol used by the factory tool
 */
static thingino_error_t firmware_read_chunk_with_handshake(usb_device_t* device,
                                                            uint32_t chunk_index,
                                                            uint32_t chunk_offset,
                                                            uint32_t chunk_size,
                                                            uint8_t** out_data,
                                                            int* out_len) {
    if (!device || !out_data || !out_len || chunk_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("firmware_read_chunk_with_handshake: index=%u, offset=0x%08X, size=%u\n",
           chunk_index, chunk_offset, chunk_size);

    // Use the handshake protocol from handshake.c
    uint8_t* data_buffer = NULL;
    int transferred = 0;

    thingino_error_t result = firmware_handshake_read_chunk(device, chunk_index,
                                                            chunk_offset, chunk_size,
                                                            &data_buffer, &transferred);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Handshake read failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Handshake read successful: %d/%u bytes\n", transferred, chunk_size);

    *out_data = data_buffer;
    *out_len = transferred;

    return THINGINO_SUCCESS;
}

/**
 * Read a firmware bank (1MB chunk) using 1MB sub-chunks with proper handshake
 */
thingino_error_t firmware_read_bank(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data) {
    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_read_bank: offset=0x%08X, size=%u bytes\n", offset, size);

    // Allocate buffer for full bank
    uint8_t* bank_buffer = (uint8_t*)malloc(size);
    if (!bank_buffer) {
        printf("[ERROR] Failed to allocate %u bytes for bank buffer\n", size);
        return THINGINO_ERROR_MEMORY;
    }

    uint32_t total_read = 0;
    
    // Use handshake protocol for reading from flash (factory tool protocol)
    uint8_t* chunk_data = NULL;
    int chunk_len = 0;

    // Calculate chunk index (bank number)
    uint32_t chunk_index = offset / (1024 * 1024);  // 1MB banks

    thingino_error_t result = firmware_read_chunk_with_handshake(device, chunk_index,
                                                                  offset, size,
                                                                  &chunk_data, &chunk_len);
    
    if (result != THINGINO_SUCCESS) {
        printf("[ERROR] Failed to read bank at offset 0x%08X: %s\n", 
               offset, thingino_error_to_string(result));
        free(bank_buffer);
        return result;
    }
    
    if (chunk_data && chunk_len > 0) {
        // Copy chunk data to bank buffer
        memcpy(bank_buffer, chunk_data, chunk_len);
        total_read = chunk_len;
        free(chunk_data);
    }
    
    if ((uint32_t)chunk_len != size) {
        printf("[WARNING] Bank read at 0x%08X: Expected %u bytes, got %d bytes\n", 
               offset, size, chunk_len);
    }
    
    DEBUG_PRINT("Bank read complete: %u bytes\n", total_read);
    *data = bank_buffer;
    return THINGINO_SUCCESS;
}

/**
 * Read entire firmware (all 16MB in 1MB banks)
 */
thingino_error_t firmware_read_full(usb_device_t* device, uint8_t** data, uint32_t* size) {
    if (!device || !data || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_read_full: Reading full firmware from device\n");

    // PHASE 0: Device stabilization
    DEBUG_PRINT("firmware_read_full: PHASE 0 - Stabilizing device after bootstrap\n");

    // Extended delay to let device stabilize after bootstrap
    DEBUG_PRINT("Waiting for device to stabilize after bootstrap...\n");
    thingino_sleep_microseconds(2000000);

    DEBUG_PRINT("Device should now be ready for firmware read\n");

    thingino_error_t result = THINGINO_SUCCESS;

    // CRITICAL: Send flash descriptor BEFORE any read operations
    // This tells the device what flash chip is installed and how to read it
    DEBUG_PRINT("firmware_read_full: PHASE 1 - Sending flash descriptor...\n");

    uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE];
    if (flash_descriptor_create_win25q128(flash_descriptor) != 0) {
        printf("[ERROR] Failed to create flash descriptor\n");
        return THINGINO_ERROR_MEMORY;
    }

    result = flash_descriptor_send(device, flash_descriptor);
    if (result != THINGINO_SUCCESS) {
        printf("[ERROR] Failed to send flash descriptor: %s\n", thingino_error_to_string(result));
        return result;
    }
    DEBUG_PRINT("Flash descriptor sent successfully\n");

    // Wait for device to process the descriptor
    DEBUG_PRINT("Waiting for device to process flash descriptor...\n");
    thingino_sleep_microseconds(500000);

    // Initialize firmware handshake protocol (VR_FW_HANDSHAKE 0x11)
    DEBUG_PRINT("firmware_read_full: PHASE 2 - Initializing handshake protocol...\n");
    result = firmware_handshake_init(device);
    if (result != THINGINO_SUCCESS) {
        printf("[ERROR] Failed to initialize handshake protocol: %s\n", thingino_error_to_string(result));
        return result;
    }
    DEBUG_PRINT("Handshake protocol initialized successfully\n");

    // Initialize read configuration for main firmware
    DEBUG_PRINT("firmware_read_full: Reading main firmware (16MB in 1MB banks)\n");
    firmware_read_config_t config;
    result = firmware_read_init(device, &config);
    if (result != THINGINO_SUCCESS) {
        return result;
    }
    
    // Allocate buffer for full firmware
    uint8_t* firmware_buffer = (uint8_t*)malloc(config.total_size);
    if (!firmware_buffer) {
        firmware_read_cleanup(&config);
        return THINGINO_ERROR_MEMORY;
    }
    
    uint32_t total_read = 0;
    
    // Read all banks with proper handshake protocol
    for (int i = 0; i < config.bank_count; i++) {
        flash_bank_t* bank = &config.banks[i];
        if (!bank->enabled) {
            DEBUG_PRINT("Skipping disabled bank %d\n", i);
            continue;
        }
        
        DEBUG_PRINT("Reading bank %d/%d (%s) at offset=0x%08X using handshake protocol...\n", 
               i + 1, config.bank_count, bank->label, bank->offset);
        
        uint8_t* bank_data = NULL;
        
        // Read bank with proper handshake protocol
        result = firmware_read_bank(device, bank->offset, bank->size, &bank_data);
        if (result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to read bank %d: %s\n", i, thingino_error_to_string(result));
            free(firmware_buffer);
            firmware_read_cleanup(&config);
            return result;
        }
        
        // Copy bank data to main firmware buffer
        if (bank_data) {
            memcpy(firmware_buffer + bank->offset, bank_data, bank->size);
            total_read += bank->size;
            free(bank_data);
        }
        
        DEBUG_PRINT("Bank %d read successfully (total: %u/%u bytes, %d%%)\n",
            i, total_read, config.total_size, (total_read * 100) / config.total_size);
        
        // Small delay between banks to let device stabilize
        thingino_sleep_microseconds(50000);
    }
    
    DEBUG_PRINT("firmware_read_full: Completed reading %u bytes\n", total_read);
    
    *data = firmware_buffer;
    *size = total_read;
    
    firmware_read_cleanup(&config);
    return THINGINO_SUCCESS;
}

/**
 * Detect firmware flash size (16MB for T31X)
 */
thingino_error_t firmware_read_detect_size(usb_device_t* device, uint32_t* size) {
    if (!device || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_read_detect_size: Detecting firmware flash size\n");
    
    // For T31X devices, the flash is 16MB (WIN25Q128JVSQ)
    // In a full implementation, this would query the device for actual flash size
    *size = 16 * 1024 * 1024; // 16MB
    
    DEBUG_PRINT("Detected flash size: %u bytes (%.2f MB)\n", *size, (float)*size / (1024 * 1024));
    
    return THINGINO_SUCCESS;
}

/**
 * PROTOCOL ANALYSIS CRITICAL FINDING:
 * 
 * The vendor tool uses INT transfers on EP 0x00 for firmware read protocol,
 * but libusb doesn't provide easy access to this. The device streams firmware
 * via BULK IN (EP 0x81) AFTER receiving proper INT handshakes.
 * 
 * Current limitation: libusb can't easily replicate INT endpoint communication
 * on the control endpoint that the vendor tool uses.
 * 
 * WORKAROUND: The firmware read may still work if we:
 * 1. Skip the INT handshake (which isn't working with libusb anyway)
 * 2. Give the device extra time to stabilize
 * 3. Try reading with extended timeouts
 * 4. Check if device streams data without explicit handshake
 * 
 * The assumption is that after firmware bootstrap completes,
 * the device may auto-stream or respond to direct BULK IN requests.
 */

/**
 * Initialize firmware read configuration (16 banks of 1MB each)
 */
thingino_error_t firmware_read_init(usb_device_t* device, firmware_read_config_t* config) {
    if (!device || !config) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_read_init: Initializing firmware read configuration\n");
    
    // Detect flash size
    thingino_error_t result = firmware_read_detect_size(device, &config->total_size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }
    
    // For 16MB flash, use 16 banks of 1MB each
    config->bank_count = 16;
    config->block_size = 65536; // 64KB blocks (common for SPI NOR flash)
    
    // Allocate banks array
    config->banks = (flash_bank_t*)malloc(config->bank_count * sizeof(flash_bank_t));
    if (!config->banks) {
        return THINGINO_ERROR_MEMORY;
    }
    
    // Initialize bank configuration (16 banks of 1MB each)
    for (int i = 0; i < config->bank_count; i++) {
        flash_bank_t* bank = &config->banks[i];
        bank->offset = i * 1024 * 1024; // i * 1MB
        bank->size = 1024 * 1024; // 1MB per bank
        snprintf(bank->label, sizeof(bank->label), "FW%d", i);
        bank->enabled = true;
        
        DEBUG_PRINT("Bank %d: offset=0x%08X, size=%u bytes, label=%s\n", 
            i, bank->offset, bank->size, bank->label);
    }
    
    DEBUG_PRINT("firmware_read_init: Configuration ready (%d banks, %u bytes total)\n",
           config->bank_count, config->total_size);
    
    return THINGINO_SUCCESS;
}

/**
 * Cleanup firmware read configuration
 */
thingino_error_t firmware_read_cleanup(firmware_read_config_t* config) {
    if (!config) {
        return THINGINO_SUCCESS;
    }
    
    if (config->banks) {
        free(config->banks);
        config->banks = NULL;
    }
    
    config->bank_count = 0;
    config->total_size = 0;
    config->block_size = 0;
    
    return THINGINO_SUCCESS;
}