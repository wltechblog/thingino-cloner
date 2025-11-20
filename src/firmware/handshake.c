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

// Compute CRC32 over a buffer (matches standard Ethernet CRC32)
static uint32_t firmware_crc32(const uint8_t* data, uint32_t length) {
    if (!data || length == 0) {
        return 0;
    }

    uint32_t crc = CRC32_INITIAL;

    for (uint32_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

// Drain log messages from bulk IN endpoint(s) after a write chunk.
// Vendor tool issues many IN transfers on 0x81/0x82 between chunks; we
// approximate this by reading small chunks with short timeouts and
// ignoring the contents.
__attribute__((unused))
static void firmware_drain_logs(usb_device_t* device, uint8_t endpoint, int max_reads)
{
    if (!device) {
        return;
    }

    uint8_t buf[512];
    int transferred = 0;

    for (int i = 0; i < max_reads; ++i) {
        thingino_error_t res = usb_device_bulk_transfer(device, endpoint,
                                                        buf, sizeof(buf),
                                                        &transferred, 10);
        if (res != THINGINO_SUCCESS || transferred <= 0) {
            break;
        }

        DEBUG_PRINT("FW log: ep=0x%02X, %d bytes\n", endpoint, transferred);
    }
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

    // CRITICAL: After bulk IN completes, we must tickle the firmware with
    // VR_FW_READ (0x10). Factory tool analysis shows this is required to
    // acknowledge the transfer and prepare the device for the next operation.
    // Vendor trace shows bmRequestType=0xC0 and wLength=4.
    DEBUG_PRINT("Sending final VR_FW_READ (0x10) with 4-byte status...\n");
    uint8_t final_status[4] = {0};
    int final_status_len = 0;

    int ctrl_result = libusb_control_transfer(device->handle,
        REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0,
        final_status, sizeof(final_status), 5000);

    if (ctrl_result < 0) {
        DEBUG_PRINT("Warning: VR_FW_READ after read chunk failed: %d (%s)\n",
                    ctrl_result, libusb_error_name(ctrl_result));
        // Don't fail the operation - the data was already received
    } else {
        final_status_len = ctrl_result;
        DEBUG_PRINT("Final FW_READ status: len=%d, bytes=%02X %02X %02X %02X\n",
                    final_status_len,
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
 * Protocol (as observed in vendor T31 doorbell capture):
 * 1. Set total firmware size with VR_SET_DATA_LEN (once, before first chunk)
 * 2. For each chunk:
 *    - Send VR_WRITE (0x12) with 40-byte handshake structure
 *    - Bulk-out transfer firmware data chunk
 *    - Device logs progress via bulk-IN and FW_READ
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
    // Layout derived from vendor T31 write capture vendor_write_real_20251118_122703.pcap
    // and extended with T41N/T41 (XBurst2) trailer from t41_full_write_20251119_185651.pcap:
    //   Bytes  0-9 : zeros
    //   Bytes 10-11: Chunk offset in 64KB units (little-endian)
    //   Bytes 12-17: zeros
    //   Bytes 18-19: Chunk size in 64KB units (for 128KB: 0x0002, for 64KB: 0x0001)
    //   Bytes 20-23: zeros
    //   Bytes 24-27: 0x00000600 (00 00 06 00)
    //   Bytes 28-31: ~CRC32(chunk_data) (little-endian)
    //   Bytes 32-39: Constant trailer (T31: 20 FB 00 08 A2 77 00 00,
    //                                   T41N: F0 17 00 44 70 7A 00 00)
    //
    // Verified pattern from complete capture with 128 chunks on T31 and
    // from t41_full_write_20251119_185651.pcap on T41N.
    uint8_t handshake_cmd[40] = {0};

    // Bytes 10-11: chunk offset in 64KB units (little-endian).
    // For offset=0x00000000 (chunk 0) this is 0x0000; for offset=0x00020000
    // (chunk 1) this is 0x0002, matching vendor handshake #2.
    uint32_t chunk_units = (chunk_offset >> 16);  // offset / 0x10000 (64KB)
    handshake_cmd[10] = (chunk_units >> 0) & 0xFF;
    handshake_cmd[11] = (chunk_units >> 8) & 0xFF;

    // Bytes 18-19: chunk size in 64KB units (little-endian).
    // Vendor writes 0x0002 here for 128KB chunks on T31 and 0x0001 for 64KB
    // chunks on T41N.
    uint32_t size_units = (data_size + 0xFFFF) >> 16;  // ceil(size / 64KB)
    handshake_cmd[18] = (size_units >> 0) & 0xFF;
    handshake_cmd[19] = (size_units >> 8) & 0xFF;

    // Bytes 24-27: Constant pattern 0x00000600
    handshake_cmd[24] = 0x00;
    handshake_cmd[25] = 0x00;
    handshake_cmd[26] = 0x06;
    handshake_cmd[27] = 0x00;

    // Bytes 28-31: Inverted CRC32 of chunk data (little-endian)
    // Vendor captures show this equals ~crc32(chunk_data)
    uint32_t crc = firmware_crc32(data, data_size);
    uint32_t crc_inv = ~crc;

    handshake_cmd[28] = (crc_inv >> 0) & 0xFF;
    handshake_cmd[29] = (crc_inv >> 8) & 0xFF;
    handshake_cmd[30] = (crc_inv >> 16) & 0xFF;
    handshake_cmd[31] = (crc_inv >> 24) & 0xFF;

    // Bytes 32-39: Constant trailer observed in vendor write handshakes.
    // T31-family uses 20 FB 00 08 A2 77 00 00 while T41N/T41 uses
    // F0 17 00 44 70 7A 00 00.
    if (device->info.stage == STAGE_FIRMWARE &&
        device->info.variant == VARIANT_T41) {
        // T41N/T41 (XBurst2) trailer from t41_full_write_20251119_185651.pcap
        handshake_cmd[32] = 0xF0;
        handshake_cmd[33] = 0x17;
        handshake_cmd[34] = 0x00;
        handshake_cmd[35] = 0x44;
        handshake_cmd[36] = 0x70;
        handshake_cmd[37] = 0x7A;
        handshake_cmd[38] = 0x00;
        handshake_cmd[39] = 0x00;
    } else {
        // Default T31-family trailer
        handshake_cmd[32] = 0x20;
        handshake_cmd[33] = 0xFB;
        handshake_cmd[34] = 0x00;
        handshake_cmd[35] = 0x08;
        handshake_cmd[36] = 0xA2;
        handshake_cmd[37] = 0x77;
        handshake_cmd[38] = 0x00;
        handshake_cmd[39] = 0x00;
    }

    // Send handshake using VR_WRITE (0x12), as seen in vendor write capture
    // VR_FW_WRITE1/2 (0x13/0x14) are used for other initialization commands
    uint8_t handshake_cmd_code = VR_WRITE;

    DEBUG_PRINT("Sending write handshake with command 0x%02X...\n", handshake_cmd_code);

    // Debug: dump handshake bytes for analysis
    DEBUG_PRINT("Handshake bytes:");
    for (int i = 0; i < 40; i++) {
        if (i % 8 == 0) {
            DEBUG_PRINT("\n  ");
        }
        fprintf(stderr, "%02X ", handshake_cmd[i]);
    }
    fprintf(stderr, "\n");

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
    // Use a generous timeout for firmware-stage bulk-out writes. Some burner
    // firmwares aggressively NAK while erasing/programming, so a 1s timeout
    // can expire before the host reports any bytes transferred. We allow up
    // to ~6 seconds for a 64KB chunk to match the protocol timeouts used in
    // other parts of the stack.
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t*)data,
                                      data_size, &transferred, 6000);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Bulk-out transfer failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("Data sent: %d/%u bytes\n", transferred, data_size);

    // Give device time to start processing the chunk
    DEBUG_PRINT("Waiting 100ms for device to start processing chunk...\n");
    usleep(100000); // 100ms delay

    // For T41-family firmware-stage writes, the vendor T41N capture shows a
    // VR_FW_READ (0x10) after each chunk. On T31 this times out and breaks the
    // pipeline, so we only issue it on T41 while keeping the timing-based
    // behavior for other variants.
    if (device->info.stage == STAGE_FIRMWARE &&
        device->info.variant == VARIANT_T41) {
        DEBUG_PRINT("Sending per-chunk VR_FW_READ (0x10) for T41...\n");

        // For T41/T41N, vendor traces show a 4-byte VR_FW_READ (0x10) after
        // each write chunk. We issue this directly via libusb to avoid the
        // generic usb_device_vendor_request() retry logic, which can turn a
        // simple timeout into a long sequence of retries.
        uint8_t status[4] = {0};
        int ctrl_result = libusb_control_transfer(device->handle,
            REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0,
            status, sizeof(status), 1000);

        if (ctrl_result < 0) {
            DEBUG_PRINT("Warning: per-chunk VR_FW_READ for T41 failed: %s\n",
                        libusb_error_name(ctrl_result));
            // Don't fail the operation here; the data chunk was already sent.
        } else {
            DEBUG_PRINT("Per-chunk VR_FW_READ status: len=%d, bytes=%02X %02X %02X %02X\n",
                        ctrl_result,
                        status[0], status[1], status[2], status[3]);
        }
    }

    // Drain log traffic from bulk-IN endpoint 0x81
    // The vendor capture shows many bulk-IN transfers happen after status check
    DEBUG_PRINT("Draining logs from bulk-IN endpoint 0x81...\n");

    int total_drained = 0;
    // Limit to a small number of quick polls to avoid slowing down the write
    for (int i = 0; i < 16; i++) {
        uint8_t log_buf[512];
        int log_transferred = 0;

        int log_result = libusb_bulk_transfer(device->handle, ENDPOINT_IN,
            log_buf, sizeof(log_buf), &log_transferred, 5);  // 5ms timeout

        if (log_result == LIBUSB_ERROR_TIMEOUT || log_transferred == 0) {
            break;
        }

        if (log_result == 0 && log_transferred > 0) {
            total_drained += log_transferred;
        }
    }

    if (total_drained > 0) {
        DEBUG_PRINT("Drained %d bytes of logs\n", total_drained);
    }

    // Give device more time to finish processing chunk before next handshake
    // Tightened from 1000ms to 300ms to speed up full-image writes while
    // still giving firmware time to progress internal state.
    DEBUG_PRINT("Waiting 300ms for device to finish processing chunk...\n");
    usleep(300000); // 300ms delay

    return THINGINO_SUCCESS;

}


/**
 * Firmware write with 40-byte handshake protocol for A1 boards.
 *
 * A1 uses a different handshake layout than T31/T41, with 1MB chunks and
 * a unique trailer. Pattern derived from a1_full_write_20251119_221121.pcap:
 *   Bytes  0-7 : zeros
 *   Bytes  8-11: Constant 0x00000600 (00 00 06 00)
 *   Bytes 12-15: Chunk offset in bytes (little-endian)
 *   Bytes 16-19: Chunk size 0x00100000 (00 00 10 00) = 1MB
 *   Bytes 20-23: ~CRC32(chunk_data) (little-endian)
 *   Bytes 24-31: zeros
 *   Bytes 32-39: A1 trailer (30 24 00 D4 02 75 00 00)
 */
thingino_error_t firmware_handshake_write_chunk_a1(usb_device_t* device, uint32_t chunk_index,
                                                  uint32_t chunk_offset, const uint8_t* data,
                                                  uint32_t data_size) {
    if (!device || !data || data_size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("FirmwareHandshakeWriteChunkA1: index=%u, offset=0x%08X, size=%u\n",
           chunk_index, chunk_offset, data_size);

    // Build 40-byte handshake command for write (A1-specific layout)
    // Pattern from a1_full_write_20251119_221121.pcap showing 1MB chunks:
    //   Bytes  0-7 : zeros
    //   Bytes  8-11: Constant 0x00000600 (00 00 06 00)
    //   Bytes 12-15: Chunk offset in bytes (little-endian)
    //   Bytes 16-19: Chunk size 0x00100000 (00 00 10 00) = 1MB
    //   Bytes 20-23: ~CRC32(chunk_data) (little-endian)
    //   Bytes 24-31: zeros
    //   Bytes 32-39: A1 trailer (30 24 00 D4 02 75 00 00)
    uint8_t handshake_cmd[40] = {0};

    // Bytes 8-11: Constant pattern 0x00000600
    handshake_cmd[8] = 0x00;
    handshake_cmd[9] = 0x00;
    handshake_cmd[10] = 0x06;
    handshake_cmd[11] = 0x00;

    // Bytes 12-15: Chunk offset in bytes (little-endian)
    handshake_cmd[12] = (chunk_offset >> 0) & 0xFF;
    handshake_cmd[13] = (chunk_offset >> 8) & 0xFF;
    handshake_cmd[14] = (chunk_offset >> 16) & 0xFF;
    handshake_cmd[15] = (chunk_offset >> 24) & 0xFF;

    // Bytes 16-19: Chunk size in bytes (little-endian)
    // A1 uses 1MB (0x100000) chunks
    handshake_cmd[16] = (data_size >> 0) & 0xFF;
    handshake_cmd[17] = (data_size >> 8) & 0xFF;
    handshake_cmd[18] = (data_size >> 16) & 0xFF;
    handshake_cmd[19] = (data_size >> 24) & 0xFF;

    // Compute inverted CRC32 of chunk data
    uint32_t crc = firmware_crc32(data, data_size);
    uint32_t crc_inv = ~crc;

    // Bytes 20-23: Inverted CRC32 of chunk data (little-endian)
    handshake_cmd[20] = (crc_inv >> 0) & 0xFF;
    handshake_cmd[21] = (crc_inv >> 8) & 0xFF;
    handshake_cmd[22] = (crc_inv >> 16) & 0xFF;
    handshake_cmd[23] = (crc_inv >> 24) & 0xFF;

    // Bytes 24-31: zeros (already initialized)

    // Bytes 32-39: A1-specific trailer from vendor capture
    handshake_cmd[32] = 0x30;
    handshake_cmd[33] = 0x24;
    handshake_cmd[34] = 0x00;
    handshake_cmd[35] = 0xD4;
    handshake_cmd[36] = 0x02;
    handshake_cmd[37] = 0x75;
    handshake_cmd[38] = 0x00;
    handshake_cmd[39] = 0x00;

    // Send handshake using VR_WRITE (0x12)
    uint8_t handshake_cmd_code = VR_WRITE;

    DEBUG_PRINT("Sending A1 write handshake with command 0x%02X...\n", handshake_cmd_code);

    // Debug: dump handshake bytes for analysis
    DEBUG_PRINT("A1 Handshake bytes:");
    for (int i = 0; i < 40; i++) {
        if (i % 8 == 0) {
            DEBUG_PRINT("\n  ");
        }
        fprintf(stderr, "%02X ", handshake_cmd[i]);
    }
    fprintf(stderr, "\n");

    int response_len = 0;
    thingino_error_t result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
        handshake_cmd_code, 0, 0, handshake_cmd, 40, NULL, &response_len);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to send A1 write handshake: %s\n", thingino_error_to_string(result));
        return result;
    }

    usleep(50000); // 50ms delay

    // Send actual data via bulk-out
    DEBUG_PRINT("[A1] Sending %u bytes of data via bulk-out...\n", data_size);

    int transferred = 0;
    result = usb_device_bulk_transfer(device, ENDPOINT_OUT, (uint8_t*)data,
                                      data_size, &transferred, 6000);

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("[A1] Bulk-out transfer failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("[A1] Data sent: %d/%u bytes\n", transferred, data_size);

    // Give device time to start and finish processing the chunk.
    DEBUG_PRINT("[A1] Waiting 300ms for device to process chunk...\n");
    usleep(300000); // 300ms delay

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