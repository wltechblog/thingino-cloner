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
#include <unistd.h>
#include <string.h>

#define CHUNK_SIZE_128KB (128 * 1024)
#define CHUNK_SIZE_64KB  (64 * 1024)
#define CHUNK_SIZE_1MB   (1024 * 1024)
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
          device->info.variant == VARIANT_T31ZX ||
          device->info.variant == VARIANT_T41)) {
        usleep((useconds_t)min_wait_ms * 1000);
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

        usleep((useconds_t)poll_interval_ms * 1000);
        elapsed_ms += poll_interval_ms;
    }

    if (elapsed_ms >= max_wait_ms) {
        printf("[WARN] Timed out waiting for firmware erase status after %d ms; "
               "continuing with write anyway.\n", elapsed_ms);
    }
}

// T41N/XBurst2 firmware write path: simple 64KB bulk chunks without VR_WRITE
// handshakes. Derived from t41n.pcap, which shows SET_DATA_ADDR/SET_DATA_LEN
// followed by raw bulk OUT transfers and a final FLUSH_CACHE.
static __attribute__((unused)) thingino_error_t write_firmware_t41n_simple(usb_device_t* device,
                                                   const uint8_t* firmware_data,
                                                   uint32_t firmware_size,
                                                   uint32_t flash_base_address,
                                                   uint32_t* bytes_written_out,
                                                   uint32_t* chunks_written_out) {
    if (!device || !firmware_data || firmware_size == 0 ||
        !bytes_written_out || !chunks_written_out) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    uint32_t bytes_written = 0;
    uint32_t chunk_num = 0;

    while (bytes_written < firmware_size) {
        uint32_t chunk_size = CHUNK_SIZE_64KB;
        if (bytes_written + chunk_size > firmware_size) {
            chunk_size = firmware_size - bytes_written;
        }

        chunk_num++;
        uint32_t current_flash_addr = flash_base_address + bytes_written;

        printf("  [T41N] Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n",
               chunk_num, chunk_size, current_flash_addr,
               (bytes_written + chunk_size) * 100.0 / firmware_size);

        int transferred = 0;
        thingino_error_t result = usb_device_bulk_transfer(
            device, ENDPOINT_OUT,
            (uint8_t*)(firmware_data + bytes_written),
            (int)chunk_size, &transferred, 6000);

        if (result != THINGINO_SUCCESS) {
            DEBUG_PRINT("T41N bulk-out transfer failed: %s\n",
                        thingino_error_to_string(result));
            return result;
        }

        if (transferred != (int)chunk_size) {
            DEBUG_PRINT("T41N incomplete bulk-out transfer: %d of %u bytes\n",
                        transferred, chunk_size);
            return THINGINO_ERROR_TRANSFER_FAILED;
        }

        bytes_written += (uint32_t)transferred;

        DEBUG_PRINT("T41N: waiting 100ms after chunk %u\n", chunk_num);
        usleep(100000); // 100ms between chunks
    }

    *bytes_written_out = bytes_written;
    *chunks_written_out = chunk_num;
    return THINGINO_SUCCESS;

}


#define T41N_PARTITION_MARKER_SIZE 172
#define T41N_FLASH_DESCRIPTOR_SIZE 984


// T41N/XBurst2 metadata FW_WRITE2 commands captured from
// tools/usb_captures/t41_full_write_20251119_185651.pcap (frames 172, 194).
// These are sent via VR_FW_WRITE2 (0x14) before the 172-byte ILOP marker
// and before the 984-byte flash descriptor, respectively.
static const uint8_t T41N_FW_WRITE2_CMD1[40] = {
    0xAC, 0x00, 0x00, 0x00,
    0x70, 0x7A, 0x00, 0x00,
    0xD0, 0x2C, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xAC, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x20, 0x36, 0x01, 0x38,
    0x70, 0x7A, 0x00, 0x00,
    0x00, 0xAF, 0x45, 0x1E,
    0x00, 0x00, 0x00, 0x00
};

static const uint8_t T41N_FW_WRITE2_CMD2[40] = {
    0xD8, 0x03, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x62, 0x74, 0xBE, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0xE0, 0xA9, 0x45, 0x1E,
    0x00, 0x00, 0x00, 0x00,
    0xC0, 0xF7, 0x3F, 0x01,
    0x00, 0x00, 0x00, 0x00,
    0xA0, 0xF9, 0x3F, 0x01,
    0x00, 0x00, 0x00, 0x00
};


// Send T41N/XBurst2 NOR writer metadata (partition marker + flash descriptor)
// captured from the stock cloner. This configures the burner with the correct
// flash geometry and policy before any firmware chunks are written.
static thingino_error_t t41n_send_write_metadata(usb_device_t *device) {
    if (!device) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    const char *marker_candidates[] = {
        "tools/extracted_t41n_write/bulk_out_0001_frame184_172bytes.bin",
        "../tools/extracted_t41n_write/bulk_out_0001_frame184_172bytes.bin",
        "../../tools/extracted_t41n_write/bulk_out_0001_frame184_172bytes.bin"
    };
    const char *desc_candidates[] = {
        "tools/extracted_t41n_write/bulk_out_0002_frame206_984bytes.bin",
        "../tools/extracted_t41n_write/bulk_out_0002_frame206_984bytes.bin",
        "../../tools/extracted_t41n_write/bulk_out_0002_frame206_984bytes.bin"
    };

    uint8_t marker[T41N_PARTITION_MARKER_SIZE];
    uint8_t descriptor[T41N_FLASH_DESCRIPTOR_SIZE];

    uint8_t status_buf[4] = {0};
    int status_len = 0;
    uint32_t status_value = 0;


    FILE *f = NULL;
    const char *path_used = NULL;

    // Load partition marker (ILOP, 172 bytes)
    for (size_t i = 0; i < sizeof(marker_candidates) / sizeof(marker_candidates[0]); ++i) {
        f = fopen(marker_candidates[i], "rb");
        if (f) {
            path_used = marker_candidates[i];
            break;
        }
    }

    if (!f) {
        printf("[ERROR] T41N partition marker file not found.\n");
        printf("        Expected at tools/extracted_t41n_write/"
               "bulk_out_0001_frame184_172bytes.bin (relative to CWD).\n");
        return THINGINO_ERROR_FILE_IO;
    }

    size_t n = fread(marker, 1, T41N_PARTITION_MARKER_SIZE, f);
    fclose(f);
    if (n != T41N_PARTITION_MARKER_SIZE) {
        printf("[ERROR] Failed to read T41N partition marker from %s: got %zu bytes, expected %d\n",
               path_used ? path_used : "(unknown)", n, T41N_PARTITION_MARKER_SIZE);
        return THINGINO_ERROR_FILE_IO;
    }
    // Send first FW_WRITE2 metadata command before the ILOP marker, as seen
    // in t41_full_write_20251119_185651.pcap (frame 172).
    thingino_error_t meta_result = protocol_fw_write_chunk2(device, T41N_FW_WRITE2_CMD1);
    if (meta_result != THINGINO_SUCCESS) {
        printf("[ERROR] T41N FW_WRITE2 command #1 failed: %s\n",
               thingino_error_to_string(meta_result));
        return meta_result;
    }



    DEBUG_PRINT("Sending T41N partition marker (ILOP, %d bytes) from %s...\n",
                T41N_PARTITION_MARKER_SIZE, path_used ? path_used : "(unknown)");

    int transferred = 0;
    thingino_error_t result = usb_device_bulk_transfer(device,
                                                       ENDPOINT_OUT,
                                                       marker,
                                                       (int)T41N_PARTITION_MARKER_SIZE,
                                                       &transferred,
                                                       5000);
    if (result != THINGINO_SUCCESS || transferred != (int)T41N_PARTITION_MARKER_SIZE) {
        printf("[ERROR] T41N partition marker transfer failed: status=%s, transferred=%d/%d bytes\n",
               thingino_error_to_string(result), transferred, T41N_PARTITION_MARKER_SIZE);
        return (result == THINGINO_SUCCESS) ? THINGINO_ERROR_TRANSFER_FAILED : result;
    }

    // After the partition marker, the stock T41N burner issues VR_FW_READ (0x10)
    // followed by VR_FW_READ_STATUS4 (0x26) before sending the descriptor.
    DEBUG_PRINT("T41N: issuing VR_FW_READ (0x10) after partition marker...\n");
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
                                       VR_FW_READ, 0, 0,
                                       NULL, sizeof(status_buf),
                                       status_buf, &status_len);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("T41N VR_FW_READ after marker failed: %s\n",
                    thingino_error_to_string(result));
    } else {
        DEBUG_PRINT("T41N VR_FW_READ after marker status: len=%d, bytes=%02X %02X %02X %02X\n",
                    status_len,
                    status_buf[0], status_buf[1],
                    status_buf[2], status_buf[3]);
    }

    result = protocol_fw_read_status(device, VR_FW_READ_STATUS4, &status_value);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("T41N VR_FW_READ_STATUS4 after marker failed: %s\n",
                    thingino_error_to_string(result));
    } else {
        DEBUG_PRINT("T41N VR_FW_READ_STATUS4 after marker: 0x%08X\n",
                    status_value);
    }


    // Short delay to let burner process the marker
    usleep(100000); // 100ms

    // Load flash descriptor (RDD/GBD/ILOP/CFS, 984 bytes)
    f = NULL;
    path_used = NULL;
    for (size_t i = 0; i < sizeof(desc_candidates) / sizeof(desc_candidates[0]); ++i) {
        f = fopen(desc_candidates[i], "rb");
        if (f) {
            path_used = desc_candidates[i];
            break;
        }
    }

    if (!f) {
        printf("[ERROR] T41N flash descriptor file not found.\n");
        printf("        Expected at tools/extracted_t41n_write/"
               "bulk_out_0002_frame206_984bytes.bin (relative to CWD).\n");
        return THINGINO_ERROR_FILE_IO;
    }

    n = fread(descriptor, 1, T41N_FLASH_DESCRIPTOR_SIZE, f);
    fclose(f);
    if (n != T41N_FLASH_DESCRIPTOR_SIZE) {
        printf("[ERROR] Failed to read T41N flash descriptor from %s: got %zu bytes, expected %d\n",
               path_used ? path_used : "(unknown)", n, T41N_FLASH_DESCRIPTOR_SIZE);
        return THINGINO_ERROR_FILE_IO;
    }
    // Send second FW_WRITE2 metadata command before the flash descriptor,
    // matching frame 194 in t41_full_write_20251119_185651.pcap.
    DEBUG_PRINT("T41N: sending FW_WRITE2 metadata command #2 before descriptor...\n");
    result = protocol_fw_write_chunk2(device, T41N_FW_WRITE2_CMD2);
    if (result != THINGINO_SUCCESS) {
        printf("[ERROR] T41N FW_WRITE2 command #2 failed: %s\n",
               thingino_error_to_string(result));
        return result;
    }



    DEBUG_PRINT("Sending T41N flash descriptor (%d bytes) from %s...\n",
                T41N_FLASH_DESCRIPTOR_SIZE, path_used ? path_used : "(unknown)");

    transferred = 0;
    result = usb_device_bulk_transfer(device,
                                      ENDPOINT_OUT,
                                      descriptor,
                                      (int)T41N_FLASH_DESCRIPTOR_SIZE,
                                      &transferred,
                                      30000);
    if (result != THINGINO_SUCCESS || transferred != (int)T41N_FLASH_DESCRIPTOR_SIZE) {
        if (result == THINGINO_ERROR_TIMEOUT && transferred == 0) {
            printf("[WARN] T41N flash descriptor transfer timed out with 0 bytes; "
                   "continuing anyway (descriptor may be optional)\n");
        } else {
            printf("[ERROR] T41N flash descriptor transfer failed: status=%s, transferred=%d/%d bytes\n",
                   thingino_error_to_string(result), transferred, T41N_FLASH_DESCRIPTOR_SIZE);
            return (result == THINGINO_SUCCESS) ? THINGINO_ERROR_TRANSFER_FAILED : result;
        }
    }
    // After the descriptor, the stock T41N sequence performs a VR_FW_READ (0x10),
    // a VR_FW_HANDSHAKE (0x11), and another VR_FW_READ (0x10) before proceeding.
    DEBUG_PRINT("T41N: issuing VR_FW_READ (0x10) after descriptor...\n");
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
                                       VR_FW_READ, 0, 0,
                                       NULL, sizeof(status_buf),
                                       status_buf, &status_len);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("T41N VR_FW_READ after descriptor failed: %s\n",
                    thingino_error_to_string(result));
    } else {
        DEBUG_PRINT("T41N VR_FW_READ after descriptor status: len=%d, bytes=%02X %02X %02X %02X\n",
                    status_len,
                    status_buf[0], status_buf[1],
                    status_buf[2], status_buf[3]);
    }

    DEBUG_PRINT("T41N: sending VR_FW_HANDSHAKE (0x11) after descriptor...\n");
    result = protocol_fw_handshake(device);
    if (result != THINGINO_SUCCESS) {
        printf("[ERROR] T41N VR_FW_HANDSHAKE after descriptor failed: %s\n",
               thingino_error_to_string(result));
        return result;
    }

    DEBUG_PRINT("T41N: issuing final VR_FW_READ (0x10) after handshake...\n");
    result = usb_device_vendor_request(device, REQUEST_TYPE_VENDOR,
                                       VR_FW_READ, 0, 0,
                                       NULL, sizeof(status_buf),
                                       status_buf, &status_len);
    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("T41N final VR_FW_READ after descriptor failed: %s\n",
                    thingino_error_to_string(result));
    } else {
        DEBUG_PRINT("T41N final VR_FW_READ after descriptor status: len=%d, bytes=%02X %02X %02X %02X\n",
                    status_len,
                    status_buf[0], status_buf[1],
                    status_buf[2], status_buf[3]);
    }



    // Small delay after descriptor
    usleep(100000); // 100ms

    DEBUG_PRINT("T41N metadata (partition marker + descriptor) sent successfully\n");
    return THINGINO_SUCCESS;
}


/**
 * Write firmware to device
 *
 * This implements the complete write sequence as observed from vendor cloner:
 * - Bootstrap device (DDR + SPL + U-Boot)
 * - Send partition marker
 * - Send metadata
 * - Send firmware in 128KB chunks (T31x) or 1MB chunks (A1)
 */
thingino_error_t write_firmware_to_device(usb_device_t* device,
                                         const char* firmware_file,
                                         const firmware_binary_t* fw_binary,
                                         bool force_erase,
                                         bool is_a1_board) {
    if (!device || !firmware_file) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    (void)force_erase; // Currently unused; reserved for future erase-policy control

    printf("Writing firmware to device...\n");
    printf("  Firmware file: %s\n", firmware_file);
    if (fw_binary) {
        printf("  SoC: %s\n", fw_binary->processor);
    }

    // Use the is_a1_board flag passed from main.c (detected before flash
    // descriptor was sent, when the device was still responsive).
    // We can't reliably detect A1 here because the device may be busy
    // processing the flash descriptor and timeout on GetCPUInfo.
    bool is_a1_fw = is_a1_board;

    // Also allow embedded firmware database keys to force A1 mode (e.g. "a1_*").
    if (!is_a1_fw && fw_binary && fw_binary->processor) {
        if (strncmp(fw_binary->processor, "a1_", 3) == 0) {
            is_a1_fw = true;
            printf("  Detected A1 firmware variant (%s) -> enabling A1 write handshakes\n",
                   fw_binary->processor);
        }
    }

    if (is_a1_fw) {
        printf("  Detected A1 CPU magic ('A1') -> enabling A1 write handshakes\n");
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

    printf("  Firmware size: %ld bytes (%.1f KB)\n", firmware_size, firmware_size / 1024.0);

    // Allocate buffer for firmware
    uint8_t* firmware_data = (uint8_t*)malloc(firmware_size);
    if (!firmware_data) {
        fprintf(stderr, "Error: Cannot allocate memory for firmware\n");
        fclose(file);
        return THINGINO_ERROR_MEMORY;
    }

    // Read firmware
    size_t bytes_read = fread(firmware_data, 1, firmware_size, file);
    fclose(file);

    if (bytes_read != (size_t)firmware_size) {
        fprintf(stderr, "Error: Failed to read firmware file\n");
        free(firmware_data);
        return THINGINO_ERROR_FILE_IO;
    }

    // Step 2: Prepare flash address and length for firmware write
    thingino_error_t result;

    // For T41N/X2580 firmware-stage writes, the vendor cloner sends a
    // partition marker ("ILOP", 172 bytes) and a 984-byte flash descriptor
    // before programming the full image. Replay that metadata here so the
    // burner knows the NOR geometry and policy.
    if (device->info.stage == STAGE_FIRMWARE &&
        device->info.variant == VARIANT_T41) {
        printf("\nStep 0: Sending T41N partition marker and flash descriptor...\n");
        result = t41n_send_write_metadata(device);
        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "Error: Failed to send T41N metadata: %s\n",
                    thingino_error_to_string(result));
            free(firmware_data);
            return result;
        }
    }

    printf("\nStep 1: Preparing firmware write (address/length)...\n");

    // Vendor T31 capture shows main firmware written starting at flash 0x00008010
    uint32_t flash_base_address = 0x00008010;

    DEBUG_PRINT("Setting flash base address with SetDataAddress: 0x%08lX\n",
                (unsigned long)flash_base_address);

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

    // For A1 boards, the VR_FW_HANDSHAKE (0x11) triggers a chip erase that takes
    // ~50-60 seconds. The vendor capture shows they wait ~53 seconds before sending
    // VR_SET_DATA_LEN, with no status polling during the erase. A1 doesn't respond
    // to VR_FW_READ_STATUS2 during erase (returns 0 or times out), so we use a
    // fixed delay instead of status polling.
    if (is_a1_fw) {
        printf("Waiting for A1 chip erase to complete (this takes ~60 seconds)...\n");
        printf("  The device will not respond to status requests during erase.\n");

        // Wait 60 seconds for erase to complete
        for (int i = 0; i < 60; i++) {
            printf("\r  Erase progress: %d/60 seconds...", i + 1);
            fflush(stdout);
            sleep(1);
        }
        printf("\n");
        printf("Erase should be complete, proceeding with write...\n");
    }

    // Set data length before the first chunk. Vendor captures show:
    // - T31x: Set total firmware size.
    // - T41N: Use a fixed 64KB length for per-chunk VR_WRITE writes.
    // - A1: Set total firmware size (sent after erase completes).
    uint32_t set_length = (device->info.stage == STAGE_FIRMWARE &&
                           device->info.variant == VARIANT_T41)
                              ? (uint32_t)CHUNK_SIZE_64KB
                              : (uint32_t)firmware_size;

    DEBUG_PRINT("Setting firmware write length with SetDataLength: %lu bytes\n",
                (unsigned long)set_length);
    result = protocol_set_data_length(device, set_length);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error: Failed to set firmware write length: %s\n", thingino_error_to_string(result));
        free(firmware_data);
        return result;
    }

    // Wait for device to prepare (erase flash, etc.) for non-A1 boards.
    // A1 boards already waited above with a fixed delay.
    if (!is_a1_fw) {
        // The first full-chip erase on a fresh or previously-programmed device
        // can take significantly longer than subsequent runs, so rely on firmware
        // status polling instead of a fixed sleep. We still enforce a minimum 5s
        // delay and cap the wait at 60s for safety.
        firmware_wait_for_erase_ready(device, 5000 /* min_wait_ms */, 60000 /* max_wait_ms */);
    }

    // NOTE: VR_FW_HANDSHAKE (0x11) should be sent earlier (after U-Boot load),
    // not here. Vendor capture shows it's sent once at frame 13467, way before
    // the firmware chunks start at frame 14051. Sending it here puts device in bad state.

    // Step 3: Send firmware with variant-specific protocol
    printf("\nStep 2: Writing firmware data...\n");

    uint32_t bytes_written = 0;
    uint32_t chunk_num = 0;
    result = THINGINO_SUCCESS;

    if (device->info.stage == STAGE_FIRMWARE &&
        device->info.variant == VARIANT_T41) {
        // T41N/XBurst2 path: 64KB chunks with VR_WRITE (0x12) handshakes,
        // matching t41_full_write_20251119_185651.pcap.
        while (bytes_written < (uint32_t)firmware_size) {
            uint32_t chunk_size = CHUNK_SIZE_64KB;
            if (bytes_written + chunk_size > (uint32_t)firmware_size) {
                chunk_size = (uint32_t)firmware_size - bytes_written;
            }

            chunk_num++;
            uint32_t chunk_offset = bytes_written;  // offset relative to flash_base_address
            uint32_t current_flash_addr = flash_base_address + chunk_offset;

            printf("  [T41N] Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n",
                   chunk_num, chunk_size, current_flash_addr,
                   (bytes_written + chunk_size) * 100.0 / firmware_size);

            // Use 40-byte VR_WRITE (0x12) handshakes per chunk, matching the
            // vendor T41N NOR writer behavior.
            result = firmware_handshake_write_chunk(device, chunk_num - 1,  // 0-based index
                                                   chunk_offset,
                                                   firmware_data + bytes_written,
                                                   chunk_size);
            if (result != THINGINO_SUCCESS) {
                fprintf(stderr, "Error: Failed to write T41N chunk %u\n", chunk_num);
                free(firmware_data);
                return result;
            }

            bytes_written += chunk_size;
        }
    } else if (is_a1_fw) {
        // A1 path: 1MB chunks with A1-specific VR_WRITE handshakes.
        // Pattern from a1_full_write_20251119_221121.pcap shows 1MB (0x100000) chunks.
        while (bytes_written < (uint32_t)firmware_size) {
            uint32_t chunk_size = CHUNK_SIZE_1MB;
            if (bytes_written + chunk_size > (uint32_t)firmware_size) {
                chunk_size = (uint32_t)firmware_size - bytes_written;
            }

            chunk_num++;
            uint32_t chunk_offset = bytes_written;  // offset relative to flash_base_address
            uint32_t current_flash_addr = flash_base_address + chunk_offset;

            printf("  [A1] Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n",
                   chunk_num, chunk_size, current_flash_addr,
                   (bytes_written + chunk_size) * 100.0 / firmware_size);

            // Use A1-specific 40-byte VR_WRITE (0x12) handshakes per chunk.
            result = firmware_handshake_write_chunk_a1(device, chunk_num - 1,  // 0-based index
                                                       chunk_offset,
                                                       firmware_data + bytes_written,
                                                       chunk_size);
            if (result != THINGINO_SUCCESS) {
                fprintf(stderr, "Error: Failed to write A1 chunk %u\n", chunk_num);
                free(firmware_data);
                return result;
            }

            bytes_written += chunk_size;
        }
    } else {
        // Default T31-family path: 128KB chunks with VR_WRITE handshakes.
        while (bytes_written < (uint32_t)firmware_size) {
            uint32_t chunk_size = CHUNK_SIZE_128KB;
            if (bytes_written + chunk_size > (uint32_t)firmware_size) {
                chunk_size = (uint32_t)firmware_size - bytes_written;
            }

            chunk_num++;
            uint32_t chunk_offset = bytes_written;  // offset relative to flash_base_address
            uint32_t current_flash_addr = flash_base_address + chunk_offset;

            printf("  Chunk %u: Writing %u bytes at 0x%08X (%.1f%%)...\n",
                   chunk_num, chunk_size, current_flash_addr,
                   (bytes_written + chunk_size) * 100.0 / firmware_size);

            // Use 40-byte VR_WRITE (0x12) handshakes per chunk, matching the
            // vendor NOR writer behavior.
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

