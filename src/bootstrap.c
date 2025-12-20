#include "thingino.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// BOOTSTRAP IMPLEMENTATION
// ============================================================================

thingino_error_t bootstrap_device(usb_device_t* device, const bootstrap_config_t* config) {
    if (!device || !config) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Only bootstrap if device is in bootrom stage
    if (device->info.stage != STAGE_BOOTROM) {
        if (config->verbose) {
            printf("Device already in firmware stage, skipping bootstrap\n");
        }
        return THINGINO_SUCCESS;
    }

    const char* variant_str = processor_variant_to_string(device->info.variant);
    printf("Starting bootstrap sequence for %s\n", variant_str);

    // NOTE: Do NOT reset device - pcap analysis shows vendor tool does not reset
    // Resetting causes device to disconnect and re-enumerate, breaking bootstrap flow
    DEBUG_PRINT("Skipping device reset (vendor tool doesn't reset)\n");

    // Get CPU info to understand current device state
    DEBUG_PRINT("Getting CPU info...\n");
    cpu_info_t cpu_info;
    thingino_error_t result = usb_device_get_cpu_info(device, &cpu_info);
    if (result != THINGINO_SUCCESS) {
        printf("Warning: failed to get CPU info: %s\n", thingino_error_to_string(result));
        printf("Continuing with bootstrap anyway - device may not be ready\n");
        // Don't exit - continue with bootstrap and let it fail gracefully if needed
    } else {
        // Show raw hex bytes for debugging
        printf("CPU magic (raw hex): ");
        for (int i = 0; i < 8; i++) {
            printf("%02X ", cpu_info.magic[i]);
        }
        printf("\n");

        printf("CPU info: stage=%s, magic='%.8s'\n",
            device_stage_to_string(cpu_info.stage), cpu_info.magic);

        // Detect and display processor variant
        processor_variant_t detected_variant = detect_variant_from_magic(cpu_info.clean_magic);
        printf("Detected processor variant: %s (from magic: '%s')\n",
            processor_variant_to_string(detected_variant), cpu_info.clean_magic);

        // Update device stage based on actual CPU info
        if (cpu_info.stage == STAGE_FIRMWARE) {
            device->info.stage = STAGE_FIRMWARE;
            printf("Device stage updated to firmware based on CPU info\n");
        }
    }

    // Load firmware files
    DEBUG_PRINT("Loading firmware files...\n");
    firmware_files_t fw;

    // Check if custom files are provided
    if (config->config_file || config->spl_file || config->uboot_file) {
        DEBUG_PRINT("Using custom firmware files:\n");
        if (config->config_file) DEBUG_PRINT("  Config: %s\n", config->config_file);
        if (config->spl_file) DEBUG_PRINT("  SPL: %s\n", config->spl_file);
        if (config->uboot_file) DEBUG_PRINT("  U-Boot: %s\n", config->uboot_file);

        result = firmware_load_from_files(device->info.variant,
            config->config_file, config->spl_file, config->uboot_file, &fw);
    } else {
        DEBUG_PRINT("Using default firmware files\n");
        result = firmware_load(device->info.variant, &fw);
    }

    if (result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Firmware load failed: %s\n", thingino_error_to_string(result));
        return result;
    }

    printf("Firmware loaded - Config: %zu bytes, SPL: %zu bytes, U-Boot: %zu bytes\n",
        fw.config_size, fw.spl_size, fw.uboot_size);

    // Step 1: Load DDR configuration to memory (NOT executed yet)
    if (!config->skip_ddr) {
        printf("Loading DDR configuration\n");
        result = bootstrap_load_data_to_memory(device, fw.config, fw.config_size, 0x80001000);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(&fw);
            return result;
        }
        printf("DDR configuration loaded\n");
    } else {
        printf("Skipping DDR configuration (SkipDDR flag set)\n");
    }

    // Step 2: Load SPL to memory (NOT executed yet)
    printf("Loading SPL (Stage 1 bootloader)\n");
    result = bootstrap_load_data_to_memory(device, fw.spl, fw.spl_size, 0x80001800);
    if (result != THINGINO_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    printf("SPL loaded\n");

    // Step 3: Set execution size (d2i_len) and execute SPL
    // This is processor-specific: T20 uses 0x4000, most others use 0x7000
    uint32_t d2i_len = (device->info.variant == VARIANT_T20) ? 0x4000 : 0x7000;
    DEBUG_PRINT("Setting execution size (d2i_len) to 0x%x for %s\n",
        d2i_len, processor_variant_to_string(device->info.variant));
    result = protocol_set_data_length(device, d2i_len);
    if (result != THINGINO_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }

    DEBUG_PRINT("Executing SPL from entry point 0x80001800\n");
    result = protocol_prog_stage1(device, 0x80001800);
    if (result != THINGINO_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    printf("SPL execution started\n");

    // IMPORTANT: Unlike T31X, the vendor's T20 implementation does NOT close/reopen the device
    // The USB device address stays the same (verified in pcap: address 106 throughout)
    // We just wait for SPL to complete DDR initialization
    DEBUG_PRINT("Waiting for SPL to complete DDR initialization (keeping device handle open)...\n");

    // Variant-specific wait for SPL to complete DDR initialization
    // Vendor pcaps show ~1.1s for T20 and T41/T41N, longer for T31-family parts
    int wait_ms;
    if (device->info.variant == VARIANT_T20 || device->info.variant == VARIANT_T41) {
        wait_ms = 1100;  // Match vendor T20/T41/T41N behavior (~1.1s)
    } else {
        wait_ms = 2000;  // Default: allow more time for other variants (e.g., T31 family)
    }
    DEBUG_PRINT("Waiting %d ms for DDR init...\n", wait_ms);

#ifdef _WIN32
    Sleep(wait_ms);
#else
    usleep(wait_ms * 1000);
#endif

    DEBUG_PRINT("SPL should have completed, device handle remains valid\n");

    // For T20 and T41/T41N, vendor tools poll GET_CPU_INFO after the SPL wait
    if (device->info.variant == VARIANT_T20 || device->info.variant == VARIANT_T41) {
        DEBUG_PRINT("Polling GET_CPU_INFO after SPL wait (T20/T41 vendor pattern)...\n");
        cpu_info_t poll_info;
        bool spl_ready = false;
        for (int attempt = 0; attempt < 10; attempt++) {
            thingino_error_t poll_result = usb_device_get_cpu_info(device, &poll_info);
            if (poll_result == THINGINO_SUCCESS) {
                DEBUG_PRINT("SPL ready after %d attempt(s): stage=%s, magic='%s'\n",
                    attempt + 1,
                    device_stage_to_string(poll_info.stage),
                    poll_info.clean_magic);
                spl_ready = true;
                break;
            }
#ifdef _WIN32
            Sleep(20);
#else
            usleep(20000);  // 20ms between polls
#endif
        }
        if (!spl_ready) {
            DEBUG_PRINT("Warning: GET_CPU_INFO polling after SPL failed for variant %s\n",
                processor_variant_to_string(device->info.variant));
        }
    }

    // For T31ZX, SPL may reset or re-enumerate the USB device; reopen the handle
    if (device->info.variant == VARIANT_T31ZX) {
        DEBUG_PRINT("Reopening USB device handle after SPL for T31ZX variant\n");
        thingino_error_t reopen_result = usb_device_reopen(device);
        if (reopen_result != THINGINO_SUCCESS) {
            printf("Error: failed to re-open USB device after SPL: %s\n",
                thingino_error_to_string(reopen_result));
            firmware_cleanup(&fw);
            return reopen_result;
        }

        // Give the device additional time to be ready after reopen
        // Some boards (like A1) need extra time after USB re-enumeration
        // Testing shows A1 needs at least 5 seconds, while T31ZX works with 500ms
        DEBUG_PRINT("Waiting 5000ms after USB reopen for device to be ready...\n");
#ifdef _WIN32
        Sleep(5000);
#else
        usleep(5000000);
#endif
    }


    // Step 4: Load and program U-Boot (Stage 2 bootloader)
    printf("Loading U-Boot (Stage 2 bootloader)\n");
    result = bootstrap_program_stage2(device, fw.uboot, fw.uboot_size);
    if (result != THINGINO_SUCCESS) {
        firmware_cleanup(&fw);
        return result;
    }
    printf("U-Boot loaded\n");

    // Vendor does GET_CPU_INFO immediately after PROG_START2 (verified in pcap)
    // This might be necessary to "wake up" the device or trigger the transition
    DEBUG_PRINT("Checking CPU info immediately after PROG_START2 (vendor sequence)...\n");
    cpu_info_t cpu_info_after;
    result = usb_device_get_cpu_info(device, &cpu_info_after);
    if (result == THINGINO_SUCCESS) {
        DEBUG_PRINT("CPU info after PROG_START2: stage=%s, magic='%s'\n",
            device_stage_to_string(cpu_info_after.stage), cpu_info_after.clean_magic);
    } else {
        DEBUG_PRINT("GET_CPU_INFO after PROG_START2 failed (may be expected): %s\n",
            thingino_error_to_string(result));
    }

    // NOTE (T31 doorbell): Factory T31 burner U-Boot logs show that sending
    // VR_FW_HANDSHAKE/VR_FW_READ immediately after PROG_STAGE2 results in
    // cloner->ack = -22 and a trap exception when no flash descriptor has
    // been provided yet. For this device we therefore perform FW_HANDSHAKE
    // only in the higher-level read/write flows, *after* the 172-byte
    // partition marker and 972-byte flash descriptor have been sent.

    printf("Bootstrap sequence completed successfully\n");

    firmware_cleanup(&fw);
    return THINGINO_SUCCESS;
}

thingino_error_t bootstrap_ensure_bootstrapped(usb_device_t* device, const bootstrap_config_t* config) {
    if (!device || !config) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // If device is already in firmware stage, no bootstrap needed
    if (device->info.stage == STAGE_FIRMWARE) {
        return THINGINO_SUCCESS;
    }

    // Device needs bootstrap
    return bootstrap_device(device, config);
}

thingino_error_t bootstrap_load_data_to_memory(usb_device_t* device,
    const uint8_t* data, size_t size, uint32_t address) {

    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Step 1: Set target address
    DEBUG_PRINT("Setting data address to 0x%08x\n", address);
    thingino_error_t result = protocol_set_data_address(device, address);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 2: Set data length
    DEBUG_PRINT("Setting data length to %zu bytes\n", size);
    result = protocol_set_data_length(device, (uint32_t)size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 3: Transfer data
    DEBUG_PRINT("Transferring data (%zu bytes)...\n", size);
    result = bootstrap_transfer_data(device, data, size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    return THINGINO_SUCCESS;
}

thingino_error_t bootstrap_program_stage2(usb_device_t* device,
    const uint8_t* data, size_t size) {

    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Step 1: Set target address for U-Boot (PCAP shows 0x80100000)
    uint32_t uboot_address = 0x80100000;
    DEBUG_PRINT("Setting U-Boot data address to 0x%08x\n", uboot_address);
    thingino_error_t result = protocol_set_data_address(device, uboot_address);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 2: Set data length
    DEBUG_PRINT("Setting U-Boot data length to %zu bytes\n", size);
    result = protocol_set_data_length(device, (uint32_t)size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 3: Transfer data
    DEBUG_PRINT("Transferring U-Boot data (%zu bytes)...\n", size);
    result = bootstrap_transfer_data(device, data, size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // After large U-Boot transfer, give device time to process
    DEBUG_PRINT("Waiting for device to process U-Boot transfer...\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(500);
#else
#ifdef _WIN32
    Sleep(500);
#else
    usleep(500000);
#endif
#endif

    // Step 4: Flush cache before executing U-Boot
    DEBUG_PRINT("Flushing cache before U-Boot execution\n");
    result = protocol_flush_cache(device);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 5: Execute U-Boot using ProgStage2
    DEBUG_PRINT("Executing U-Boot using ProgStage2\n");

    // Split execution address into MSB and LSB
    uint16_t wValue = (uint16_t)(uboot_address >> 16);    // MSB of 0x80100000 = 0x8010
    uint16_t wIndex = (uint16_t)(uboot_address & 0xFFFF); // LSB of 0x80100000 = 0x0000

    DEBUG_PRINT("ProgStage2: wValue=0x%04x (MSB), wIndex=0x%04x (LSB), addr=0x%08x\n",
        wValue, wIndex, uboot_address);

    result = protocol_prog_stage2(device, uboot_address);

    // PCAP analysis shows device does NOT re-enumerate after ProgStage2
    // Instead, it transitions internally from bootrom to firmware stage
    DEBUG_PRINT("ProgStage2 completed - device should now be in firmware stage\n");

    // Platform-specific sleep
#ifdef _WIN32
    Sleep(1000);
#else
    sleep(1);
#endif

    return THINGINO_SUCCESS;
}

thingino_error_t bootstrap_transfer_data(usb_device_t* device,
    const uint8_t* data, size_t size) {

    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("TransferData starting: %zu bytes total\n", size);

    // For small transfers (< 64KB), try single transfer first
    // For large transfers (like U-Boot ~390KB), use chunked approach
    size_t chunk_size = 1048576; // 4KB chunks - good balance between performance and reliability

    // However, for very large transfers, use smaller chunks to avoid endpoint stalls
/*   if (size > 100 * 1024) { // > 100KB
        chunk_size = 1024; // 1KB chunks for large transfers
    }
  */
    size_t total_written = 0;
    size_t offset = 0;
    int max_retries = 3;

    while (offset < size) {
        // Determine chunk size for this iteration
        size_t remaining = size - offset;
        size_t current_chunk_size = chunk_size;
        if (remaining < current_chunk_size) {
            current_chunk_size = remaining;
        }

        DEBUG_PRINT("TransferData chunk: offset=%zu, size=%zu, remaining=%zu\n",
            offset, current_chunk_size, remaining);

        // Try to write this chunk with retries
        bool chunk_written = false;
        for (int retry = 0; retry < max_retries && !chunk_written; retry++) {
            int transferred;
            // Calculate timeout: 5s base + 1s per 64KB, max 30s
            int timeout = 5000 + ((int)current_chunk_size / 65536) * 1000;
            if (timeout > 30000) timeout = 30000;
            if (timeout < 5000) timeout = 5000; // Minimum 5 seconds

            thingino_error_t result = usb_device_bulk_transfer(device, ENDPOINT_OUT,
                (uint8_t*)&data[offset], (int)current_chunk_size, &transferred, timeout);

            if (result == THINGINO_SUCCESS && transferred > 0) {
                // Success - at least some bytes written
                DEBUG_PRINT("TransferData chunk written: %d bytes (attempt %d)\n", transferred, retry + 1);
                total_written += transferred;
                offset += transferred;

                // If we wrote the expected amount, move to next chunk
                if (transferred == (int)current_chunk_size) {
                    chunk_written = true;
                    break;
                } else if (transferred < (int)current_chunk_size) {
                    // Partial write - adjust chunk size and retry
                    current_chunk_size -= transferred;
                    DEBUG_PRINT("Partial write, retrying remaining %zu bytes\n", current_chunk_size);
                    continue;
                }
            }

            // Handle write error
            if (result != THINGINO_SUCCESS) {
                DEBUG_PRINT("TransferData error on attempt %d: %s\n", retry + 1,
                    thingino_error_to_string(result));

                // For other errors or if retry limit reached
                if (retry < max_retries - 1) {
                    DEBUG_PRINT("Retrying write after brief delay (attempt %d/%d)\n",
                        retry + 2, max_retries);

                    // Platform-specific sleep
#ifdef _WIN32
                    Sleep(50);
#else
                    usleep(50000);
#endif
                    continue;
                }

                // Out of retries - this is a real failure
                return result;
            }

            // No error but no bytes written - shouldn't happen
            if (retry == max_retries - 1) {
                DEBUG_PRINT("Bulk write returned 0 bytes and no error at offset %zu\n", offset);
                return THINGINO_ERROR_TRANSFER_FAILED;
            }
        }

        // Small delay between chunks for large transfers to prevent overwhelming device
        if (size > 100 * 1024 && offset < size) {
            // Platform-specific sleep
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
        }
    }

    DEBUG_PRINT("TransferData complete: %zu bytes written successfully\n", total_written);
    return THINGINO_SUCCESS;
}