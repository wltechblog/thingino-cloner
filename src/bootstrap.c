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
        printf("CPU info: stage=%s, magic='%.8s'\n",
            device_stage_to_string(cpu_info.stage), cpu_info.magic);

        // Update device stage based on actual CPU info
        if (cpu_info.stage == STAGE_FIRMWARE) {
            device->info.stage = STAGE_FIRMWARE;
            printf("Device stage updated to firmware based on CPU info\n");
        }
    }

    // Load firmware files
    DEBUG_PRINT("Loading firmware files...\n");
    firmware_files_t fw;
    // Ensure re-enumeration pointers are initialized for all paths
    device_info_t* found_devices = NULL;

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

    // Step 3: Set execution size and execute SPL
    // The config.cfg specifies d2i_len=0x7000 for T41
    DEBUG_PRINT("Setting execution size to 0x7000\n");
    result = protocol_set_data_length(device, 0x7000);
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

    // Device re-enumeration behavior differs by variant:
    // - T31X: Device re-enumerates to new USB address → must close/reopen handle
    // - T41N: Device stays at same USB address → keep using same handle
    processor_variant_t previous_variant = device->info.variant;

    // For T41N, don't close the handle - keep using it
    if (device->info.variant == VARIANT_T41N || device->info.variant == VARIANT_T41) {
        DEBUG_PRINT("T41N detected - keeping USB handle open (device doesn't re-enumerate)\n");

        // Wait for SPL to initialize DDR
        DEBUG_PRINT("Waiting for SPL to initialize DDR (8 seconds)...\n");
#ifdef _WIN32
        Sleep(8000);
#else
        sleep(8);
#endif

        // Skip the re-enumeration logic for T41N
        goto skip_reenumeration;
    }

    // For T31X and other variants: close handle and wait for re-enumeration
    DEBUG_PRINT("Closing device handle before re-enumeration...\n");
    usb_device_close(device);
    DEBUG_PRINT("Device handle closed\n");

    // Wait for SPL to complete execution and device to re-enumerate
    DEBUG_PRINT("Waiting for SPL to complete execution and device to re-enumerate...\n");
    DEBUG_PRINT("(Waiting 3 seconds for device re-enumeration)...\n");
#ifdef _WIN32
    Sleep(3000);
#else
    sleep(3);
#endif

    // Re-detect and re-open the device after re-enumeration
    DEBUG_PRINT("Re-detecting device after re-enumeration...\n");
    usb_manager_t manager;
    result = usb_manager_init(&manager);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to initialize USB manager for re-detection: %s\n", thingino_error_to_string(result));
        firmware_cleanup(&fw);
        return result;
    }

    // Find the device using fast enumeration (skips CPU info checks to avoid timeouts)
    int device_count = 0;
    result = usb_manager_find_devices_fast(&manager, &found_devices, &device_count);
    if (result != THINGINO_SUCCESS || device_count == 0) {
        printf("Failed to find re-enumerated device: %s\n", thingino_error_to_string(result));
        usb_manager_cleanup(&manager);
        firmware_cleanup(&fw);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }

    printf("Found %d Ingenic device(s) after re-enumeration\n", device_count);

    // Use the first device (in practice there should be only one)
    device_info_t new_device_info = found_devices[0];
    DEBUG_PRINT("Using re-enumerated device at USB address: Bus %d, Address %d\n",
        new_device_info.bus, new_device_info.address);

    // Open the newly enumerated device
    usb_device_t* new_device = NULL;
    result = usb_manager_open_device(&manager, &new_device_info, &new_device);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to open re-enumerated device: %s\n", thingino_error_to_string(result));
        free(found_devices);
        usb_manager_cleanup(&manager);
        firmware_cleanup(&fw);
        return result;
    }

    DEBUG_PRINT("Successfully re-opened device, updating device pointer\n");

    // Check if device re-enumerated to a different address
    bool same_address = (device->info.bus == new_device->info.bus &&
                         device->info.address == new_device->info.address);

    if (same_address) {
        DEBUG_PRINT("Device stayed at same USB address (Bus %d, Address %d) - using new handle\n",
            device->info.bus, device->info.address);
        // Device didn't re-enumerate to new address (T41N behavior)
        // The old handle was already closed before re-enumeration
        // Use the new handle we just opened
        device->handle = new_device->handle;
        device->info = new_device->info;
        device->closed = false;
        free(new_device);
    } else {
        DEBUG_PRINT("Device re-enumerated to new address (Bus %d, Address %d -> Bus %d, Address %d)\n",
            device->info.bus, device->info.address, new_device->info.bus, new_device->info.address);
        // Device re-enumerated to new address (T31X behavior)
        // The old handle was already closed before re-enumeration
        // Use the new handle we just opened
        device->handle = new_device->handle;
        device->info = new_device->info;
        device->closed = false;
        free(new_device);
    }

    // Restore the originally selected variant (honor --variant across re-enumeration)
    DEBUG_PRINT("Restoring forced/previous variant: %d -> %d\n", device->info.variant, previous_variant);
    device->info.variant = previous_variant;


    // Dump active configuration after SPL (helps diagnose interface/alt settings on T41)
    usb_device_dump_active_config(device, false);

skip_reenumeration:
    // T41N jumps here directly, skipping the re-enumeration logic

    // Dump active configuration after SPL (helps diagnose interface/alt settings on T41)
    usb_device_dump_active_config(device, false);

    // Post-SPL readiness handling differs by variant
    if (device->info.variant == VARIANT_T41 || device->info.variant == VARIANT_T41N) {
        // For T41/T41N, stock tools do not rely on long GetCPUInfo polling here.
        // We already waited 5s for DDR init. Do a short settle + safe EP0 refresh, then proceed.
        DEBUG_PRINT("T41/T41N: skipping GetCPUInfo polling; refreshing EP0 and proceeding to stage2...\n");
#ifdef _WIN32
        Sleep(100);
#else
        usleep(100000); // 100ms settle
#endif
        // Safe handle refresh at same address (no config/alt changes)
        usb_device_close(device);
#ifdef _WIN32
        Sleep(200);
#else
        usleep(200000); // 200ms
#endif
        thingino_error_t r = usb_device_init(device, device->info.bus, device->info.address);
        if (r == THINGINO_SUCCESS) {
            DEBUG_PRINT("Re-opened device handle successfully before stage2\n");
#ifdef _WIN32
            Sleep(50);
#else
            usleep(50000); // 50ms
#endif
        } else {
            DEBUG_PRINT("Handle refresh before stage2 failed: %s\n", thingino_error_to_string(r));
        }

        // Do not claim interface before stage2 on T41/T41N to match vendor behavior
    } else {
        DEBUG_PRINT("Ready to poll for SPL completion...\n");

        // Poll GetCPUInfo to wait for SPL to complete DDR initialization
        // The SPL will respond when DDR is ready
        int max_polls = 2000;
        int poll_count = 0;
        int consecutive_successes = 0;
        const int required_successes = 3;  // Need 3 consecutive successful responses

        // Track progress and a one-time safe handle refresh (no config changes)
        int any_success = 0;
        int reopens_done = 0;
        const int max_reopens = 1;
        const int reopen_threshold = 200;  // after 200 polls with no success, refresh handle

        DEBUG_PRINT("Polling GetCPUInfo to wait for SPL to complete DDR initialization...\n");
        for (poll_count = 0; poll_count < max_polls; poll_count++) {
            cpu_info_t cpu_info;
            thingino_error_t poll_result = usb_device_get_cpu_info(device, &cpu_info);

            if (poll_result == THINGINO_SUCCESS) {
                any_success = 1;
                consecutive_successes++;
                if (consecutive_successes >= required_successes) {
                    DEBUG_PRINT("SPL ready after %d polls (CPU magic: %s)\n", poll_count + 1, cpu_info.magic);
                    break;
                }
            } else {
                consecutive_successes = 0;  // Reset on failure

                // If we have not seen any success yet, perform a one-time safe handle refresh
                if (!any_success && ((poll_count + 1) % reopen_threshold == 0) && reopens_done < max_reopens) {
                    DEBUG_PRINT("No GetCPUInfo response yet; refreshing USB handle at same address (Bus %d, Address %d)...\n",
                                device->info.bus, device->info.address);
                    usb_device_close(device);
#ifdef _WIN32
                    Sleep(200);
#else
                    usleep(200000); // 200ms
#endif
                    thingino_error_t rr = usb_device_init(device, device->info.bus, device->info.address);
                    if (rr == THINGINO_SUCCESS) {
                        DEBUG_PRINT("Re-opened device handle successfully for continued polling\n");
                        reopens_done++;
#ifdef _WIN32
                        Sleep(50);
#else
                        usleep(50000); // 50ms
#endif
                    } else {
                        DEBUG_PRINT("Handle refresh failed: %s\n", thingino_error_to_string(rr));
                    }
                }
            }

            // Small delay between polls
#ifdef _WIN32
            Sleep(10);  // 10ms
#else
            usleep(10000);  // 10ms
#endif
        }

        if (poll_count >= max_polls) {
            DEBUG_PRINT("Warning: Reached maximum poll count (%d), proceeding anyway\n", max_polls);
        }
    }


    // Step 4: Load and program U-Boot (Stage 2 bootloader)
    printf("Loading U-Boot (Stage 2 bootloader)\n");
    result = bootstrap_program_stage2(device, config, fw.uboot, fw.uboot_size);
    if (result != THINGINO_SUCCESS) {
        firmware_cleanup(&fw);
        if (found_devices) free(found_devices);
        // NOTE: DO NOT call usb_manager_cleanup() here because it will invalidate
        // the libusb context that the device handle is still using, causing segfault
        // when the caller tries to clean up the device.
        // usb_manager_cleanup(&manager);
        return result;
    }
    printf("U-Boot loaded\n");

    // Cleanup temporary manager resources
    // NOTE: DO NOT call usb_manager_cleanup() because it will exit the libusb context
    // that the device handle is still using. Just free the device list.
    if (found_devices) free(found_devices);
    // The manager->context is now being used by device->context, so we can't exit it

    // NOTE: After ProgStage2, the device will re-enumerate with a new USB address.
    // We cannot verify the transition here because the device handle is now invalid.
    // The caller must re-scan for devices and verify the transition.
    DEBUG_PRINT("Bootstrap sequence completed - device will re-enumerate\n");
    DEBUG_PRINT("Note: Device handle is now invalid and must be re-opened after re-enumeration\n");

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

thingino_error_t bootstrap_program_stage2(usb_device_t* device, const bootstrap_config_t* config,
    const uint8_t* data, size_t size) {

    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    // Step 1: Determine target address for U-Boot
    uint32_t uboot_address = (config && config->uboot_address_override)
        ? config->uboot_address_override
        : 0x80100000; // default
    if (config && config->uboot_address_override) {
        DEBUG_PRINT("Using override Stage-2 address: 0x%08x\n", uboot_address);
    }
    DEBUG_PRINT("Setting U-Boot data address to 0x%08x\n", uboot_address);
    thingino_error_t result = protocol_set_data_address(device, uboot_address);
    if (result != THINGINO_SUCCESS) {
        return result;
    }

    // Step 2: Set data length
    DEBUG_PRINT("Setting U-Boot data length to %zu bytes\n", size);
    result = protocol_set_data_length(device, (uint32_t)size);
    if (result != THINGINO_SUCCESS) {
        // Some devices don't require or accept SetDataLength for stage2; proceed without it
        DEBUG_PRINT("SetDataLength failed for U-Boot, proceeding without it (compat mode): %s\n",
                    thingino_error_to_string(result));
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

    // Step 4: Flush cache before executing U-Boot (skip for T41/T41N to match vendor)
    if (device->info.variant == VARIANT_T41 || device->info.variant == VARIANT_T41N) {
        DEBUG_PRINT("Skipping FlushCache for T41/T41N (vendor sequence has none)\n");
    } else {
        DEBUG_PRINT("Flushing cache before U-Boot execution\n");
        result = protocol_flush_cache(device);
        if (result != THINGINO_SUCCESS) {
            return result;
        }
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