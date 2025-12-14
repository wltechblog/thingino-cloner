/**
 * Firmware Writer Implementation
 *
 * Based on USB capture analysis of vendor cloner write operation.
 *
 * Write sequence discovered:
 * 1. DDR configuration (324 bytes)
 * 2. SPL bootloader (~10KB)
 * 3. U-Boot bootloader (~240KB)
 * 4. Partition marker ("ILOP", 172 bytes)
 * 5. Metadata (972 bytes)
 * 6. Firmware data in 128KB chunks
 */

#include "thingino.h"
#include "firmware_database.h"

#define CHUNK_SIZE_128KB (128 * 1024)
#define ENDPOINT_OUT 0x01

// Wait for NOR erase to complete in firmware stage using VR_FW_READ_STATUS2.
//
// The vendor T31x write flow issues status checks (0x16/0x19/0x25/0x26) while
// the burner is erasing and programming. Here we implement a conservative
// polling loop around VR_FW_READ_STATUS2 (0x19) to avoid starting the first
// VR_WRITE/firmware chunk while a full-chip erase is still in progress.
//
// Strategy:
//   - Always wait at least min_wait_ms (default 5s) to give the chip time to
//     begin/finish erase.
//   - Poll VR_FW_READ_STATUS2 every 500ms and log the raw 32-bit status value.
//   - After the minimum wait, proceed once the status has been stable for a
//     few polls or when max_wait_ms is reached.
//   - Any protocol errors are treated as "device busy"; we keep waiting up to
//     max_wait_ms but do not fail the write purely due to status polling.
//
// This mirrors the vendor behavior ("wait on status before writes") without
// depending on undocumented status bit semantics.
static void firmware_wait_for_erase_ready(usb_device_t* device,
                                          int min_wait_ms,
                                          int max_wait_ms) {
    const int poll_interval_ms = 500;  // 0.5s between polls

    if (!device) {
        return;
    }

    // Only do firmware-stage polling for T31-family variants. For other
    // SoCs, fall back to a simple fixed delay.
    if (device->info.stage != STAGE_FIRMWARE ||
        !(device->info.variant == VARIANT_T31 ||
          device->info.variant == VARIANT_T31X ||
          device->info.variant == VARIANT_T31ZX)) {
        thingino_sleep_milliseconds((uint32_t)min_wait_ms);
        return;
    }

    if (min_wait_ms < 0) min_wait_ms = 0;
    if (max_wait_ms < min_wait_ms) max_wait_ms = min_wait_ms;

    printf("Waiting for device to prepare flash (erase) using status polling...\n");

    int elapsed_ms = 0;
    uint32_t last_status = 0;
    int stable_count = 0;
    int have_status = 0;

    while (elapsed_ms < max_wait_ms) {
        uint32_t status = 0;
        thingino_error_t st = protocol_fw_read_status(device, VR_FW_READ_STATUS2, &status);

        if (st == THINGINO_SUCCESS) {
            DEBUG_PRINT("Erase status (VR_FW_READ_STATUS2) at %d ms: 0x%08X\n",
                        elapsed_ms, status);

            if (elapsed_ms >= min_wait_ms) {
                if (!have_status) {
                    have_status = 1;
                    last_status = status;
                    stable_count = 1;
                } else if (status == last_status) {
                    stable_count++;
                } else {
                    // Status changed after minimum wait; assume erase state
                    // transitioned (e.g., from busy to ready).
                    DEBUG_PRINT("Erase status changed from 0x%08X to 0x%08X at %d ms; "
                                "assuming erase complete\n",
                                last_status, status, elapsed_ms);
                    break;
                }

                // If we've seen the same status value a few times after the
                // minimum wait, treat the device as ready.
                if (stable_count >= 3) {
                    DEBUG_PRINT("Erase status stable at 0x%08X for %d polls after %d ms; "
                                "proceeding with write\n",
                                status, stable_count, elapsed_ms);
                    break;
                }
            }
        } else {
            DEBUG_PRINT("Erase status poll error at %d ms: %s\n",
                        elapsed_ms, thingino_error_to_string(st));
        }

        thingino_sleep_milliseconds((uint32_t)poll_interval_ms);
        elapsed_ms += poll_interval_ms;
    }

    if (elapsed_ms >= max_wait_ms) {
        printf("[WARN] Timed out waiting for firmware erase status after %d ms; "
               "continuing with write anyway.\n", elapsed_ms);
    }
}

/**
 * Write firmware to device
 *
 * This implements the complete write sequence as observed from vendor cloner:
 * - Bootstrap device (DDR + SPL + U-Boot)
 * - Send partition marker
 * - Send metadata
 * - Send firmware in 128KB chunks
 */
thingino_error_t write_firmware_to_device(usb_device_t* device,
                                         const char* firmware_file,
                                         const firmware_binary_t* fw_binary) {
    if (!device || !firmware_file) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    printf("Writing firmware to device...\n");
    printf("  Firmware file: %s\n", firmware_file);
    if (fw_binary) {
        printf("  SoC: %s\n", fw_binary->processor);
    }

    // Step 1: Load firmware file
    FILE* file = fopen(firmware_file, "rb");
    if (!file) {
        fprintf(stderr, "Error: Cannot open firmware file: %s\n", firmware_file);
        return THINGINO_ERROR_FILE_IO;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long firmware_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (firmware_size <= 0) {
        fprintf(stderr, "Error: Invalid firmware file size\n");
        fclose(file);
        return THINGINO_ERROR_FILE_IO;
    }
    if ((unsigned long)firmware_size > (unsigned long)UINT32_MAX) {
        fprintf(stderr, "Error: Firmware file too large (%ld bytes)\n", firmware_size);
        fclose(file);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint32_t firmware_size_u = (uint32_t)firmware_size;
    printf("  Firmware size: %u bytes (%.1f KB)\n", firmware_size_u, firmware_size_u / 1024.0);

    // Allocate buffer for firmware
    uint8_t* firmware_data = (uint8_t*)malloc(firmware_size_u);
    if (!firmware_data) {
        fprintf(stderr, "Error: Cannot allocate memory for firmware\n");
        fclose(file);
        return THINGINO_ERROR_MEMORY;
    }

    // Read firmware
    size_t bytes_read = fread(firmware_data, 1, firmware_size_u, file);
    fclose(file);

    if (bytes_read != (size_t)firmware_size_u) {
        fprintf(stderr, "Error: Failed to read firmware file\n");
        free(firmware_data);
        return THINGINO_ERROR_FILE_IO;
    }

    // Step 2: Prepare flash address and length for firmware write
    printf("\nStep 1: Preparing firmware write (address/length)...\n");

    // Vendor T31 capture shows main firmware written starting at flash 0x00008010
    uint32_t flash_base_address = 0x00008010;

    DEBUG_PRINT("Setting flash base address with SetDataAddress: 0x%08lX\n",
                (unsigned long)flash_base_address);

    thingino_error_t result;
    // For T31 firmware-stage write, vendor capture shows VR_SET_DATA_ADDR
    // with bmRequestType=0x40, bRequest=0x01, wValue=0x8010, wIndex=0 for
    // base address 0x00008010. This differs from the generic
    // protocol_set_data_address splitting used in bootrom stage, so we
    // issue the control transfer directly here to match the vendor
    // semantics exactly.
    int addr_resp_len = 0;
    result = usb_device_vendor_request(device, REQUEST_TYPE_OUT,
                                       VR_SET_DATA_ADDR,
                                       (uint16_t)(flash_base_address & 0xFFFF),
                                       0,
                                       NULL, 0, NULL, &addr_resp_len);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error: Failed to set flash base address: %s\n",
                thingino_error_to_string(result));
        free(firmware_data);
        return result;
    }

    // Set total firmware length once, before first chunk (matches vendor tool behavior)
    DEBUG_PRINT("Setting total firmware size with SetDataLength: %lu bytes\n", (unsigned long)firmware_size_u);
    result = protocol_set_data_length(device, firmware_size_u);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error: Failed to set total firmware length: %s\n", thingino_error_to_string(result));
        free(firmware_data);
        return result;
    }

    // Wait for device to prepare (erase flash, etc.). The first full-chip
    // erase on a fresh or previously-programmed device can take significantly
    // longer than subsequent runs, so rely on firmware status polling instead
    // of a fixed sleep. We still enforce a minimum 5s delay and cap the wait
    // at 60s for safety.
    firmware_wait_for_erase_ready(device, 5000 /* min_wait_ms */, 60000 /* max_wait_ms */);

    // NOTE: VR_FW_HANDSHAKE (0x11) should be sent earlier (after U-Boot load),
    // not here. Vendor capture shows it's sent once at frame 13467, way before
    // the firmware chunks start at frame 14051. Sending it here puts device in bad state.

    // Step 3: Send firmware in 128KB chunks with proper protocol commands
    printf("\nStep 2: Writing firmware data...\n");

    uint32_t bytes_written = 0;
    uint32_t chunk_num = 0;

    while (bytes_written < firmware_size_u) {
        uint32_t chunk_size = CHUNK_SIZE_128KB;
        if (bytes_written + chunk_size > firmware_size_u) {
            chunk_size = firmware_size_u - bytes_written;
        }

        chunk_num++;
        uint32_t chunk_offset = bytes_written;  // offset relative to flash_base_address
        uint32_t current_flash_addr = flash_base_address + chunk_offset;

        printf("  Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n",
               chunk_num, chunk_size, current_flash_addr,
               (bytes_written + chunk_size) * 100.0 / firmware_size_u);

        // Send handshake command + data for this chunk
        // This function handles both the 40-byte handshake and the bulk data transfer
        result = firmware_handshake_write_chunk(device, chunk_num - 1,  // 0-based index
                                               chunk_offset,
                                               firmware_data + bytes_written,
                                               chunk_size);
        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "Error: Failed to write chunk %u\n", chunk_num);
            free(firmware_data);
            return result;
        }

        bytes_written += chunk_size;
    }

    // Flush cache after all writes
    printf("\nFlushing cache...\n");
    result = protocol_flush_cache(device);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Warning: Failed to flush cache\n");
        // Don't fail on flush error
    }

    printf("\nFirmware write complete!\n");
    printf("  Total written: %u bytes in %u chunks\n", bytes_written, chunk_num);

    free(firmware_data);
    return THINGINO_SUCCESS;
}

/**
 * Send bulk data to device
 */
thingino_error_t send_bulk_data(usb_device_t* device, uint8_t endpoint,
                                const uint8_t* data, uint32_t size) {
    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    int transferred = 0;
    int result = libusb_bulk_transfer(device->handle, endpoint,
                                     (uint8_t*)data, size,
                                     &transferred, 5000);  // 5 second timeout

    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Bulk transfer failed: %s\n", libusb_error_name(result));
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    if (transferred != (int)size) {
        fprintf(stderr, "Incomplete transfer: sent %d of %u bytes\n", transferred, size);
        return THINGINO_ERROR_TRANSFER_FAILED;
    }

    return THINGINO_SUCCESS;
}

