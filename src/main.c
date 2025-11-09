#include "thingino.h"
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
    bool skip_ddr;
    char* force_variant;  // Force specific processor variant
    uint32_t stage2_addr_override; // 0 = default; otherwise override Stage-2 address
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
    printf("  -w, --write <file>      Write firmware from file to device\n");
    printf("  --config <file>         Custom DDR configuration file\n");
    printf("  --spl <file>            Custom SPL file\n");
    printf("  --uboot <file>          Custom U-Boot file\n");
    printf("  --stage2-addr <addr>    Override Stage-2 U-Boot address (e.g., 0x80100000)\n");
    printf("  --skip-ddr              Skip DDR configuration during bootstrap\n");
    printf("  --variant <type>        Force processor variant (t31x, t31zx, t41, t41n, etc.)\n");
    printf("\nExamples:\n");
    printf("  %s -l                           # List devices\n", program_name);
    printf("  %s -i 0 -b                      # Bootstrap device 0\n", program_name);
    printf("  %s -i 0 -r firmware.bin          # Read firmware\n", program_name);
    printf("  %s -i 0 -w firmware.bin          # Write firmware\n", program_name);
    printf("\nProcessor Variants Supported:\n");
    printf("  T31X, T31ZX (primary targets)\n");
    printf("  T20, T21, T23, T30, T31, T40, T41, T41N\n");
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
        } else if (strcmp(argv[i], "--stage2-addr") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires an address\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            char* endp = NULL;
            unsigned long val = strtoul(argv[++i], &endp, 0);
            if (endp == argv[i] || val == 0) {
                printf("Error: invalid address '%s'\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->stage2_addr_override = (uint32_t)val;
        } else if (strcmp(argv[i], "--skip-ddr") == 0) {
            options->skip_ddr = true;
        } else if (strcmp(argv[i], "--variant") == 0) {
            if (i + 1 >= argc) {
                printf("Error: %s requires a variant name\n", argv[i]);
                return THINGINO_ERROR_INVALID_PARAMETER;
            }
            options->force_variant = argv[++i];
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

    // Apply variant override if specified
    if (options->force_variant) {
        processor_variant_t old_variant = device_info->variant;

        // Parse variant string
        if (strcmp(options->force_variant, "t20") == 0) device_info->variant = VARIANT_T20;
        else if (strcmp(options->force_variant, "t21") == 0) device_info->variant = VARIANT_T21;
        else if (strcmp(options->force_variant, "t23") == 0) device_info->variant = VARIANT_T23;
        else if (strcmp(options->force_variant, "t30") == 0) device_info->variant = VARIANT_T30;
        else if (strcmp(options->force_variant, "t31") == 0) device_info->variant = VARIANT_T31;
        else if (strcmp(options->force_variant, "t31x") == 0) device_info->variant = VARIANT_T31X;
        else if (strcmp(options->force_variant, "t31zx") == 0) device_info->variant = VARIANT_T31ZX;
        else if (strcmp(options->force_variant, "t40") == 0) device_info->variant = VARIANT_T40;
        else if (strcmp(options->force_variant, "t41") == 0) device_info->variant = VARIANT_T41;
        else if (strcmp(options->force_variant, "t41n") == 0) device_info->variant = VARIANT_T41N;
        else if (strcmp(options->force_variant, "x1000") == 0) device_info->variant = VARIANT_X1000;
        else if (strcmp(options->force_variant, "x1600") == 0) device_info->variant = VARIANT_X1600;
        else if (strcmp(options->force_variant, "x1700") == 0) device_info->variant = VARIANT_X1700;
        else if (strcmp(options->force_variant, "x2000") == 0) device_info->variant = VARIANT_X2000;
        else if (strcmp(options->force_variant, "x2100") == 0) device_info->variant = VARIANT_X2100;
        else if (strcmp(options->force_variant, "x2600") == 0) device_info->variant = VARIANT_X2600;
        else {
            printf("Error: Unknown variant '%s'\n", options->force_variant);
            free(devices);
            return THINGINO_ERROR_INVALID_PARAMETER;
        }

        printf("Variant override: %s -> %s\n",
            processor_variant_to_string(old_variant),
            processor_variant_to_string(device_info->variant));
    }

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
        .uboot_file = options->uboot_file,
        .uboot_address_override = options->stage2_addr_override
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

    // Apply variant override if specified
    if (options->force_variant) {
        processor_variant_t old_variant = device_info->variant;

        // Parse variant string
        if (strcmp(options->force_variant, "t20") == 0) device_info->variant = VARIANT_T20;
        else if (strcmp(options->force_variant, "t21") == 0) device_info->variant = VARIANT_T21;
        else if (strcmp(options->force_variant, "t23") == 0) device_info->variant = VARIANT_T23;
        else if (strcmp(options->force_variant, "t30") == 0) device_info->variant = VARIANT_T30;
        else if (strcmp(options->force_variant, "t31") == 0) device_info->variant = VARIANT_T31;
        else if (strcmp(options->force_variant, "t31x") == 0) device_info->variant = VARIANT_T31X;
        else if (strcmp(options->force_variant, "t31zx") == 0) device_info->variant = VARIANT_T31ZX;
        else if (strcmp(options->force_variant, "t40") == 0) device_info->variant = VARIANT_T40;
        else if (strcmp(options->force_variant, "t41n") == 0) device_info->variant = VARIANT_T41N;
        else if (strcmp(options->force_variant, "t41") == 0) device_info->variant = VARIANT_T41;
        else if (strcmp(options->force_variant, "x1000") == 0) device_info->variant = VARIANT_X1000;
        else if (strcmp(options->force_variant, "x1600") == 0) device_info->variant = VARIANT_X1600;
        else if (strcmp(options->force_variant, "x1700") == 0) device_info->variant = VARIANT_X1700;
        else if (strcmp(options->force_variant, "x2000") == 0) device_info->variant = VARIANT_X2000;
        else if (strcmp(options->force_variant, "x2100") == 0) device_info->variant = VARIANT_X2100;
        else if (strcmp(options->force_variant, "x2600") == 0) device_info->variant = VARIANT_X2600;
        else {
            printf("Error: Unknown variant '%s'\n", options->force_variant);
            free(devices);
            return THINGINO_ERROR_INVALID_PARAMETER;
        }

        printf("Variant override: %s -> %s\n",
            processor_variant_to_string(old_variant),
            processor_variant_to_string(device_info->variant));
    }

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
            printf("Current device stage: %s (CPU magic: %.8s)\n",
                device_stage_to_string(cpu_info.stage), cpu_info.magic);

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
        printf("Firmware write functionality not yet implemented\n");
        exit_code = 1;
    } else {
        printf("No action specified. Use -h for help.\n");
        exit_code = 1;
    }

    // Cleanup
    usb_manager_cleanup(&manager);

    return exit_code;
}