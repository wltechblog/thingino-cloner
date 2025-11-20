#include "thingino.h"
#include "flash_descriptor.h"
#include <unistd.h>  // for sleep()

// ============================================================================
// GLOBAL DEBUG FLAG
// ============================================================================

bool g_debug_enabled = false;

// ============================================================================
// MAIN CLI INTERFACE
// ============================================================================

typedef struct {
    bool verbose;
    bool debug;
    bool list_devices;
    bool bootstrap;
    bool read_firmware;
    bool write_firmware;
    int device_index;
    char* config_file;
    char* spl_file;
    char* uboot_file;
    char* output_file;
    char* input_file;
    bool force_erase;
    bool skip_ddr;
} cli_options_t;

void print_usage(const char* program_name) {
    printf("Thingino Cloner - USB Device Cloner for Ingenic Processors\n");
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --verbose           Enable verbose logging\n");
    printf("  -d, --debug             Enable debug output\n");
    printf("  -l, --list             List connected devices\n");
    printf("  -i, --index <num>       Device index to operate on (default: 0)\n");
    printf("  -b, --bootstrap         Bootstrap device to firmware stage\n");
    printf("  -r, --read <file>       Read firmware from device to file\n");
    printf("  -w, --write <file>       Write firmware from file to device\n");
    printf("      --erase              Request full flash erase before writing (when supported)\n");
    printf("  --config <file>         Custom DDR configuration file\n");
    printf("  --spl <file>            Custom SPL file\n");
    printf("  --uboot <file>          Custom U-Boot file\n");
    printf("  --skip-ddr              Skip DDR configuration during bootstrap\n");
    printf("\nExamples:\n");
    printf("  %s -l                           # List devices\n", program_name);
    printf("  %s -i 0 -b                      # Bootstrap device 0\n", program_name);
    printf("  %s -i 0 -r firmware.bin          # Read firmware\n", program_name);
    printf("  %s -i 0 -w firmware.bin          # Write firmware\n", program_name);
    printf("\nProcessor Variants Supported:\n");
    printf("  T31X, T31ZX (primary targets)\n");
    printf("  T20, T21, T23, T30, T31, T40, T41\n");
    printf("  X1000, X1600, X1700, X2000, X2100, X2600\n");
}

thingino_error_t parse_arguments(int argc, char* argv[], cli_options_t* options) {
    // Initialize options
    memset(options, 0, sizeof(cli_options_t));
    options->device_index = 0;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            options->verbose = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            options->debug = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            options->list_devices = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bootstrap") == 0) {
            options->bootstrap = true;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--read") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->read_firmware = true;
            options->output_file = argv[++i];
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--write") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->write_firmware = true;
            options->input_file = argv[++i];
        } else if (strcmp(argv[i], "--config") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->config_file = argv[++i];
        } else if (strcmp(argv[i], "--spl") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->spl_file = argv[++i];
        } else if (strcmp(argv[i], "--uboot") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a filename\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->uboot_file = argv[++i];
        } else if (strcmp(argv[i], "--skip-ddr") == 0) {
            options->skip_ddr = true;
        } else if (strcmp(argv[i], "--erase") == 0) {
            options->force_erase = true;
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--index") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a device index\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->device_index = atoi(argv[++i]);
            if (options->device_index < 0) {
                printf("Error: device index must be >= 0\n");
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
        } else {
            printf("Error: Unknown option %s\n", argv[i]);
            print_usage(argv[0]);
            return THINGINO_ERROR_INVALID_PARAMETER;
        }
    }
    
    return THINGINO_SUCCESS;
}

thingino_error_t list_devices(usb_manager_t* manager) {
    printf("Scanning for Ingenic devices...\n\n");
    
    device_info_t* devices;
    int device_count;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }
    
    if (device_count == 0) {
        printf("No Ingenic devices found\n");
        return THINGINO_SUCCESS;
    }
    
    printf("Found %d device(s):\n", device_count);
    printf("Index | Bus | Addr | Vendor  | Product | Stage    | Variant\n");
    printf("-----|-----|------|---------|----------|--------\n");
    
    for (int i = 0; i < device_count; i++) {
        device_info_t* dev = &devices[i];
        printf("%5d | %3d | %4d | 0x%04X  | 0x%04X  | %-8s | %s\n",
            i, dev->bus, dev->address, dev->vendor, dev->product,
            device_stage_to_string(dev->stage),
            processor_variant_to_string(dev->variant));
    }
    
    printf("\n");
    free(devices);
    return THINGINO_SUCCESS;
}

thingino_error_t bootstrap_device_by_index(usb_manager_t* manager, int index, const cli_options_t* options) {
    // Get devices
    device_info_t* devices;
    int device_count;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }
    
    if (device_count == 0) {
        printf("No devices found\n");
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    if (index >= device_count) {
        printf("Error: device index %d out of range (found %d devices)\n", 
            index, device_count);
        free(devices);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Show device info
    device_info_t* device_info = &devices[index];
    printf("Bootstrapping device [%d]: %s %s (Bus %03d Address %03d)\n", 
        index, processor_variant_to_string(device_info->variant), 
        device_stage_to_string(device_info->stage), 
        device_info->bus, device_info->address);
    printf("  Vendor: 0x%04x, Product: 0x%04x\n", 
        device_info->vendor, device_info->product);
    
    // Open device
    DEBUG_PRINT("Opening device...\n");
    usb_device_t* device;
    result = usb_manager_open_device(manager, device_info, &device);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to open device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }
    DEBUG_PRINT("Device opened successfully\n");
    DEBUG_PRINT("Device variant from manager: %d (%s)\n",
        device_info->variant, processor_variant_to_string(device_info->variant));
    DEBUG_PRINT("Device variant from opened device: %d (%s)\n",
        device->info.variant, processor_variant_to_string(device->info.variant));
    
    // Create bootstrap config
    bootstrap_config_t config = {
        .sdram_address = BOOTLOADER_ADDRESS_SDRAM,
        .timeout = BOOTSTRAP_TIMEOUT_SECONDS,
        .verbose = options->verbose,
        .skip_ddr = options->skip_ddr,
        .config_file = options->config_file,
        .spl_file = options->spl_file,
        .uboot_file = options->uboot_file
    };
    
    // Run bootstrap
    result = bootstrap_device(device, &config);
    if (result != THINGINO_SUCCESS) {
        printf("Bootstrap failed: %s\n", thingino_error_to_string(result));
    } else {
        printf("Bootstrap completed successfully!\n");
    }
    
    // Cleanup
    usb_device_close(device);
    free(device);
    free(devices);
    
    return result;
}

/**
 * CLI Command: Read Firmware from Device
 * 
 * PROTOCOL WORKFLOW (Session 13 Update - FIRMWARE_UPLOAD_DOWNLOAD_ANALYSIS.md):
 * ==================================================================================
 * 
 * 1. DEVICE DETECTION & BOOTSTRAP:
 *    - Scan for Ingenic devices via USB VID/PID
 *    - Check device stage (bootrom vs firmware)
 *    - If bootrom: Auto-bootstrap to firmware stage
 *    - Wait for device re-enumeration (2-3 seconds)
 * 
 * 2. HANDSHAKE PROTOCOL INITIALIZATION:
 *    - Send VR_FW_HANDSHAKE to enter firmware read mode
 *    - Device acknowledges and prepares for transfers
 * 
 * 3. FIRMWARE READ (Session 13 - Using firmware_handshake_read_chunk):
 *    For each 1MB chunk:
 *    a) Send handshake command via firmware_handshake_read_chunk():
 *       - Alternates between VR_FW_WRITE1 (0x13) and VR_FW_WRITE2 (0x14)
 *       - 40-byte command buffer containing:
 *         * Flash offset (bytes 0-3, little-endian)
 *         * Chunk size (bytes 20-23, little-endian)
 *         * Vendor pattern (bytes 32-37): {0x06, 0x00, 0x05, 0x7F, 0x00, 0x00}
 *         * Chunk index (bytes 38-39)
 *    b) Receive 8-byte status handshake from device
 *    c) Perform bulk-in transfer to receive firmware data
 *    d) On failure: fallback to protocol_vendor_style_read() for reliability
 * 
 * 4. FILE WRITE & CLEANUP:
 *    - Save firmware to output file
 *    - Close USB device
 *    - Cleanup resources
 * 
 * NOTE: Session 13 upgrades the firmware_read_bank() function to use the new
 * firmware_handshake_read_chunk() function with proper alternating command pattern
 * and status verification. Falls back to vendor-style read if handshake fails.
 */
thingino_error_t read_firmware_from_device(usb_manager_t* manager, int index, const char* output_file, const cli_options_t* options) {
    // Get devices
    device_info_t* devices;
    int device_count;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }
    
    if (device_count == 0) {
        printf("No devices found\n");
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }
    
    if (index >= device_count) {
        printf("Error: device index %d out of range (found %d devices)\n", 
            index, device_count);
        free(devices);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Show device info
    device_info_t* device_info = &devices[index];
    printf("Reading firmware from device [%d]: %s %s (Bus %03d Address %03d)\n", 
        index, processor_variant_to_string(device_info->variant), 
        device_stage_to_string(device_info->stage), 
        device_info->bus, device_info->address);
    
    // Check if device is in firmware stage, but also verify by getting CPU info
    printf("Checking device stage...\n");
    usb_device_t* test_device;
    result = usb_manager_open_device(manager, device_info, &test_device);
    if (result == THINGINO_SUCCESS) {
        cpu_info_t cpu_info;
        thingino_error_t cpu_result = usb_device_get_cpu_info(test_device, &cpu_info);
        if (cpu_result == THINGINO_SUCCESS) {
            // Show raw hex bytes for debugging
            printf("CPU magic (raw hex): ");
            for (int i = 0; i < 8; i++) {
                printf("%02X ", cpu_info.magic[i]);
            }
            printf("\n");

            printf("Current device stage: %s (CPU magic: %.8s)\n",
                device_stage_to_string(cpu_info.stage), cpu_info.magic);

            // Detect and display processor variant
            processor_variant_t detected_variant = detect_variant_from_magic(cpu_info.clean_magic);
            printf("Detected processor variant: %s (from magic: '%s')\n",
                processor_variant_to_string(detected_variant), cpu_info.clean_magic);

            // Check if device PID matches firmware stage
            bool pid_is_firmware = (device_info->product == PRODUCT_ID_FIRMWARE ||
                                   device_info->product == PRODUCT_ID_FIRMWARE2);
            bool cpu_is_firmware = (cpu_info.stage == STAGE_FIRMWARE);

            // Device needs bootstrap if:
            // 1. CPU magic indicates bootrom stage, OR
            // 2. CPU magic indicates firmware but PID is still bootrom (transitional state)
            if (!cpu_is_firmware || (cpu_is_firmware && !pid_is_firmware)) {
                if (cpu_is_firmware && !pid_is_firmware) {
                    printf("Device CPU shows firmware stage but USB PID is still bootrom\n");
                    printf("Device is in transitional state - waiting for re-enumeration...\n");
                    usb_device_close(test_device);
                    free(test_device);
                    test_device = NULL;

                    // Wait for device to re-enumerate
                    sleep(1);

                    // Re-scan for devices
                    printf("Re-scanning for devices after transition...\n");
                    if (devices) {
                        free(devices);
                        devices = NULL;
                    }
                    device_count = 0;

                    result = usb_manager_find_devices(manager, &devices, &device_count);
                    if (result != THINGINO_SUCCESS || device_count == 0) {
                        printf("Failed to find device after transition\n");
                        if (devices) free(devices);
                        return THINGINO_ERROR_DEVICE_NOT_FOUND;
                    }

                    // Find the device again (should have firmware PID now)
                    device_info = NULL;
                    for (int i = 0; i < device_count; i++) {
                        bool is_fw_pid = (devices[i].product == PRODUCT_ID_FIRMWARE ||
                                         devices[i].product == PRODUCT_ID_FIRMWARE2);
                        if (devices[i].stage == STAGE_FIRMWARE && is_fw_pid) {
                            device_info = &devices[i];
                            printf("Found device with firmware PID: Bus %03d Address %03d (PID: 0x%04x)\n",
                                device_info->bus, device_info->address, device_info->product);
                            break;
                        }
                    }

                    if (!device_info) {
                        printf("Device not found with firmware PID after transition\n");
                        printf("Note: Some devices keep bootrom PID even after loading U-Boot\n");
                        printf("Accepting device with bootrom PID and firmware CPU magic\n");

                        // Accept the device with bootrom PID if it has firmware CPU magic
                        for (int i = 0; i < device_count; i++) {
                            if (devices[i].product == PRODUCT_ID_BOOTROM2 || devices[i].product == PRODUCT_ID_BOOTROM) {
                                device_info = &devices[i];
                                printf("Using device: Bus %03d Address %03d (PID: 0x%04x)\n",
                                    device_info->bus, device_info->address, device_info->product);
                                break;
                            }
                        }

                        if (!device_info) {
                            printf("No Ingenic device found after transition\n");
                            free(devices);
                            return THINGINO_ERROR_DEVICE_NOT_FOUND;
                        }
                    }

                    // Open the device for firmware reading
                    printf("Opening device for firmware reading...\n");
                    result = usb_manager_open_device(manager, device_info, &test_device);
                    if (result != THINGINO_SUCCESS) {
                        printf("Failed to open device: %s\n", thingino_error_to_string(result));
                        free(devices);
                        return result;
                    }

                    // Verify it's in firmware stage
                    cpu_result = usb_device_get_cpu_info(test_device, &cpu_info);
                    if (cpu_result != THINGINO_SUCCESS || cpu_info.stage != STAGE_FIRMWARE) {
                        printf("Device not in firmware stage after opening\n");
                        usb_device_close(test_device);
                        free(test_device);
                        free(devices);
                        return THINGINO_ERROR_PROTOCOL;
                    }

                    printf("Device opened successfully and verified in firmware stage\n");
                    printf("Keeping device open for firmware reading to avoid re-enumeration\n");
                } else {
                    printf("Device not in firmware stage, attempting bootstrap first...\n");
                    usb_device_close(test_device);
                    free(test_device);
                    test_device = NULL;

                    // Bootstrap device - pass through the original options to preserve custom file paths
                result = bootstrap_device_by_index(manager, index, options);
                
                if (result != THINGINO_SUCCESS) {
                    printf("Bootstrap failed: %s\n", thingino_error_to_string(result));
                    free(devices);
                    return result;
                }
                
                // Re-check device stage after bootstrap
                // Device may have re-enumerated with new address, so wait and re-scan
                printf("Waiting for device to stabilize after bootstrap...\n");

                // Close the test device (it's now invalid after bootstrap)
                if (test_device) {
                    usb_device_close(test_device);
                    free(test_device);
                    test_device = NULL;
                }

                // Wait for device to re-enumerate and fully stabilize
                // Device may re-enumerate multiple times after ProgStage2
                // User reports it takes about 10 seconds to reappear
                printf("Waiting 1 seconds for device to fully stabilize...\n");
                sleep(1);

                // Re-scan for devices to get updated address
                printf("Re-scanning for devices after bootstrap...\n");
                if (devices) {
                    free(devices);
                    devices = NULL;
                }
                device_count = 0;

                result = usb_manager_find_devices(manager, &devices, &device_count);
                if (result != THINGINO_SUCCESS || device_count == 0) {
                    printf("Failed to find device after bootstrap\n");
                    if (devices) {
                        free(devices);
                        devices = NULL;
                    }
                    return THINGINO_ERROR_DEVICE_NOT_FOUND;
                }

                // Find the device again (should be in firmware stage now)
                // Note: Device may still have bootrom PID but firmware CPU magic during transition
                device_info = NULL;
                for (int i = 0; i < device_count; i++) {
                    // Accept device if it's in firmware stage OR if it has bootrom PID but we can verify CPU magic
                    if (devices[i].stage == STAGE_FIRMWARE) {
                        device_info = &devices[i];
                        printf("Found device in firmware stage: Bus %03d Address %03d\n",
                            device_info->bus, device_info->address);
                        break;
                    } else if (devices[i].product == PRODUCT_ID_BOOTROM2 || devices[i].product == PRODUCT_ID_BOOTROM) {
                        // Device might be in transitional state - verify with CPU magic
                        printf("Found device with bootrom PID, verifying CPU magic...\n");
                        usb_device_t* verify_device;
                        if (usb_manager_open_device(manager, &devices[i], &verify_device) == THINGINO_SUCCESS) {
                            cpu_info_t verify_cpu;
                            if (usb_device_get_cpu_info(verify_device, &verify_cpu) == THINGINO_SUCCESS) {
                                if (verify_cpu.stage == STAGE_FIRMWARE) {
                                    printf("Device has firmware CPU magic (%.8s), using it\n", verify_cpu.magic);
                                    device_info = &devices[i];
                                    usb_device_close(verify_device);
                                    free(verify_device);
                                    break;
                                }
                            }
                            usb_device_close(verify_device);
                            free(verify_device);
                        }
                    }
                }

                if (!device_info) {
                    printf("Device not found in firmware stage after bootstrap\n");
                    if (devices) {
                        free(devices);
                        devices = NULL;
                    }
                    return THINGINO_ERROR_DEVICE_NOT_FOUND;
                }

                // Verify it's in firmware stage and keep device open for firmware reading
                result = usb_manager_open_device(manager, device_info, &test_device);
                if (result == THINGINO_SUCCESS) {
                    cpu_result = usb_device_get_cpu_info(test_device, &cpu_info);
                    if (cpu_result == THINGINO_SUCCESS && cpu_info.stage == STAGE_FIRMWARE) {
                        printf("Device successfully bootstrapped to firmware stage\n");
                        printf("Keeping device open for firmware reading to avoid re-enumeration\n");
                        // DON'T close the device - we'll reuse this handle for firmware reading
                    } else {
                        printf("Bootstrap completed but device still not in firmware stage\n");
                        if (test_device) {
                            usb_device_close(test_device);
                            free(test_device);
                            test_device = NULL;
                        }
                        if (devices) {
                            free(devices);
                            devices = NULL;
                        }
                        return THINGINO_ERROR_PROTOCOL;
                    }
                } else {
                    printf("Failed to reopen device after bootstrap\n");
                    if (devices) {
                        free(devices);
                        devices = NULL;
                    }
                    return result;
                }
                }  // End of bootstrap if block
            } else {
                printf("Device is in firmware stage with correct PID, proceeding with read\n");
                printf("Keeping device open for firmware reading to avoid re-enumeration\n");
                // DON'T close the device - we'll reuse this handle for firmware reading
            }
        } else {
            printf("Failed to get CPU info for stage verification\n");
            usb_device_close(test_device);
            free(test_device);
            test_device = NULL;
        }
    } else {
        printf("Failed to open device for stage verification\n");
        test_device = NULL;
    }

    if (result != THINGINO_SUCCESS) {
        free(devices);
        return result;
    }

    // Reuse the already-open device handle for firmware reading
    // This avoids triggering re-enumeration by reopening the device
    usb_device_t* device = test_device;
    if (result != THINGINO_SUCCESS) {
        printf("Failed to open device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }
    
    printf("Reading firmware from device...\n");
    
    // Read full firmware from device
    uint8_t* firmware_data = NULL;
    uint32_t firmware_size = 0;
    result = firmware_read_full(device, &firmware_data, &firmware_size);
    
    if (result != THINGINO_SUCCESS) {
        printf("Failed to read firmware: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        free(devices);
        return result;
    }
    
    printf("Successfully read %u bytes from device\n", firmware_size);
    
    // Save to file
    FILE* file = fopen(output_file, "wb");
    if (!file) {
        printf("Failed to open output file: %s\n", output_file);
        free(firmware_data);
        usb_device_close(device);
        free(device);
        free(devices);
        return THINGINO_ERROR_FILE_IO;
    }
    
    size_t bytes_written = fwrite(firmware_data, 1, firmware_size, file);
    fclose(file);
    
    free(firmware_data);
    
    if (bytes_written != (size_t)firmware_size) {
        printf("Warning: only %zu of %u bytes written to file\n", bytes_written, firmware_size);
    } else {
        printf("Firmware successfully saved to: %s (%.2f MB)\n", 
            output_file, (float)firmware_size / (1024 * 1024));
    }
    
    // Cleanup
    usb_device_close(device);
    free(device);
    free(devices);
    
    return THINGINO_SUCCESS;
}

/**
 * Write firmware from file to device
 */
thingino_error_t write_firmware_from_file(usb_manager_t* manager, int device_index,
                                         const char* firmware_file, cli_options_t* options) {
    if (!manager || !firmware_file) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    (void)options;  // Unused for now

    printf("\n");
    printf("================================================================================\n");
    printf("FIRMWARE WRITE\n");
    printf("================================================================================\n");
    printf("\n");

    // List devices
    device_info_t* devices = NULL;
    int device_count = 0;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error listing devices: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (device_index >= device_count) {
        fprintf(stderr, "Error: Device index %d out of range (0-%d)\n",
                device_index, device_count - 1);
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }

    // Open device
    usb_device_t* device = NULL;
    result = usb_manager_open_device(manager, &devices[device_index], &device);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error opening device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }

    printf("Target Device:\n");
    printf("  Index: %d\n", device_index);
    printf("  Bus: %03d Address: %03d\n", devices[device_index].bus, devices[device_index].address);
    printf("  Variant: %s\n", processor_variant_to_string(devices[device_index].variant));
    printf("  Stage: %s\n", device_stage_to_string(devices[device_index].stage));
    printf("\n");

    // Check if device needs bootstrap
    if (devices[device_index].stage == STAGE_BOOTROM) {
        printf("Device is in bootrom stage. Bootstrapping to firmware stage first...\n\n");

        bootstrap_config_t bootstrap_config = {
            .skip_ddr = options->skip_ddr,
            .config_file = options->config_file,
            .spl_file = options->spl_file,
            .uboot_file = options->uboot_file,
            .sdram_address = 0x80000000,  // Default SDRAM address
            .timeout = 5000,
            .verbose = options->verbose
        };

        result = bootstrap_device(device, &bootstrap_config);
        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "Error: Bootstrap failed: %s\n", thingino_error_to_string(result));
            usb_device_close(device);
            free(device);
            free(devices);
            return result;
        }

        printf("\nBootstrap complete. Device should now be in firmware stage.\n");
        printf("Waiting for device to stabilize...\n\n");
        usleep(2000000);  // 2 seconds

        // Close and reopen device to get fresh connection
        usb_device_close(device);
        free(device);

        // Re-scan for device in firmware stage
        free(devices);
        devices = NULL;
        result = usb_manager_find_devices(manager, &devices, &device_count);
        if (result != THINGINO_SUCCESS || device_count == 0) {
            fprintf(stderr, "Error: Device not found after bootstrap\n");
            if (devices) free(devices);
            return THINGINO_ERROR_DEVICE_NOT_FOUND;
        }

        // Find the device again (it may have re-enumerated)
        int found_index = -1;
        for (int i = 0; i < device_count; i++) {
            if (devices[i].stage == STAGE_FIRMWARE) {
                found_index = i;
                break;
            }
        }

        if (found_index < 0) {
            fprintf(stderr, "Error: Device not in firmware stage after bootstrap\n");
            free(devices);
            return THINGINO_ERROR_PROTOCOL;
        }

        // Reopen device
        result = usb_manager_open_device(manager, &devices[found_index], &device);
        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "Error: Failed to reopen device: %s\n", thingino_error_to_string(result));
            free(devices);
            return result;
        }

        printf("Device reopened in firmware stage.\n\n");
    }

    free(devices);

    // Detect A1 firmware-stage boards via CPU magic so we can use the correct
    // flash descriptor (A1 uses XM25QH128B, T31x uses GD25Q127CSIG).
    bool is_a1_fw_stage = false;
    cpu_info_t fw_cpu_info;
    memset(&fw_cpu_info, 0, sizeof(fw_cpu_info));
    thingino_error_t fw_cpu_res = usb_device_get_cpu_info(device, &fw_cpu_info);
    if (fw_cpu_res == THINGINO_SUCCESS) {
        if (strncmp(fw_cpu_info.clean_magic, "A1", 2) == 0 ||
            strncmp(fw_cpu_info.clean_magic, "a1", 2) == 0) {
            is_a1_fw_stage = true;
            DEBUG_PRINT("Detected A1 CPU magic ('%s') in firmware stage\n",
                       fw_cpu_info.clean_magic);
        }
    }

    // Prepare burner protocol in firmware stage: send partition marker,
    // then flash descriptor, then initialize the firmware handshake
    // protocol. This mirrors the vendor write sequence more closely:
    //   - Chunk 3: 172-byte "ILOP" partition marker (bulk OUT)
    //   - Chunk 4: 972-byte flash descriptor + policies (contains "nor" string
    //     that tells A1 burner to use NOR flash mode instead of MMC mode)
    //   - Then firmware write handshakes and data chunks.
    //
    // NOTE: A1 boards also need this! The metadata contains the crucial "nor"
    // string at offset 0xF0 that tells the burner to use NOR flash mode.
    // Without it, the A1 burner tries to write to MMC/SD card and fails.
    if (device->info.stage == STAGE_FIRMWARE &&
        (device->info.variant == VARIANT_T31 ||
         device->info.variant == VARIANT_T31X ||
         device->info.variant == VARIANT_T31ZX)) {

        thingino_error_t prep_result = THINGINO_SUCCESS;

        printf("Preparing partition marker, flash descriptor and firmware handshake...\n");

        // 1) Send 172-byte partition marker ("ILOP" header)
        prep_result = flash_partition_marker_send(device);
        if (prep_result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to send partition marker: %s\n",
                   thingino_error_to_string(prep_result));
            usb_device_close(device);
            free(device);
            return prep_result;
        }

        // 2) Build and send full 972-byte flash descriptor
        // Use A1-specific descriptor for A1 boards, T31x descriptor otherwise.
        // The A1 descriptor contains the XM25QH128B flash chip info and the
        // crucial "nor" string at offset 0xF0 that tells the burner to use
        // NOR flash mode instead of MMC mode.
        uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE];
        int desc_result;
        if (is_a1_fw_stage) {
            desc_result = flash_descriptor_create_a1_writer_full(flash_descriptor);
            if (desc_result != 0) {
                printf("[ERROR] Failed to create A1 writer_full flash descriptor\n");
                usb_device_close(device);
                free(device);
                return THINGINO_ERROR_MEMORY;
            }
        } else {
            desc_result = flash_descriptor_create_t31x_writer_full(flash_descriptor);
            if (desc_result != 0) {
                printf("[ERROR] Failed to create T31x writer_full flash descriptor\n");
                usb_device_close(device);
                free(device);
                return THINGINO_ERROR_MEMORY;
            }
        }

        prep_result = flash_descriptor_send(device, flash_descriptor);
        if (prep_result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to send flash descriptor: %s\n",
                   thingino_error_to_string(prep_result));
            usb_device_close(device);
            free(device);
            return prep_result;
        }

        // Give the burner time to process descriptor, matching read path
        usleep(500000); // 500ms

        // 3) Initialize the firmware handshake protocol (VR_FW_HANDSHAKE)
        prep_result = firmware_handshake_init(device);
        if (prep_result != THINGINO_SUCCESS) {
            printf("[ERROR] Failed to initialize firmware handshake: %s\n",
                   thingino_error_to_string(prep_result));
            usb_device_close(device);
            free(device);
            return prep_result;
        }
    }

    // Get firmware binary (optional - can be NULL if not using embedded firmware)
    const firmware_binary_t* fw_binary = NULL;
    // TODO: Detect processor and get firmware binary
    // fw_binary = firmware_get("t31x");

    // Write firmware
    printf("Writing firmware to device...\n");
    printf("  Source file: %s\n", firmware_file);
    printf("\n");

    result = write_firmware_to_device(device, firmware_file, fw_binary, options->force_erase, is_a1_fw_stage);
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "Error: Firmware write failed: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    printf("\n");
    printf("================================================================================\n");
    printf("FIRMWARE WRITE COMPLETE\n");
    printf("================================================================================\n");
    printf("\n");

    usb_device_close(device);
    free(device);
    return THINGINO_SUCCESS;
}

int main(int argc, char* argv[]) {
    cli_options_t options;
    thingino_error_t result = parse_arguments(argc, argv, &options);
    if (result != THINGINO_SUCCESS) {
        return 1;
    }
    
    // Set global debug flag based on CLI options
    g_debug_enabled = options.debug;
    
    // Initialize USB manager
    usb_manager_t manager;
    result = usb_manager_init(&manager);
    if (result != THINGINO_SUCCESS) {
        printf("Failed to initialize USB manager: %s\n", thingino_error_to_string(result));
        return 1;
    }
    
    int exit_code = 0;
    
    if (options.list_devices) {
        result = list_devices(&manager);
        if (result != THINGINO_SUCCESS) {
            exit_code = 1;
        }
    } else if (options.bootstrap) {
        result = bootstrap_device_by_index(&manager, options.device_index, &options);
        if (result != THINGINO_SUCCESS) {
            exit_code = 1;
        }
    } else if (options.read_firmware) {
        result = read_firmware_from_device(&manager, options.device_index,
            options.output_file, &options);
        if (result != THINGINO_SUCCESS) {
            exit_code = 1;
        }
    } else if (options.write_firmware) {
        result = write_firmware_from_file(&manager, options.device_index,
            options.input_file, &options);
        if (result != THINGINO_SUCCESS) {
            exit_code = 1;
        }
    } else {
        printf("No action specified. Use -h for help.\n");
        exit_code = 1;
    }
    
    // Cleanup
    usb_manager_cleanup(&manager);
    
    return exit_code;
}
