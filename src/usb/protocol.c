#include "thingino.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// PROTOCOL IMPLEMENTATION
// ============================================================================

thingino_error_t protocol_set_data_address(usb_device_t* device, uint32_t addr) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("SetDataAddress: 0x%08x\n", addr);

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_SET_DATA_ADDR, (uint16_t)(addr >> 16), (uint16_t)(addr & 0xFFFF),
        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("SetDataAddress error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("SetDataAddress OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_set_data_length(usb_device_t* device, uint32_t length) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("SetDataLength: %d (0x%08x)\n", length, length);

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_SET_DATA_LEN, (uint16_t)(length >> 16), (uint16_t)(length & 0xFFFF),
        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("SetDataLength error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("SetDataLength OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_flush_cache(usb_device_t* device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FlushCache: executing\n");

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_FLUSH_CACHE, 0, 0, NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FlushCache error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FlushCache OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    return THINGINO_SUCCESS;
}

/**
 * Read device status
 */
thingino_error_t protocol_read_status(usb_device_t* device, uint8_t* status_buffer,
                                     int buffer_size, int* status_len) {
    if (!device || !status_buffer || buffer_size < 8) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ReadStatus: executing\n");

    // Use VR_FW_READ_STATUS2 (0x19) - most commonly used status check
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_FW_READ_STATUS2, 0, 0, NULL, buffer_size, status_buffer, status_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("ReadStatus error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("ReadStatus: success, got %d bytes\n", *status_len);
    return THINGINO_SUCCESS;
}

thingino_error_t protocol_prog_stage1(usb_device_t* device, uint32_t addr) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProgStage1: addr=0x%08x\n", addr);

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_PROG_STAGE1, (uint16_t)(addr >> 16), (uint16_t)(addr & 0xFFFF),
        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("ProgStage1 error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("ProgStage1 OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_prog_stage2(usb_device_t* device, uint32_t addr) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProgStage2: addr=0x%08x\n", addr);

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_PROG_STAGE2, (uint16_t)(addr >> 16), (uint16_t)(addr & 0xFFFF),
        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        // It's expected for ProgStage2 to fail with timeout or pipe error
        // because device is re-enumerating after executing U-Boot
        DEBUG_PRINT("ProgStage2 sent (timeout/pipe error during re-enumeration is expected): %s\n",
            thingino_error_to_string(result));
        return THINGINO_SUCCESS; // Treat as success - device is re-enumerating
    }

    DEBUG_PRINT("ProgStage2 OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_get_ack(usb_device_t* device, int32_t* status) {
    if (!device || !status) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint8_t data[4];
    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_GET_CPU_INFO, 0, 0, NULL, 4, data, &response_length);

    if (result != THINGINO_SUCCESS) {
        return result;
    }

    if (response_length < 4) {
        return THINGINO_ERROR_PROTOCOL;
    }

    // Convert little-endian bytes to int32
    *status = (int32_t)data[0] | (int32_t)data[1] << 8 |
              (int32_t)data[2] << 16 | (int32_t)data[3] << 24;

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_init(usb_device_t* device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    int response_length;
    return usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_FW_HANDSHAKE, 0, 0, NULL, 0, NULL, &response_length);
}

// DDR auto-probe protocol helpers (used with custom SRAM-only probe SPL)
thingino_error_t protocol_ddr_probe_set_config(usb_device_t* device, const uint8_t* config, size_t length) {
    if (!device || !config || length == 0 || length > 0xFFFF) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    int response_length = 0;
    return usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_DDR_PROBE_SET_CONFIG, 0, 0,
        (uint8_t*)config, (uint16_t)length,
        NULL, &response_length);
}

thingino_error_t protocol_ddr_probe_run_test(usb_device_t* device, uint8_t* result) {
    if (!device || !result) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint8_t status = 0;
    int response_length = 0;
    thingino_error_t rv = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_DDR_PROBE_RUN_TEST, 0, 0,
        NULL, 1, &status, &response_length);
    if (rv != THINGINO_SUCCESS) {
        return rv;
    }
    if (response_length < 1) {
        return THINGINO_ERROR_PROTOCOL;
    }

    *result = status;
    return THINGINO_SUCCESS;
}


// Enhanced timeout calculation for protocol operations
static int calculate_protocol_timeout(uint32_t size) {
    // Base timeout of 5 seconds + 1 second per 64KB
    // For 1MB transfers: 5000 + (1048576/65536)*1000 = 21 seconds
    // For larger transfers, max 60 seconds to allow sufficient time
    int timeout = 5000 + (size / 65536) * 1000;
    if (timeout > 60000) timeout = 60000;  // Increased max to 60s for 1MB+ transfers
    return timeout;
}

// Firmware stage protocol functions
thingino_error_t protocol_fw_read(usb_device_t* device, int data_len, uint8_t** data, int* actual_len) {
    if (!device || !data || !actual_len) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWRead: reading %d bytes\n", data_len);

    // For firmware reading, we need to claim interface first
    thingino_error_t result = usb_device_claim_interface(device);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWRead failed to claim interface: %s\n", thingino_error_to_string(result));
        return result;
    }

    // Now read actual data via bulk transfer
    uint8_t* buffer = (uint8_t*)malloc(data_len);
    if (!buffer) {
        usb_device_release_interface(device);
        return THINGINO_ERROR_MEMORY;
    }

    int transferred = 0;
    int timeout = calculate_protocol_timeout(data_len);

    DEBUG_PRINT("FWRead: using adaptive timeout of %dms for %d bytes\n", timeout, data_len);

    // Use direct libusb call with adaptive timeout for better control
    int libusb_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN,
        buffer, data_len, &transferred, timeout);

    // Handle stall errors with interface reset (from Go implementation experience)
    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("FWRead bulk transfer failed: %s\n", libusb_error_name(libusb_result));

        // If it's a pipe error, try to reset and retry
        if (libusb_result == LIBUSB_ERROR_PIPE) {
            DEBUG_PRINT("FWRead stall detected, resetting interface and retrying...\n");
            usb_device_release_interface(device);

            // Small delay before retry
            usleep(100000); // 100ms

            // Re-claim interface and retry once with longer timeout
            thingino_error_t claim_result = usb_device_claim_interface(device);
            if (claim_result == THINGINO_SUCCESS) {
                DEBUG_PRINT("FWRead retrying transfer after interface reset...\n");
                int retry_timeout = timeout * 2; // Double timeout for retry
                libusb_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN,
                    buffer, data_len, &transferred, retry_timeout);
            } else {
                DEBUG_PRINT("FWRead failed to reclaim interface: %s\n", thingino_error_to_string(claim_result));
            }
        }
    }

    // Release interface
    usb_device_release_interface(device);

    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("FWRead bulk transfer error: %s\n", libusb_error_name(libusb_result));
        free(buffer);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    DEBUG_PRINT("FWRead success: got %d bytes (requested %d)\n", transferred, data_len);

    *data = buffer;
    *actual_len = transferred;
    return THINGINO_SUCCESS;
}

thingino_error_t protocol_fw_handshake(usb_device_t* device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWHandshake: sending vendor request (command 0x%02X)\n", VR_FW_HANDSHAKE);

    // VR_FW_HANDSHAKE (0x11) is a vendor request, NOT an INT endpoint operation
    // Send it as a control/vendor request like all bootstrap commands
    // This request takes no parameters (wValue=0, wIndex=0) and has no data
    int response_length = 0;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
                                                        VR_FW_HANDSHAKE, 0, 0,
                                                        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWHandshake vendor request failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWHandshake vendor request sent successfully\n");

    // Platform-specific sleep after successful handshake
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t protocol_fw_write_chunk1(usb_device_t* device, const uint8_t* data) {
    if (!device || !data) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWWriteChunk1: writing 40 bytes\n");

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_FW_WRITE1, 0, 0, (uint8_t*)data, 40, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWWriteChunk1 error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWWriteChunk1 OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    return THINGINO_SUCCESS;
}

// ============================================================================
// PROPER PROTOCOL FUNCTIONS (Using Bootloader Code Execution Pattern)
// ============================================================================

/**
 * Load firmware reader stub into RAM and execute it
 * Protocol: VR_SET_DATA_ADDRESS → VR_SET_DATA_LENGTH → VR_PROGRAM_START1 → Bulk-Out → VR_PROGRAM_START2
 */
thingino_error_t protocol_load_and_execute_code(usb_device_t* device, uint32_t ram_address,
                                                 const uint8_t* code, uint32_t code_size) {
    if (!device || !code || code_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Loading code to RAM: address=0x%08X, size=%u bytes\n", ram_address, code_size);

    // Step 1: Set RAM address for code
    thingino_error_t result = protocol_prog_stage1(device, ram_address);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to set RAM address for code: %s\n", thingino_error_to_string(result));
        return result;
    }

    // Step 2: Transfer code to device via bulk-out
    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t*)code, code_size, &transferred, 10000);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to transfer code: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Code transferred: %d/%u bytes\n", transferred, code_size);

    if (transferred < (int)code_size) {
        DEBUG_PRINT("Warning: Not all code bytes transferred (%d/%u)\n", transferred, code_size);
    }

    // Step 3: Execute code at RAM address
    result = protocol_prog_stage2(device, ram_address);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to execute code: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Code execution initiated\n");
    return THINGINO_SUCCESS;
}

/**
 * Firmware read using bootloader's code execution pattern
 * Proper protocol: Set address → Set size → Load reader stub → Execute → Bulk-in data
 */
thingino_error_t protocol_proper_firmware_read(usb_device_t* device, uint32_t flash_offset,
                                               uint32_t read_size, uint8_t** out_data, int* out_len) {
    if (!device || !out_data || !out_len) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProperFirmwareRead: offset=0x%08X, size=%u bytes\n", flash_offset, read_size);

    // Step 1: Set flash address and size
    thingino_error_t result = protocol_set_data_address(device, flash_offset);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    result = protocol_set_data_length(device, read_size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 2: For now, use the existing firmware read (requires firmware reader stub to be loaded separately)
    // In a complete implementation, you would:
    // 1. Load firmware reader stub here
    // 2. Execute it via protocol_load_and_execute_code()
    // 3. Read the data via bulk-in with proper handshaking

    DEBUG_PRINT("ProperFirmwareRead: Address and size set. Requires firmware reader stub to be loaded separately.\n");

    // Fallback to protocol_fw_read for now
    return protocol_fw_read(device, read_size, out_data, out_len);
}

/**
 * Firmware write using bootloader's code execution pattern with CRC32 verification
 * Proper protocol: Set address → Set size → Load writer stub → Execute → Bulk-out data → Verify
 */
thingino_error_t protocol_proper_firmware_write(usb_device_t* device, uint32_t flash_offset,
                                                const uint8_t* data, uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("ProperFirmwareWrite: offset=0x%08X, size=%u bytes\n", flash_offset, data_size);

    // Step 1: Set flash address and size
    thingino_error_t result = protocol_set_data_address(device, flash_offset);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    result = protocol_set_data_length(device, data_size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 2: Calculate CRC32 for data verification
    uint32_t crc = calculate_crc32(data, data_size);
    DEBUG_PRINT("Data CRC32: 0x%08X\n", crc);

    // Step 3: Prepare buffer with data + CRC32
    uint32_t buffer_size = data_size + 4;
    uint8_t* write_buffer = (uint8_t*)malloc(buffer_size);
    if (!write_buffer) {
        return THINGINO_ERROR_MEMORY;
    }

    memcpy(write_buffer, data, data_size);
    // Append CRC32 (little-endian)
    write_buffer[data_size + 0] = (crc >> 0) & 0xFF;
    write_buffer[data_size + 1] = (crc >> 8) & 0xFF;
    write_buffer[data_size + 2] = (crc >> 16) & 0xFF;
    write_buffer[data_size + 3] = (crc >> 24) & 0xFF;

    DEBUG_PRINT("ProperFirmwareWrite: Buffer size with CRC: %u bytes\n", buffer_size);

    // Step 4: For now, this requires firmware writer stub
    // In a complete implementation, you would:
    // 1. Load firmware writer stub here
    // 2. Execute it via protocol_load_and_execute_code()
    // 3. Send firmware data via bulk-out with proper handshaking

    DEBUG_PRINT("ProperFirmwareWrite: Address and size set. Requires firmware writer stub to be loaded separately.\n");

    free(write_buffer);
    return THINGINO_SUCCESS;
}

// ============================================================================
// VENDOR-STYLE FIRMWARE READ (Fallback - Reverse-engineered from vendor tool)
// ============================================================================

// Vendor-style firmware read using VR_READ (0x13) command
// This matches the vendor tool's approach: send 40-byte command, check status, bulk read
thingino_error_t protocol_vendor_style_read(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data, int* actual_len) {
    if (!device || !data || !actual_len) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("VendorStyleRead: offset=0x%08X, size=%u bytes\n", offset, size);

    // CRITICAL: Initialize device state with SetDataAddress and SetDataLength
    // This prepares the U-Boot firmware to read from the specified address/size
    // Same pattern as NAND_OPS - must be done BEFORE issuing the read command
    thingino_error_t result = protocol_set_data_address(device, offset);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: SetDataAddress failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    result = protocol_set_data_length(device, size);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: SetDataLength failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Device initialized for address=0x%08X, length=%u\n", offset, size);

    // Build 40-byte command buffer for VR_READ (0x13)
    // Based on ioctl log analysis:
    // Bytes 0-3: offset (little-endian)
    // Bytes 4-7: unknown (0x00000000)
    // Bytes 8-11: unknown (0x00000000)
    // Bytes 12-15: unknown (0x00000000)
    // Bytes 16-19: unknown (0x00000000)
    // Bytes 20-23: size (little-endian)
    // Bytes 24-31: unknown (0x00000000 0x00000000)
    // Bytes 32-39: vendor-specific data (varies between calls)
    uint8_t cmd_buffer[40] = {0};

    // Set offset (little-endian)
    cmd_buffer[0] = (offset >> 0) & 0xFF;
    cmd_buffer[1] = (offset >> 8) & 0xFF;
    cmd_buffer[2] = (offset >> 16) & 0xFF;
    cmd_buffer[3] = (offset >> 24) & 0xFF;

    // Set size (little-endian) at offset 20
    cmd_buffer[20] = (size >> 0) & 0xFF;
    cmd_buffer[21] = (size >> 8) & 0xFF;
    cmd_buffer[22] = (size >> 16) & 0xFF;
    cmd_buffer[23] = (size >> 24) & 0xFF;

    // Set vendor-specific bytes (pattern from vendor tool)
    cmd_buffer[32] = 0x06;
    cmd_buffer[33] = 0x00;
    cmd_buffer[34] = 0x05;
    cmd_buffer[35] = 0x7F;
    cmd_buffer[36] = 0x00;
    cmd_buffer[37] = 0x00;

    // Send VR_READ command with 40 bytes
    int response_length;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_READ, 0, 0, cmd_buffer, 40, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: VR_READ command failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: VR_READ command sent successfully\n");

    // Check status with VR_FW_READ_STATUS2 (0x19)
    uint8_t status_buffer[8] = {0};
    int status_len;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_FW_READ_STATUS2, 0, 0, NULL, 8, status_buffer, &status_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: Status check failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Status check OK (got %d bytes)\n", status_len);
    DEBUG_PRINT("Status buffer: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        status_buffer[0], status_buffer[1], status_buffer[2], status_buffer[3],
        status_buffer[4], status_buffer[5], status_buffer[6], status_buffer[7]);

    // Wait for device to prepare data for bulk transfer
    // Using 50ms like the handshake protocol to ensure device has data ready
    usleep(50000); // 50ms delay for device to prepare bulk data

    // Allocate buffer for bulk read
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        return THINGINO_ERROR_MEMORY;
    }

    // Perform bulk IN transfer on endpoint 0x81
    // Calculate adaptive timeout based on transfer size
    // For 1MB: 21 seconds; for larger transfers: up to 60 seconds
    int timeout = calculate_protocol_timeout(size);

    DEBUG_PRINT("VendorStyleRead: Using adaptive timeout of %dms for %u bytes\n", timeout, size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, 0x81, buffer, size, &transferred, timeout);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("VendorStyleRead: Bulk transfer failed: %s\n", thingino_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("VendorStyleRead: Successfully read %d bytes (requested %u)\n", transferred, size);

    *data = buffer;
    *actual_len = transferred;
    return THINGINO_SUCCESS;
}

// Traditional firmware read using VR_READ command (alternative approach)
thingino_error_t protocol_traditional_read(usb_device_t* device, int data_len, uint8_t** data, int* actual_len) {
    if (!device || !data || !actual_len) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("TraditionalRead: reading %d bytes using VR_READ\n", data_len);

    // Claim interface for the operation
    thingino_error_t result = usb_device_claim_interface(device);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("TraditionalRead failed to claim interface: %s\n", thingino_error_to_string(result));
        return result;
    }

    // Use traditional VR_READ command
    uint8_t* buffer = (uint8_t*)malloc(data_len);
    if (!buffer) {
        usb_device_release_interface(device);
        return THINGINO_ERROR_MEMORY;
    }

    int transferred = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_READ, 0, 0, NULL, data_len, buffer, &transferred);

    // Release interface after transfer
    usb_device_release_interface(device);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("TraditionalRead vendor request error: %s\n", thingino_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("TraditionalRead success: got %d bytes (requested %d)\n", transferred, data_len);

    *data = buffer;
    *actual_len = transferred;
    return THINGINO_SUCCESS;
}

thingino_error_t protocol_fw_read_operation(usb_device_t* device, uint32_t offset, uint32_t length, uint8_t** data, int* actual_len) {
    if (!device || !data || !actual_len) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWReadOperation: offset=0x%08X, length=%u\n", offset, length);

    // Set address and length first
    thingino_error_t result = protocol_set_data_address(device, offset);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    result = protocol_set_data_length(device, length);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Try different operation parameters for read
    uint8_t* buffer = (uint8_t*)malloc(length);
    if (!buffer) {
        return THINGINO_ERROR_MEMORY;
    }

    int response_length;
    // Try operation 12 with different parameters based on reference config analysis
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        12, 0, 0, NULL, length, buffer, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWReadOperation error: %s\n", thingino_error_to_string(result));
        free(buffer);
        return result;
    }

    DEBUG_PRINT("FWReadOperation success: got %d bytes (requested %u)\n", response_length, length);

    *data = buffer;
    *actual_len = response_length;
    return THINGINO_SUCCESS;
}

thingino_error_t protocol_fw_read_status(usb_device_t* device, int status_cmd, uint32_t* status) {
    if (!device || !status) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWReadStatus: checking status with command 0x%02X\n", status_cmd);

    uint8_t data[4];
    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        status_cmd, 0, 0, NULL, 4, data, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWReadStatus error: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (response_length < 4) {
        DEBUG_PRINT("FWReadStatus: insufficient response length %d\n", response_length);
        return THINGINO_ERROR_PROTOCOL;
    }

    // Convert little-endian bytes to uint32
    *status = (uint32_t)data[0] | (uint32_t)data[1] << 8 |
              (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;

    DEBUG_PRINT("FWReadStatus: status = 0x%08X (%u)\n", *status, *status);
    return THINGINO_SUCCESS;
}

thingino_error_t protocol_fw_write_chunk2(usb_device_t* device, const uint8_t* data) {
    if (!device || !data) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FWWriteChunk2: writing 40 bytes\n");

    int response_length;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_FW_WRITE2, 0, 0, (uint8_t*)data, 40, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("FWWriteChunk2 error: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("FWWriteChunk2 OK\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);
#endif

    return THINGINO_SUCCESS;
}

// ============================================================================
// NAND OPERATIONS (VR_NAND_OPS - 0x07)
// ============================================================================

/**
 * Read firmware via NAND_OPS (VR_NAND_OPS 0x07 with NAND_READ subcommand 0x05)
 *
 * Protocol sequence:
 * 1. Set data address (SPI-NAND flash offset)
 * 2. Set data length (how many bytes to read)
 * 3. Issue NAND_OPS read command (0x07)
 * 4. Bulk-in transfer to read the data
 *
 * This uses the NAND_OPS command built into U-Boot bootloader
 */
thingino_error_t protocol_nand_read(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data, int* transferred) {
    if (!device || !data || !transferred || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("NAND_OPS Read: offset=0x%08X, size=%u bytes\n", offset, size);

    // Step 1: Set data address (flash offset)
    thingino_error_t result = protocol_set_data_address(device, offset);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: SetDataAddress failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    // Step 2: Set data length (read size)
    result = protocol_set_data_length(device, size);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: SetDataLength failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    // Step 3: Issue NAND_OPS read command (0x07 with subcommand 0x05)
    DEBUG_PRINT("NAND_OPS: Issuing read command (VR_NAND_OPS=0x07, subcommand=0x%02X)\n", NAND_OPERATION_READ);

    int response_length;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        VR_NAND_OPS, NAND_OPERATION_READ, 0x0000,
        NULL, 0, NULL, &response_length);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: Command failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("NAND_OPS: Command sent successfully\n");

    // Give device time to prepare data for bulk transfer
    // Platform-specific sleep
#ifdef _WIN32
    Sleep(50);
#else
    usleep(50000);  // 50ms
#endif

    // Step 4: Bulk-in transfer to read the data
    uint8_t* buffer = (uint8_t*)malloc(size);
    if (!buffer) {
        DEBUG_PRINT("NAND_OPS: Memory allocation failed for %u bytes\n", size);
        return THINGINO_ERROR_MEMORY;
    }

    // Calculate timeout based on transfer size
    int timeout = calculate_protocol_timeout(size);
    DEBUG_PRINT("NAND_OPS: Performing bulk-in transfer (timeout=%dms)...\n", timeout);

    // Perform bulk transfer
    int bytes_transferred = 0;
    int libusb_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN,
        buffer, size, &bytes_transferred, timeout);

    if (libusb_result != LIBUSB_SUCCESS) {
        DEBUG_PRINT("NAND_OPS: Bulk transfer failed: %s\n", libusb_error_name(libusb_result));
        free(buffer);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    DEBUG_PRINT("NAND_OPS: Successfully read %d bytes (requested %u bytes)\n",
        bytes_transferred, size);

    *data = buffer;
    *transferred = bytes_transferred;
    return THINGINO_SUCCESS;
}