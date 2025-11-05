#include "thingino.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// FIRMWARE HANDSHAKE PROTOCOL (40-byte chunk transfers)
// ============================================================================

/**
 * Handshake Structure (8 bytes total - 4 u16 values)
 * Used to verify status of firmware read/write operations
 */
typedef struct {
    uint16_t result_low;      // Lower 16 bits of result code
    uint16_t result_high;     // Upper 16 bits of result code
    uint16_t reserved;        // Reserved field
    uint16_t status;          // Device status
} firmware_handshake_t;

/**
 * Parse handshake response from device
 * Returns 0x0000 for success, 0xFFFF for CRC failure
 */
static uint32_t parse_handshake_result(const firmware_handshake_t* hs) {
    if (!hs) {
        return 0xFFFFFFFF;
    }
    return (uint32_t)hs->result_low | ((uint32_t)hs->result_high << 16);
}

/**
 * Firmware read with 40-byte handshake protocol
 * This implements the proper vendor protocol for reading firmware in chunks
 * 
 * Protocol:
 * 1. Send VR_FW_WRITE1 (0x13) command with 40-byte handshake
 * 2. Receive status handshake from device
 * 3. Perform bulk-in transfer for data
 * 4. Repeat with VR_FW_WRITE2 (0x14) for next chunk
 */
thingino_error_t firmware_handshake_read_chunk(usb_device_t* device, uint32_t chunk_index, 
                                               uint32_t chunk_offset, uint32_t chunk_size,
                                               uint8_t** out_data, int* out_len) {
    if (!device || !out_data || !out_len || chunk_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeReadChunk: index=%u, offset=0x%08X, size=%u\n",
           chunk_index, chunk_offset, chunk_size);

    // NOTE: Unlike NAND_OPS, the handshake protocol does NOT use SetDataAddress/SetDataLength
    // The offset and size are encoded in the 40-byte handshake structure itself
    // Factory tool analysis confirms: no VR_SET_DATA_ADDR (0x01) or VR_SET_DATA_LEN (0x02) calls

    thingino_error_t result;

    // Build 40-byte handshake command
    // Structure based on USB capture from factory tool:
    // Bytes 0-7:   zeros
    // Bytes 8-11:  Flash offset (little-endian)
    // Bytes 12-15: zeros
    // Bytes 16-19: Size (little-endian)
    // Bytes 20-23: zeros
    // Bytes 24-27: 0x00000600 (constant - hex bytes: 00 00 06 00)
    // Bytes 28-31: 0xaf7f0000 (constant - hex bytes: af 7f 00 00)
    // Bytes 32-39: zeros
    uint8_t handshake_cmd[40] = {0};

    // Bytes 0-7: zeros (already zeroed by initialization)

    // Bytes 8-11: Flash offset (little-endian)
    handshake_cmd[8] = (chunk_offset >> 0) & 0xFF;
    handshake_cmd[9] = (chunk_offset >> 8) & 0xFF;
    handshake_cmd[10] = (chunk_offset >> 16) & 0xFF;
    handshake_cmd[11] = (chunk_offset >> 24) & 0xFF;

    // Bytes 12-15: zeros (already zeroed)

    // Bytes 16-19: Chunk size (little-endian)
    handshake_cmd[16] = (chunk_size >> 0) & 0xFF;
    handshake_cmd[17] = (chunk_size >> 8) & 0xFF;
    handshake_cmd[18] = (chunk_size >> 16) & 0xFF;
    handshake_cmd[19] = (chunk_size >> 24) & 0xFF;

    // Bytes 20-23: zeros (already zeroed)

    // Bytes 24-27: Constant pattern 0x00000600 (hex bytes: 00 00 06 00)
    handshake_cmd[24] = 0x00;
    handshake_cmd[25] = 0x00;
    handshake_cmd[26] = 0x06;
    handshake_cmd[27] = 0x00;

    // Bytes 28-31: Constant pattern 0x00007faf (little-endian)
    handshake_cmd[28] = 0xaf;
    handshake_cmd[29] = 0x7f;
    handshake_cmd[30] = 0x00;
    handshake_cmd[31] = 0x00;

    // Bytes 32-39: zeros (already zeroed)

    DEBUG_PRINT("Sending handshake command (40 bytes)...\n");

    // Factory tool analysis: Always use VR_FW_WRITE1 (0x13) for firmware reads
    // VR_FW_WRITE2 (0x14) is used for different initialization commands, not reads
    uint8_t handshake_cmd_code = VR_FW_WRITE1;
    DEBUG_PRINT("Using command: 0x%02X\n", handshake_cmd_code);

    int response_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        handshake_cmd_code, 0, 0, handshake_cmd, 40, NULL, &response_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to send handshake command: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Handshake command sent, waiting for status...\n");

    // Small delay to allow device to process
    usleep(50000); // 50ms

    // Read status handshake from device (8 bytes)
    uint8_t status_buffer[8] = {0};
    int status_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_FW_READ_STATUS2, 0, 0, NULL, 8, status_buffer, &status_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to read status handshake: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (status_len < 8) {
        DEBUG_PRINT("Warning: Incomplete status handshake (%d/8 bytes)\n", status_len);
    }

    // Debug: Print status buffer content
    DEBUG_PRINT("Status buffer: %02X %02X %02X %02X %02X %02X %02X %02X\n",
        status_buffer[0], status_buffer[1], status_buffer[2], status_buffer[3],
        status_buffer[4], status_buffer[5], status_buffer[6], status_buffer[7]);

    // Parse handshake response
    firmware_handshake_t* hs = (firmware_handshake_t*)status_buffer;
    uint32_t hs_result = parse_handshake_result(hs);
    
    DEBUG_PRINT("Handshake result: 0x%08X (low=0x%04X, high=0x%04X, status=0x%04X)\n",
           hs_result, hs->result_low, hs->result_high, hs->status);

    // Check for CRC failure indication (0xFFFF in result fields)
    // NOTE: Device may return 0xFFFF legitimately, so we log but don't fail
    if (hs->result_low == 0xFFFF || hs->result_high == 0xFFFF) {
        DEBUG_PRINT("Warning: Device handshake shows 0xFFFF (may not indicate failure)\n");
    }

    // Wait for device to prepare data for bulk transfer
    usleep(50000); // 50ms delay for device to prepare bulk data

    // Now perform bulk-in transfer to read the actual data
    DEBUG_PRINT("Reading %u bytes of data via bulk-in...\n", chunk_size);

    uint8_t* data_buffer = (uint8_t*)malloc(chunk_size);
    if (!data_buffer) {
        return THINGINO_ERROR_MEMORY;
    }

    int transferred = 0;
    int timeout = 10000; // 10 seconds for bulk transfer
    
    result = usb_device_bulk_transfer(device, ENDPOINT_IN, data_buffer, chunk_size, &transferred, timeout);
    
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Bulk-in transfer failed: %s\n", thingino_error_to_string(result));
        free(data_buffer);
        return result;
    }

    DEBUG_PRINT("Data received: %d/%u bytes\n", transferred, chunk_size);
    DEBUG_PRINT("DEBUG: transferred value after bulk transfer = %d\n", transferred);
    DEBUG_PRINT("First 32 bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
           data_buffer[0], data_buffer[1], data_buffer[2], data_buffer[3],
           data_buffer[4], data_buffer[5], data_buffer[6], data_buffer[7],
           data_buffer[8], data_buffer[9], data_buffer[10], data_buffer[11],
           data_buffer[12], data_buffer[13], data_buffer[14], data_buffer[15],
           data_buffer[16], data_buffer[17], data_buffer[18], data_buffer[19],
           data_buffer[20], data_buffer[21], data_buffer[22], data_buffer[23],
           data_buffer[24], data_buffer[25], data_buffer[26], data_buffer[27],
           data_buffer[28], data_buffer[29], data_buffer[30], data_buffer[31]);

    // CRITICAL: After bulk IN completes, must read final status with VR_FW_READ (0x10)
    // Factory tool analysis shows this is required to acknowledge the transfer
    // and prepare the device for the next operation
    DEBUG_PRINT("Reading final status with VR_FW_READ (0x10)...\n");
    // NOTE: Buffer must be 8 bytes because usb_device_vendor_request always reads 8 bytes when response is provided
    uint8_t final_status[8] = {0};
    int final_status_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_FW_READ, 0, 0, NULL, 8, final_status, &final_status_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Warning: Failed to read final status: %s\n", thingino_error_to_string(result));
        // Don't fail the operation - the data was already received
    } else {
        DEBUG_PRINT("Final status: %02X %02X %02X %02X\n",
               final_status[0], final_status[1], final_status[2], final_status[3]);
    }

    DEBUG_PRINT("DEBUG: transferred value before assignment = %d\n", transferred);

    *out_data = data_buffer;
    *out_len = transferred;

    DEBUG_PRINT("firmware_handshake_read_chunk returning: transferred=%d, *out_len=%d\n", transferred, *out_len);

    return THINGINO_SUCCESS;
}

/**
 * Firmware write with 40-byte handshake protocol
 * 
 * Protocol:
 * 1. Send VR_FW_WRITE1 (0x13) handshake command
 * 2. Bulk-out transfer firmware data chunk
 * 3. Receive status handshake
 * 4. Repeat with VR_FW_WRITE2 (0x14) for next chunk
 */
thingino_error_t firmware_handshake_write_chunk(usb_device_t* device, uint32_t chunk_index,
                                                uint32_t chunk_offset, const uint8_t* data, 
                                                uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeWriteChunk: index=%u, offset=0x%08X, size=%u\n",
           chunk_index, chunk_offset, data_size);

    // Build 40-byte handshake command for write
    uint8_t handshake_cmd[40] = {0};
    
    // Bytes 0-3: Chunk offset (little-endian)
    handshake_cmd[0] = (chunk_offset >> 0) & 0xFF;
    handshake_cmd[1] = (chunk_offset >> 8) & 0xFF;
    handshake_cmd[2] = (chunk_offset >> 16) & 0xFF;
    handshake_cmd[3] = (chunk_offset >> 24) & 0xFF;
    
    // Bytes 20-23: Data size (little-endian)
    handshake_cmd[20] = (data_size >> 0) & 0xFF;
    handshake_cmd[21] = (data_size >> 8) & 0xFF;
    handshake_cmd[22] = (data_size >> 16) & 0xFF;
    handshake_cmd[23] = (data_size >> 24) & 0xFF;
    
    // Bytes 32-39: Vendor pattern + chunk index
    handshake_cmd[32] = 0x06;
    handshake_cmd[33] = 0x00;
    handshake_cmd[34] = 0x05;
    handshake_cmd[35] = 0x7F;
    handshake_cmd[36] = 0x00;
    handshake_cmd[37] = 0x00;
    handshake_cmd[38] = (chunk_index >> 0) & 0xFF;
    handshake_cmd[39] = (chunk_index >> 8) & 0xFF;

    // Send handshake using alternating command codes
    uint8_t handshake_cmd_code = (chunk_index % 2 == 0) ? VR_FW_WRITE1 : VR_FW_WRITE2;
    
    DEBUG_PRINT("Sending write handshake with command 0x%02X...\n", handshake_cmd_code);

    int response_len = 0;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        handshake_cmd_code, 0, 0, handshake_cmd, 40, NULL, &response_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to send write handshake: %s\n", thingino_error_to_string(result));
        return result;
    }

    usleep(50000); // 50ms delay

    // Send actual data via bulk-out
    DEBUG_PRINT("Sending %u bytes of data via bulk-out...\n", data_size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t*)data, data_size, &transferred, 10000);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Bulk-out transfer failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Data sent: %d/%u bytes\n", transferred, data_size);

    usleep(50000); // 50ms delay

    // Read status handshake
    uint8_t status_buffer[8] = {0};
    int status_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
        VR_FW_READ_STATUS2, 0, 0, NULL, 8, status_buffer, &status_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to read write status: %s\n", thingino_error_to_string(result));
        return result;
    }

    firmware_handshake_t* hs = (firmware_handshake_t*)status_buffer;
    uint32_t hs_result = parse_handshake_result(hs);
    
    DEBUG_PRINT("Write status result: 0x%08X\n", hs_result);

    if (hs->result_low == 0xFFFF || hs->result_high == 0xFFFF) {
        DEBUG_PRINT("Write verification failed (CRC mismatch)\n");
        return THINGINO_ERROR_PROTOCOL;
    }

    return THINGINO_SUCCESS;
}

/**
 * Initialize firmware stage with handshake protocol
 */
thingino_error_t firmware_handshake_init(usb_device_t* device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Initializing firmware handshake protocol...\n");

    // Send firmware handshake to initialize protocol
    thingino_error_t result = protocol_fw_handshake(device);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Firmware handshake failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    usleep(100000); // 100ms delay for device to prepare

    return THINGINO_SUCCESS;
}