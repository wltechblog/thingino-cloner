/**
 * High-level device operations
 *
 * Protocol logic for bootstrap, read, and write operations.
 * These were extracted from cli/main.c to live in the library
 * so that any frontend (CLI, daemon, etc.) can use them.
 */

#include "thingino.h"
#include "cloner/platform_profile.h"
#include "flash_descriptor.h"
#include "platform.h"
#include "spi_nor_db.h"
#include <unistd.h>

/* Last detected variant from bootstrap auto-detect, so callers can
 * pass it to subsequent read/write operations. */
static const char *g_last_detected_variant = NULL;

const char *cloner_get_last_detected_variant(void) {
    return g_last_detected_variant;
}

/* ========================================================================== */
/* Bootstrap a device by index                                                */
/* ==========================================================================*/

thingino_error_t cloner_op_bootstrap(usb_manager_t *manager, int index, const char *force_cpu, bool verbose,
                                     bool skip_ddr, const char *config_file, const char *spl_file,
                                     const char *uboot_file, const char *firmware_dir) {
    /* Get devices */
    device_info_t *devices;
    int device_count;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (device_count == 0) {
        LOG_INFO("No devices found\n");
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }

    if (index >= device_count) {
        LOG_INFO("Error: device index %d out of range (found %d devices)\n", index, device_count);
        free(devices);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    /* Show device info */
    device_info_t *device_info = &devices[index];
    LOG_INFO("Bootstrapping device [%d]: %s %s (Bus %03d Address %03d)\n", index,
             processor_variant_to_string(device_info->variant), device_stage_to_string(device_info->stage),
             device_info->bus, device_info->address);
    LOG_INFO("  Vendor: 0x%04x, Product: 0x%04x\n", device_info->vendor, device_info->product);

    /* Open device */
    DEBUG_PRINT("Opening device...\n");
    usb_device_t *device;
    result = usb_manager_open_device(manager, device_info, &device);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Failed to open device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }
    DEBUG_PRINT("Device opened successfully\n");
    DEBUG_PRINT("Device variant from manager: %d (%s)\n", device_info->variant,
                processor_variant_to_string(device_info->variant));
    DEBUG_PRINT("Device variant from opened device: %d (%s)\n", device->info.variant,
                processor_variant_to_string(device->info.variant));

    /* Determine CPU variant: use --cpu override, or auto-detect from hardware */
    if (force_cpu) {
        processor_variant_t forced_variant = string_to_processor_variant(force_cpu);
        LOG_INFO("Forcing CPU variant to: %s (was: %s)\n", processor_variant_to_string(forced_variant),
                 processor_variant_to_string(device->info.variant));
        device->info.variant = forced_variant;
    } else {
        /* Auto-detect SoC by uploading a tiny MIPS program via VR_PROG_STAGE1.
         * The program reads SoC ID registers and stores results in TCSM.
         * STAGE1 is one-shot on some SoCs (T32), so after detect we do a USB
         * reset to restart the bootrom's state machine and clear the flag. */
        LOG_INFO("Auto-detecting SoC...\n");
        processor_variant_t detected = VARIANT_T31X;
        if (protocol_detect_soc(device, &detected) == THINGINO_SUCCESS) {
            device->info.variant = detected;
            LOG_INFO("  Detected: %s\n", processor_variant_to_string(detected));
        } else {
            LOG_WARN("SoC auto-detect failed, using CPU magic fallback\n");
        }
    }

    /* Save detected variant for subsequent operations */
    g_last_detected_variant = processor_variant_to_string(device->info.variant);

    /* Create bootstrap config */
    bootstrap_config_t config = {
        .sdram_address = BOOTLOADER_ADDRESS_SDRAM,
        .timeout = BOOTSTRAP_TIMEOUT_SECONDS,
        .verbose = verbose,
        .skip_ddr = skip_ddr,
        .config_file = config_file,
        .spl_file = spl_file,
        .uboot_file = uboot_file,
        .firmware_dir = firmware_dir,
    };

    /* Run bootstrap */
    result = bootstrap_device(device, &config);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Bootstrap failed: %s\n", thingino_error_to_string(result));
    } else {
        LOG_INFO("Bootstrap completed successfully!\n");
    }

    /* Cleanup */
    usb_device_close(device);
    free(device);
    free(devices);

    return result;
}

/* ========================================================================== */
/* Read firmware from device                                                  */
/* ========================================================================== */

thingino_error_t cloner_op_read_firmware(usb_manager_t *manager, int index, const char *output_file,
                                         const char *force_cpu, const char *flash_chip_name) {
    device_info_t *devices = NULL;
    int device_count = 0;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Failed to list devices: %s\n", thingino_error_to_string(result));
        return result;
    }
    if (device_count == 0 || index >= device_count) {
        LOG_INFO("No device at index %d (found %d)\n", index, device_count);
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }

    usb_device_t *device = NULL;
    result = usb_manager_open_device(manager, &devices[index], &device);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Failed to open device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }

    /* Apply CPU override, or auto-detect if in bootrom */
    if (force_cpu) {
        processor_variant_t forced = string_to_processor_variant(force_cpu);
        LOG_INFO("Forcing CPU variant to: %s (was: %s)\n", force_cpu,
                 processor_variant_to_string(device->info.variant));
        device->info.variant = forced;
    } else if (devices[index].stage == STAGE_BOOTROM) {
        LOG_INFO("Auto-detecting SoC...\n");
        processor_variant_t detected = VARIANT_T31X;
        if (protocol_detect_soc(device, &detected) == THINGINO_SUCCESS) {
            device->info.variant = detected;
        } else {
            LOG_WARN("SoC auto-detect failed, using CPU magic fallback\n");
        }
    }

    /* Bootstrap if device is in bootrom stage */
    if (devices[index].stage == STAGE_BOOTROM) {
        LOG_INFO("Device is in bootrom stage. Bootstrapping to firmware stage first...\n\n");

        bootstrap_config_t bootstrap_config = {
            .firmware_dir = "./firmwares", .sdram_address = 0x80000000, .timeout = 5000};

        result = bootstrap_device(device, &bootstrap_config);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Bootstrap failed: %s\n", thingino_error_to_string(result));
            usb_device_close(device);
            free(device);
            free(devices);
            return result;
        }

        LOG_INFO("\nBootstrap complete. Waiting for device...\n\n");
        platform_sleep_ms(2000);

        usb_device_close(device);
        free(device);

        free(devices);
        devices = NULL;
        result = usb_manager_find_devices(manager, &devices, &device_count);
        if (result != THINGINO_SUCCESS || device_count == 0) {
            LOG_ERROR("Device not found after bootstrap\n");
            if (devices)
                free(devices);
            return THINGINO_ERROR_DEVICE_NOT_FOUND;
        }

        int found = -1;
        for (int i = 0; i < device_count; i++) {
            if (devices[i].stage == STAGE_FIRMWARE) {
                found = i;
                break;
            }
        }
        if (found < 0) {
            /* Accept bootrom PID with firmware CPU magic (T40/T41) */
            for (int i = 0; i < device_count; i++) {
                if (devices[i].product == 0xC309) {
                    found = i;
                    break;
                }
            }
        }
        if (found < 0) {
            LOG_ERROR("Device not in firmware stage after bootstrap\n");
            free(devices);
            return THINGINO_ERROR_PROTOCOL;
        }

        result = usb_manager_open_device(manager, &devices[found], &device);
        if (result != THINGINO_SUCCESS) {
            free(devices);
            return result;
        }

        if (force_cpu) {
            device->info.variant = string_to_processor_variant(force_cpu);
        }
    }

    free(devices);

    LOG_INFO("\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("FIRMWARE READ\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("\n");

    const platform_profile_t *profile = platform_get_profile(device->info.variant);
    extern thingino_error_t protocol_read_flash_id(usb_device_t *, uint32_t *);
    const spi_nor_chip_t *flash_chip = NULL;

    /* Manual chip override */
    if (flash_chip_name) {
        flash_chip = spi_nor_find_by_name(flash_chip_name);
        if (!flash_chip) {
            LOG_INFO("[ERROR] Unknown flash chip: %s\n", flash_chip_name);
            usb_device_close(device);
            free(device);
            return THINGINO_ERROR_PROTOCOL;
        }
        LOG_INFO("  Flash chip (manual): %s (JEDEC 0x%06X, %u MB)\n", flash_chip->name, flash_chip->jedec_id,
                 flash_chip->size / (1024 * 1024));
    }

    /* Step 1: Send partition marker (ILOP) */
    LOG_INFO("Sending partition marker...\n");
    result = flash_partition_marker_send(device);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("[ERROR] Partition marker failed: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    /* Read ack after marker */
    {
        uint8_t ack_buf[4] = {0};
        int ack_len = 0;
        usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, NULL, 4, ack_buf, &ack_len);
    }

    /* Step 2: Auto-detect flash chip via JEDEC ID */
    if (!flash_chip) {
        uint32_t jedec_id = 0;
        if (protocol_read_flash_id(device, &jedec_id) == THINGINO_SUCCESS) {
            flash_chip = spi_nor_find_by_id(jedec_id);
            if (flash_chip) {
                LOG_INFO("  Flash chip (detected): %s (JEDEC 0x%06X, %u MB)\n", flash_chip->name, flash_chip->jedec_id,
                         flash_chip->size / (1024 * 1024));
            } else {
                LOG_INFO("  JEDEC 0x%06X not in database\n", jedec_id);
            }
        }
    }

    if (!flash_chip) {
        LOG_INFO("[ERROR] Flash chip not detected. Use --flash-chip <name>\n");
        usb_device_close(device);
        free(device);
        return THINGINO_ERROR_PROTOCOL;
    }

    /* Step 3: Generate and send READ descriptor (no erase).
     * Format depends on platform:
     *   A1:        992 bytes (RDD prefix + shifted SFC)
     *   T40/T41:   984 bytes (RDD prefix + standard GBD)
     *   T31/xb1:   972 bytes (GBD only) */
    LOG_INFO("Sending read descriptor (%s, no-erase)...\n", profile->name);

    if (profile->crc_format == CRC_FMT_A1) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_A1];
        flash_descriptor_create_a1_read(flash_chip, desc);
        result = flash_descriptor_send_sized(device, desc, FLASH_DESCRIPTOR_SIZE_A1);
    } else if (profile->crc_format == CRC_FMT_VENDOR) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_XB2];
        flash_descriptor_create_xb2_read(flash_chip, desc);
        result = flash_descriptor_send_sized(device, desc, FLASH_DESCRIPTOR_SIZE_XB2);
    } else if (device->info.variant == VARIANT_T32) {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE_T32];
        flash_descriptor_create_t32_read(flash_chip, desc);
        result = flash_descriptor_send_sized(device, desc, FLASH_DESCRIPTOR_SIZE_T32);
    } else {
        uint8_t desc[FLASH_DESCRIPTOR_SIZE];
        flash_descriptor_create_read(flash_chip, desc);
        result = flash_descriptor_send(device, desc);
    }

    if (result != THINGINO_SUCCESS) {
        LOG_INFO("[ERROR] Failed to send read descriptor: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    platform_sleep_ms(500);

    /* Step 4: VR_FW_HANDSHAKE (0x11) - init read mode */
    LOG_INFO("Initializing read handshake...\n");
    result = firmware_handshake_init(device);
    if (result != THINGINO_SUCCESS) {
        LOG_INFO("[ERROR] Handshake init failed: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    /* Step 5: Read firmware in 1MB chunks */
    uint32_t read_size = flash_chip->size;
    LOG_INFO("Reading %u bytes (%.2f MB) from flash...\n", read_size, (float)read_size / (1024 * 1024));

    uint8_t *firmware_data = NULL;
    uint32_t firmware_size = 0;
    result = firmware_read_full(device, read_size, &firmware_data, &firmware_size);

    if (result != THINGINO_SUCCESS) {
        LOG_INFO("Failed to read firmware: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    LOG_INFO("Successfully read %u bytes from device\n", firmware_size);

    /* Step 6: Save to file */
    FILE *file = fopen(output_file, "wb");
    if (!file) {
        LOG_INFO("Failed to open output file: %s\n", output_file);
        free(firmware_data);
        usb_device_close(device);
        free(device);
        return THINGINO_ERROR_FILE_IO;
    }

    size_t bytes_written = fwrite(firmware_data, 1, firmware_size, file);
    fclose(file);
    free(firmware_data);

    if (bytes_written != (size_t)firmware_size) {
        LOG_INFO("Warning: only %zu of %u bytes written to file\n", bytes_written, firmware_size);
    } else {
        LOG_INFO("\n");
        LOG_INFO("================================================================================\n");
        LOG_INFO("FIRMWARE READ COMPLETE\n");
        LOG_INFO("================================================================================\n");
        LOG_INFO("  Output: %s (%.2f MB)\n", output_file, (float)firmware_size / (1024 * 1024));
        LOG_INFO("\n");
    }

    usb_device_close(device);
    free(device);
    return THINGINO_SUCCESS;
}

/* ========================================================================== */
/* Write firmware from file to device                                         */
/* ========================================================================== */

thingino_error_t cloner_op_write_firmware(usb_manager_t *manager, int device_index, const char *firmware_file,
                                          const char *force_cpu, const char *flash_chip_name, bool no_erase,
                                          bool reboot_after, bool do_bootstrap, bool verbose, bool skip_ddr,
                                          const char *config_file, const char *spl_file, const char *uboot_file,
                                          const char *firmware_dir, uint32_t chunk_size) {
    if (!manager || !firmware_file) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    LOG_INFO("\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("FIRMWARE WRITE\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("\n");

    /* List devices */
    device_info_t *devices = NULL;
    int device_count = 0;
    thingino_error_t result = usb_manager_find_devices(manager, &devices, &device_count);
    if (result != THINGINO_SUCCESS) {
        LOG_ERROR("Error listing devices: %s\n", thingino_error_to_string(result));
        return result;
    }

    if (device_index >= device_count) {
        LOG_ERROR("Device index %d out of range (0-%d)\n", device_index, device_count - 1);
        free(devices);
        return THINGINO_ERROR_DEVICE_NOT_FOUND;
    }

    /* Open device */
    usb_device_t *device = NULL;
    result = usb_manager_open_device(manager, &devices[device_index], &device);
    if (result != THINGINO_SUCCESS) {
        LOG_ERROR("Error opening device: %s\n", thingino_error_to_string(result));
        free(devices);
        return result;
    }

    LOG_INFO("Target Device:\n");
    LOG_INFO("  Index: %d\n", device_index);
    LOG_INFO("  Bus: %03d Address: %03d\n", devices[device_index].bus, devices[device_index].address);
    LOG_INFO("  Variant: %s\n", processor_variant_to_string(devices[device_index].variant));
    LOG_INFO("  Stage: %s\n", device_stage_to_string(devices[device_index].stage));
    LOG_INFO("\n");

    /* Apply CPU variant override, or auto-detect if in bootrom */
    if (force_cpu) {
        processor_variant_t forced_variant = string_to_processor_variant(force_cpu);
        const char *round_trip = processor_variant_to_string(forced_variant);
        if (strcasecmp(round_trip, force_cpu) == 0) {
            LOG_INFO("Forcing CPU variant to: %s (was: %s)\n", force_cpu,
                     processor_variant_to_string(device->info.variant));
            device->info.variant = forced_variant;
        } else {
            LOG_WARN("Unknown CPU variant '%s', ignoring\n", force_cpu);
        }
    } else if (devices[device_index].stage == STAGE_BOOTROM) {
        LOG_INFO("Auto-detecting SoC...\n");
        processor_variant_t detected = VARIANT_T31X;
        if (protocol_detect_soc(device, &detected) == THINGINO_SUCCESS) {
            device->info.variant = detected;
            g_last_detected_variant = processor_variant_to_string(detected);
        } else {
            LOG_WARN("SoC auto-detect failed, using CPU magic fallback\n");
        }
    }

    /* Check if device needs bootstrap (skip if caller already bootstrapped) */
    if (devices[device_index].stage == STAGE_BOOTROM && !do_bootstrap) {
        LOG_INFO("Device is in bootrom stage. Bootstrapping to firmware stage first...\n\n");

        bootstrap_config_t bootstrap_config = {.skip_ddr = skip_ddr,
                                               .config_file = config_file,
                                               .spl_file = spl_file,
                                               .uboot_file = uboot_file,
                                               .firmware_dir = firmware_dir,
                                               .sdram_address = 0x80000000, /* Default SDRAM address */
                                               .timeout = 5000,
                                               .verbose = verbose};

        result = bootstrap_device(device, &bootstrap_config);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Bootstrap failed: %s\n", thingino_error_to_string(result));
            usb_device_close(device);
            free(device);
            free(devices);
            return result;
        }

        LOG_INFO("\nBootstrap complete. Device should now be in firmware stage.\n");
        LOG_INFO("Waiting for device to stabilize...\n\n");
        platform_sleep_ms(2000);

        /* Close and reopen device to get fresh connection */
        usb_device_close(device);
        free(device);

        /* Re-scan for device in firmware stage */
        free(devices);
        devices = NULL;
        result = usb_manager_find_devices(manager, &devices, &device_count);
        if (result != THINGINO_SUCCESS || device_count == 0) {
            LOG_ERROR("Device not found after bootstrap\n");
            if (devices)
                free(devices);
            return THINGINO_ERROR_DEVICE_NOT_FOUND;
        }

        /* Find the device again (it may have re-enumerated) */
        int found_index = -1;
        for (int i = 0; i < device_count; i++) {
            if (devices[i].stage == STAGE_FIRMWARE) {
                found_index = i;
                break;
            }
        }

        if (found_index < 0) {
            LOG_ERROR("Device not in firmware stage after bootstrap\n");
            free(devices);
            return THINGINO_ERROR_PROTOCOL;
        }

        /* Reopen device */
        result = usb_manager_open_device(manager, &devices[found_index], &device);
        if (result != THINGINO_SUCCESS) {
            LOG_ERROR("Failed to reopen device: %s\n", thingino_error_to_string(result));
            free(devices);
            return result;
        }

        LOG_INFO("Device reopened in firmware stage.\n\n");

        /* Carry forward the variant detected during bootstrap auto-detect.
         * In firmware stage, T40/T41 both report "X2580" which maps to t31x.
         * The real variant was determined in bootrom stage via SoC ID registers. */
        if (!force_cpu && g_last_detected_variant) {
            processor_variant_t restored = string_to_processor_variant(g_last_detected_variant);
            if (restored != device->info.variant) {
                LOG_INFO("Restoring detected variant: %s (was: %s)\n",
                         g_last_detected_variant, processor_variant_to_string(device->info.variant));
                device->info.variant = restored;
            }
        }
    }

    free(devices);

    /* Apply CPU variant override if specified */
    if (force_cpu) {
        processor_variant_t forced_variant = string_to_processor_variant(force_cpu);
        /* Validate: round-trip the parsed variant back to string and compare.
         * string_to_processor_variant returns T31X for unknown strings, so
         * we check the round-trip matches to distinguish real T31X from unknown. */
        const char *round_trip = processor_variant_to_string(forced_variant);
        if (strcasecmp(round_trip, force_cpu) == 0) {
            LOG_INFO("Forcing CPU variant to: %s (was: %s)\n", force_cpu,
                     processor_variant_to_string(device->info.variant));
            device->info.variant = forced_variant;
        } else {
            LOG_WARN("Unknown CPU variant '%s', ignoring\n", force_cpu);
        }
    }

    /* Prepare burner protocol in firmware stage: send partition marker,
     * flash descriptor, and VR_INIT handshake. Dispatch via platform profile. */
    const platform_profile_t *fw_profile = platform_get_profile(device->info.variant);

    if (device->info.stage == STAGE_FIRMWARE) {

        thingino_error_t prep_result = THINGINO_SUCCESS;

        if (no_erase)
            LOG_INFO("Erase disabled (--no-erase)\n");
        LOG_INFO("Preparing flash descriptor...\n");

        extern thingino_error_t protocol_read_flash_id(usb_device_t *, uint32_t *);
        const spi_nor_chip_t *flash_chip = NULL;

        if (flash_chip_name) {
            /* Manual override */
            flash_chip = spi_nor_find_by_name(flash_chip_name);
            if (!flash_chip) {
                LOG_INFO("[ERROR] Unknown flash chip: %s\n", flash_chip_name);
                usb_device_close(device);
                free(device);
                return THINGINO_ERROR_PROTOCOL;
            }
            LOG_INFO("  Flash chip (manual): %s (JEDEC 0x%06X)\n", flash_chip->name, flash_chip->jedec_id);
        }

        switch (fw_profile->descriptor_mode) {
        case DESC_RAW_BULK_THEN_SEND: {
            /* T20: send descriptor directly. No marker/detect flow.
             * However, we can still auto-detect by sending the partition marker first
             * (which initializes the SFC), reading the JEDEC ID, then building the
             * correct descriptor. */
            if (!flash_chip) {
                LOG_INFO("  Flash chip not specified, attempting auto-detect...\n");
                /* Send partition marker to initialize SFC */
                prep_result = flash_partition_marker_send(device);
                if (prep_result != THINGINO_SUCCESS) {
                    LOG_INFO("[ERROR] Partition marker failed: %s\n", thingino_error_to_string(prep_result));
                    break;
                }
                /* Now try to read the actual JEDEC ID */
                uint32_t jedec_id = 0;
                if (protocol_read_flash_id(device, &jedec_id) == THINGINO_SUCCESS) {
                    flash_chip = spi_nor_find_by_id(jedec_id);
                    if (flash_chip) {
                        LOG_INFO("  Flash chip (detected): %s (JEDEC 0x%06X, %u MB)\n", flash_chip->name,
                                 flash_chip->jedec_id, flash_chip->size / (1024 * 1024));
                    } else {
                        LOG_INFO("  JEDEC 0x%06X not in database\n", jedec_id);
                        LOG_INFO("[ERROR] Flash chip not detected. Use --flash-chip <name>\n");
                        prep_result = THINGINO_ERROR_PROTOCOL;
                        break;
                    }
                } else {
                    LOG_INFO("[ERROR] Flash chip auto-detect failed. Use --flash-chip <name>\n");
                    prep_result = THINGINO_ERROR_PROTOCOL;
                    break;
                }
            }
            /* Send descriptor with detected or manually-specified chip */
            uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE];
            flash_descriptor_create(flash_chip, flash_descriptor);
            prep_result = flash_descriptor_send(device, flash_descriptor);
            break;
        }

        case DESC_MARKER_THEN_SEND: {
            /* Vendor flow (from USB capture):
             * 1. Send partition marker (172 bytes) to init SFC
             * 2. Read ack
             * 3. Read JEDEC ID via VR_FW_READ_STATUS4
             * 4. Generate and send full descriptor (972 bytes) with correct chip
             */
            prep_result = flash_partition_marker_send(device);
            if (prep_result != THINGINO_SUCCESS)
                break;

            /* Read ack after marker */
            {
                uint8_t ack_buf[4] = {0};
                int ack_len = 0;
                usb_device_vendor_request(device, REQUEST_TYPE_VENDOR, VR_FW_READ, 0, 0, NULL, 4, ack_buf, &ack_len);
            }

            /* Auto-detect flash chip via JEDEC ID */
            if (!flash_chip) {
                uint32_t jedec_id = 0;
                if (protocol_read_flash_id(device, &jedec_id) == THINGINO_SUCCESS) {
                    flash_chip = spi_nor_find_by_id(jedec_id);
                    if (flash_chip) {
                        LOG_INFO("  Flash chip (detected): %s (JEDEC 0x%06X)\n", flash_chip->name,
                                 flash_chip->jedec_id);
                    } else {
                        LOG_INFO("  JEDEC 0x%06X not in database, using default\n", jedec_id);
                    }
                }
            }

            if (!flash_chip) {
                LOG_INFO("[ERROR] Flash chip not detected. Use --flash-chip <name>\n");
                prep_result = THINGINO_ERROR_PROTOCOL;
                break;
            }

            /* Generate and send descriptor. Format depends on platform:
             * A1:        992 bytes (RDD 0x84 prefix + shifted SFC layout)
             * T40/T41:   984 bytes (RDD 0xC9 prefix + standard GBD body)
             * T31/xb1:   972 bytes (GBD only, no RDD prefix) */
            if (fw_profile->crc_format == CRC_FMT_A1) {
                /* A1: unique 992-byte format with SFC freq field */
                uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE_A1];
                flash_descriptor_create_a1(flash_chip, flash_descriptor);
                if (no_erase)
                    flash_descriptor[12 + 0xD0 + 0x14] = 0x00;
                prep_result = flash_descriptor_send_sized(device, flash_descriptor, FLASH_DESCRIPTOR_SIZE_A1);
            } else if (fw_profile->skip_set_data_addr) {
                /* xburst2 (T40/T41): 984-byte descriptor */
                uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE_XB2];
                flash_descriptor_create_xb2(flash_chip, flash_descriptor);
                if (no_erase)
                    flash_descriptor[12 + 0xC8 + 0x14] = 0x00;
                prep_result = flash_descriptor_send_sized(device, flash_descriptor, FLASH_DESCRIPTOR_SIZE_XB2);
            } else if (device->info.variant == VARIANT_T32) {
                /* T32: 976-byte descriptor with shifted SFC layout */
                uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE_T32];
                flash_descriptor_create_t32(flash_chip, flash_descriptor);
                if (no_erase)
                    flash_descriptor[0xC8 + 0x14] = 0x00;
                prep_result = flash_descriptor_send_sized(device, flash_descriptor, FLASH_DESCRIPTOR_SIZE_T32);
            } else {
                uint8_t flash_descriptor[FLASH_DESCRIPTOR_SIZE];
                flash_descriptor_create(flash_chip, flash_descriptor);
                if (no_erase)
                    flash_descriptor[0xC8 + 0x14] = 0x00;
                prep_result = flash_descriptor_send(device, flash_descriptor);
            }
            if (prep_result != THINGINO_SUCCESS)
                break;

            /* Erase wait is handled by writer.c (5s delay for A1) */
            break;
        }

        default:
            break;
        }

        if (prep_result != THINGINO_SUCCESS) {
            LOG_INFO("[ERROR] Failed to send flash descriptor: %s\n", thingino_error_to_string(prep_result));
            usb_device_close(device);
            free(device);
            return prep_result;
        }

        platform_sleep_ms(500);

        /* VR_INIT - triggers SFC probe + erase */
        prep_result = firmware_handshake_init(device);
        if (prep_result != THINGINO_SUCCESS) {
            LOG_INFO("[ERROR] Failed to initialize firmware handshake: %s\n", thingino_error_to_string(prep_result));
            usb_device_close(device);
            free(device);
            return prep_result;
        }
    }

    /* Write firmware */
    LOG_INFO("Writing firmware to device...\n");
    LOG_INFO("  Source file: %s\n", firmware_file);
    LOG_INFO("\n");

    result =
        write_firmware_to_device(device, firmware_file, NULL, no_erase, fw_profile->use_a1_handshake, chunk_size);
    if (result != THINGINO_SUCCESS) {
        LOG_ERROR("Firmware write failed: %s\n", thingino_error_to_string(result));
        usb_device_close(device);
        free(device);
        return result;
    }

    LOG_INFO("\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("FIRMWARE WRITE COMPLETE\n");
    LOG_INFO("================================================================================\n");
    LOG_INFO("\n");

    if (reboot_after) {
        LOG_INFO("Rebooting device...\n");
        int resp_len = 0;
        usb_device_vendor_request(device, REQUEST_TYPE_OUT, VR_REBOOT, 0, 0, NULL, 0, NULL, &resp_len);
    }

    usb_device_close(device);
    free(device);
    return THINGINO_SUCCESS;
}
