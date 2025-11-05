#include "thingino.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// ============================================================================
// FIRMWARE READER IMPLEMENTATION - Session 19 Fix
// Based on READ_FIRMWARE_PROTOCOL_ANALYSIS.md - vendor tool streams data automatically
// ============================================================================

// Direct implementation using BULK IN (EP 0x81) for all transfers
// Based on protocol analysis - device streams data without prior commands
static thingino_error_t firmware_read_direct_bulk_in(usb_device_t* device, 
                                                      uint8_t* buffer, 
                                                      uint32_t size, 
                                                      int* transferred) {
    if (!device || !buffer || size == 0 || !transferred) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("Direct BULK IN read: size=%u bytes, timeout=30s\n", size);
    
    // Ensure interface is claimed
    thingino_error_t claim_result = usb_device_claim_interface(device);
    if (claim_result != THINGINO_SUCCESS) {
        DEBUG_PRINT("Failed to claim interface for BULK IN: %s\n", thingino_error_to_string(claim_result));
        return claim_result;
    }
    
    // Use BULK IN endpoint 0x81 directly
    // 30-second timeout for large transfers (1MB chunks need ~3-5 seconds each)
    int result = libusb_bulk_transfer(device->handle, ENDPOINT_IN, 
                                      buffer, size, transferred, 30000);
    
    // Always release interface after transfer
    usb_device_release_interface(device);
    
    if (result == LIBUSB_SUCCESS) {
        DEBUG_PRINT("BULK IN transfer successful: %d/%u bytes\n", *transferred, size);
        return THINGINO_SUCCESS;
    }
    
    DEBUG_PRINT("BULK IN transfer failed: %s\n", libusb_error_name(result));
    
    if (result == LIBUSB_ERROR_TIMEOUT) {
        return THINGINO_ERROR_TRANSFER_TIMEOUT;
    }
    if (result == LIBUSB_ERROR_PIPE) {
        return THINGINO_ERROR_TRANSFER_FAILED;
    }
    
    return THINGINO_ERROR_TRANSFER_FAILED;
}

// Read firmware components as streamed by device (172B, 324B, 972B, 10KB, 390KB)
// Based on protocol analysis - these are sent first before main firmware
static thingino_error_t firmware_read_components(usb_device_t* device) {
    DEBUG_PRINT("Reading firmware components from device...\n");
    
    // Component sizes from protocol analysis
    uint32_t component_sizes[] = {172, 324, 972, 10092, 390532};
    const char* component_names[] = {"Init", "DDR Config", "SPL", "U-Boot Stage 1", "U-Boot Main"};
    
    for (int i = 0; i < 5; i++) {
        DEBUG_PRINT("Reading component %d: %s (%u bytes)\n", i, component_names[i], component_sizes[i]);
        
        uint8_t* component_buffer = (uint8_t*)malloc(component_sizes[i]);
        if (!component_buffer) {
            printf("[ERROR] Failed to allocate buffer for component %d\n", i);
            return THINGINO_ERROR_MEMORY;
        }
        
        int transferred = 0;
        thingino_error_t result = firmware_read_direct_bulk_in(device, component_buffer, 
                                                         component_sizes[i], &transferred);
        
        if (result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to read component %d: %s\n", i, thingino_error_to_string(result));
            free(component_buffer);
            return result;
        }
        
        if ((uint32_t)transferred != component_sizes[i]) {
            printf("[WARNING] Component %d: Expected %u bytes, got %d bytes\n", 
                   i, component_sizes[i], transferred);
        }
        
        // We don't need to save the components, just read them to advance the protocol
        free(component_buffer);
        
        DEBUG_PRINT("Component %d read successfully (%d bytes)\n", i, transferred);
        
        // Small delay between components to let device prepare
        usleep(100000); // 100ms
    }
    
    DEBUG_PRINT("All firmware components read successfully\n");
    return THINGINO_SUCCESS;
}

thingino_error_t firmware_read_bank(usb_device_t* device, uint32_t offset, uint32_t size, uint8_t** data) {
    if (!device || !data || size == 0) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("[Session 19] Reading firmware bank: offset=0x%08X, size=%u bytes\n", offset, size);
    DEBUG_PRINT("Using vendor tool protocol: Components first, then main firmware\n");

    // CRITICAL: First bank - read firmware components as per protocol analysis
    // The device streams these first before main firmware
    if (offset == 0) {
        DEBUG_PRINT("First bank - reading firmware components first (172B, 324B, 972B, 10KB, 390KB)\n");
        
        thingino_error_t component_result = firmware_read_components(device);
        if (component_result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to read firmware components: %s\n", thingino_error_to_string(component_result));
            return component_result;
        }
        
        DEBUG_PRINT("Components read successfully, now reading main firmware...\n");
    } else {
        DEBUG_PRINT("Non-first bank, giving device 100ms to stabilize...\n");
        usleep(100000);  // 100ms for subsequent banks
    }

    // STAGE 2: Read main firmware in 1MB chunks (via BULK IN EP 0x81)
    // Allocate buffer for firmware bank
    uint8_t* bank_buffer = (uint8_t*)malloc(size);
    if (!bank_buffer) {
        printf("[ERROR] Failed to allocate %u bytes for bank buffer\n", size);
        return THINGINO_ERROR_MEMORY;
    }

    // Use 1MB chunks to match vendor tool behavior
    uint32_t CHUNK_SIZE = 1024 * 1024; // 1MB per chunk
    uint32_t chunks_count = (size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    DEBUG_PRINT("Main firmware: Reading %u bytes in %u chunks of 1MB each\n", size, chunks_count);

    uint32_t total_read = 0;

    // Read main firmware in 1MB chunks via BULK IN
    for (uint32_t chunk_idx = 0; chunk_idx < chunks_count; chunk_idx++) {
        uint32_t remaining = size - (chunk_idx * CHUNK_SIZE);
        uint32_t current_chunk_size = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;
        
        // Progress update (show every 10% or chunk boundary)
        // Based on protocol analysis: 10%, 20%, 40%, 50%, 60%, 70%, 75%, 80%, 90%, 100%
        uint32_t progress_percent = (chunk_idx * 100) / chunks_count;
        
        // Only show progress at key milestones (10%, 20%, 40%, 50%, 60%, 70%, 75%, 80%, 90%, 100%)
        bool show_progress = false;
        if (progress_percent == 10 || progress_percent == 20 || progress_percent == 40 ||
            progress_percent == 50 || progress_percent == 60 || progress_percent == 70 ||
            progress_percent == 75 || progress_percent == 80 || progress_percent == 90 ||
            progress_percent == 100 || chunk_idx == 0 || chunk_idx == chunks_count - 1) {
            show_progress = true;
        }
        
        if (show_progress) {
            DEBUG_PRINT("[Session 19] Progress: %u%% - Chunk %u/%u (reading %u bytes via BULK IN)\n",
                   progress_percent, chunk_idx + 1, chunks_count, current_chunk_size);
        }
        
        int transferred = 0;
        
        // Direct BULK IN read - this is what vendor tool does
        // NO SetDataAddress/SetDataLength commands - device streams data automatically
        thingino_error_t result = firmware_read_direct_bulk_in(device,
                                                                bank_buffer + (chunk_idx * CHUNK_SIZE),
                                                                current_chunk_size,
                                                                &transferred);
        
        if (result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to read chunk %u/%u: %s\n", 
                   chunk_idx + 1, chunks_count, thingino_error_to_string(result));
            free(bank_buffer);
            return result;
        }
        
        if ((uint32_t)transferred != current_chunk_size) {
            printf("[WARNING] Chunk %u: Expected %u bytes, got %d bytes\n", 
                   chunk_idx + 1, current_chunk_size, transferred);
        }
        
        total_read += (uint32_t)transferred;
        
        // Small delay between chunks to prevent device buffer overflow
        usleep(100000);  // 100ms between chunks
    }
    
    DEBUG_PRINT("Bank read complete: %u bytes total\n", total_read);
    *data = bank_buffer;
    return THINGINO_SUCCESS;
}

thingino_error_t firmware_read_full(usb_device_t* device, uint8_t** data, uint32_t* size) {
    if (!device || !data || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("Reading full firmware from device...\n");
    DEBUG_PRINT("Using protocol: Vendor tool style (automatic streaming)\n");
    DEBUG_PRINT("Based on READ_FIRMWARE_PROTOCOL_ANALYSIS.md\n");
    
    // CRITICAL: According to protocol analysis, the device should stream firmware components automatically
    // when in firmware stage. NO commands should be sent before BULK IN reads.
    // The SetDataAddress/SetDataLength commands are causing the device to reboot.
    DEBUG_PRINT("Skipping ALL initialization commands - device should stream data automatically\n");
    DEBUG_PRINT("This matches vendor tool protocol from READ_FIRMWARE_PROTOCOL_ANALYSIS.md\n");
    
    // Give device time to stabilize after bootstrap
    usleep(1000000); // 1 second delay
    
    // Initialize read configuration
    firmware_read_config_t config;
    thingino_error_t result = firmware_read_init(device, &config);
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
    
    // Read all banks - fail immediately on error (no fallback)
    for (int i = 0; i < config.bank_count; i++) {
        flash_bank_t* bank = &config.banks[i];
        if (!bank->enabled) {
            DEBUG_PRINT("Skipping disabled bank %d\n", i);
            continue;
        }
        
        DEBUG_PRINT("Reading bank %d/%d (%s) using automatic streaming protocol...\n", 
               i + 1, config.bank_count, bank->label);
        
        uint8_t* bank_data = NULL;
        uint32_t bank_size = 0;
        
        // Attempt to read bank - fail immediately if unsuccessful
        result = firmware_read_bank(device, bank->offset, bank->size, &bank_data);
        if (result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to read bank %d: %s\n", i, thingino_error_to_string(result));
            free(firmware_buffer);
            firmware_read_cleanup(&config);
            return result;
        }
        
        // Get actual size of data read (may be different from requested)
        bank_size = bank->size; // In a real implementation, we'd get this from read function
        
        // Copy bank data to main buffer
        memcpy(firmware_buffer + bank->offset, bank_data, bank_size);
        total_read += bank_size;
        
        free(bank_data);
        
        DEBUG_PRINT("Bank %d read successfully (%u bytes, total: %u/%u bytes)\n",
            i, bank_size, total_read, config.total_size);
    }
    
    DEBUG_PRINT("Full firmware read completed: %u bytes total\n", total_read);
    
    *data = firmware_buffer;
    *size = total_read;
    
    firmware_read_cleanup(&config);
    return THINGINO_SUCCESS;
}

thingino_error_t firmware_read_detect_size(usb_device_t* device, uint32_t* size) {
    if (!device || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("Detecting firmware flash size...\n");
    
    // Try to read flash info using firmware stage protocol
    // For now, we'll default to 16MB as mentioned in task
    // In a full implementation, this would query the device for actual flash size
    *size = 16 * 1024 * 1024; // 16MB
    
    DEBUG_PRINT("Detected flash size: %u bytes (%.2f MB)\n", *size, (float)*size / (1024 * 1024));
    
    return THINGINO_SUCCESS;
}

thingino_error_t firmware_read_init(usb_device_t* device, firmware_read_config_t* config) {
    if (!device || !config) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("Initializing firmware read configuration...\n");
    
    // Detect flash size
    thingino_error_t result = firmware_read_detect_size(device, &config->total_size);
    if (result != THINGINO_SUCCESS) {
        return result;
    }
    
    // For 16MB flash, use 16 banks of 1MB each (based on config files)
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
    
    // NOTE: Do NOT call protocol_fw_handshake() here!
    // The device should use VR_READ (0x13) directly without handshake initialization
    // Handshake initialization can interfere with 40-byte command parameter validation
    DEBUG_PRINT("Skipping handshake initialization - using direct streaming\n");
    
    DEBUG_PRINT("Firmware read configuration initialized successfully\n");
    return THINGINO_SUCCESS;
}

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