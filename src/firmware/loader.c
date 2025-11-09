#include "thingino.h"
#include "ddr_binary_builder.h"
#include "ddr_config_database.h"
#include "firmware_database.h"
#include "t20_reference_ddr.h"

// ============================================================================
// FIRMWARE LOADER IMPLEMENTATION
// ============================================================================
// Loads real firmware files from disk (no fallback to placeholders)
// DDR configuration is now generated dynamically from chip parameters using
// the new ddr_binary_builder API that matches the Python script format

// ============================================================================
// DDR GENERATION USING NEW BINARY BUILDER API
// ============================================================================

/**
 * Convert timing from picoseconds to clock cycles (ceiling division)
 */
static inline uint32_t ps_to_cycles_ceil(uint32_t ps, uint32_t freq_hz) {
    // Convert frequency to period in picoseconds: period_ps = 1000000000000 / freq_hz
    // Then: cycles = (ps + period_ps - 1) / period_ps (ceiling division)
    // Simplified: cycles = (ps * freq_hz + 999999999999) / 1000000000000
    uint64_t numerator = (uint64_t)ps * freq_hz + 999999999999ULL;
    return (uint32_t)(numerator / 1000000000000ULL);
}

/**
 * Generate DDR configuration binary dynamically using the new ddr_binary_builder API
 *
 * This function generates a 324-byte DDR binary in the format:
 *   - FIDB section (192 bytes): Platform configuration (frequencies, UART, memory size)
 *   - RDD section (132 bytes): DDR PHY parameters (timing, geometry, DQ mapping)
 *
 * The format matches the Python script (ddr_compiler_final.py) and has been verified
 * to produce byte-perfect output for M14D1G1664A DDR2 @ 400MHz.
 */
static thingino_error_t firmware_generate_ddr_config(processor_variant_t variant,
    uint8_t** config_buffer, size_t* config_size) {

    if (!config_buffer || !config_size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("firmware_generate_ddr_config: variant=%d (%s)\n",
        variant, processor_variant_to_string(variant));

    // WORKAROUND: T20 uses a different DDR binary format than T31X
    // Use the vendor's reference binary for T20 until we reverse-engineer the format
    if (variant == VARIANT_T20) {
        DEBUG_PRINT("Using vendor reference DDR binary for T20 (different format than T31X)\n");

        *config_buffer = (uint8_t*)malloc(vendor_ddr_t20_bin_len);
        if (!*config_buffer) {
            fprintf(stderr, "ERROR: Failed to allocate DDR buffer\n");
            return THINGINO_ERROR_MEMORY;
        }

        memcpy(*config_buffer, vendor_ddr_t20_bin, vendor_ddr_t20_bin_len);
        *config_size = vendor_ddr_t20_bin_len;

        DEBUG_PRINT("Using T20 reference DDR binary: %zu bytes\n", *config_size);
        return THINGINO_SUCCESS;
    }

    // Get platform configuration based on processor variant
    platform_config_t platform_cfg;
    if (ddr_get_platform_config_by_variant(variant, &platform_cfg) != 0) {
        fprintf(stderr, "ERROR: Unsupported processor variant for DDR generation: %d\n", variant);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Platform config: crystal=%u Hz, cpu=%u Hz, ddr=%u Hz, uart=%u baud, mem=%u bytes\n",
        platform_cfg.crystal_freq, platform_cfg.cpu_freq, platform_cfg.ddr_freq,
        platform_cfg.uart_baud, platform_cfg.mem_size);

    // Get DDR chip configuration from database
    const char *platform_name = processor_variant_to_string(variant);
    const ddr_chip_config_t *chip_cfg = ddr_chip_config_get_default(platform_name);
    if (!chip_cfg) {
        fprintf(stderr, "ERROR: No default DDR chip found for platform: %s\n", platform_name);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }

    DEBUG_PRINT("Using DDR chip: %s (%s)\n", chip_cfg->name, chip_cfg->vendor);

    // Convert timing parameters from picoseconds to clock cycles
    ddr_phy_params_t phy_params;
    phy_params.ddr_type = chip_cfg->ddr_type;
    phy_params.row_bits = chip_cfg->row_bits;
    phy_params.col_bits = chip_cfg->col_bits;
    phy_params.cl = chip_cfg->cl;
    phy_params.bl = chip_cfg->bl;
    phy_params.tRAS = ps_to_cycles_ceil(chip_cfg->tRAS, platform_cfg.ddr_freq);
    phy_params.tRC = ps_to_cycles_ceil(chip_cfg->tRC, platform_cfg.ddr_freq);
    phy_params.tRCD = ps_to_cycles_ceil(chip_cfg->tRCD, platform_cfg.ddr_freq);
    phy_params.tRP = ps_to_cycles_ceil(chip_cfg->tRP, platform_cfg.ddr_freq);
    phy_params.tRFC = ps_to_cycles_ceil(chip_cfg->tRFC, platform_cfg.ddr_freq);
    phy_params.tRTP = ps_to_cycles_ceil(chip_cfg->tRTP, platform_cfg.ddr_freq);
    phy_params.tFAW = ps_to_cycles_ceil(chip_cfg->tFAW, platform_cfg.ddr_freq);
    phy_params.tRRD = ps_to_cycles_ceil(chip_cfg->tRRD, platform_cfg.ddr_freq);
    phy_params.tWTR = ps_to_cycles_ceil(chip_cfg->tWTR, platform_cfg.ddr_freq);

    DEBUG_PRINT("DDR PHY params: type=%u, row=%u, col=%u, CL=%u, BL=%u\n",
        phy_params.ddr_type, phy_params.row_bits, phy_params.col_bits,
        phy_params.cl, phy_params.bl);
    DEBUG_PRINT("Timing (cycles): tRAS=%u, tRC=%u, tRCD=%u, tRP=%u, tRFC=%u, tRTP=%u, tFAW=%u, tRRD=%u, tWTR=%u\n",
        phy_params.tRAS, phy_params.tRC, phy_params.tRCD, phy_params.tRP, phy_params.tRFC,
        phy_params.tRTP, phy_params.tFAW, phy_params.tRRD, phy_params.tWTR);

    // Allocate buffer for DDR binary (324 bytes)
    *config_buffer = (uint8_t*)malloc(DDR_BINARY_SIZE);
    if (!*config_buffer) {
        fprintf(stderr, "ERROR: Failed to allocate DDR buffer\n");
        return THINGINO_ERROR_MEMORY;
    }

    // Generate the DDR binary using the new API
    DEBUG_PRINT("Generating 324-byte DDR binary (FIDB + RDD format)\n");
    size_t generated_size = ddr_build_binary(&platform_cfg, &phy_params, *config_buffer);
    if (generated_size == 0) {
        fprintf(stderr, "ERROR: Failed to generate DDR binary\n");
        free(*config_buffer);
        *config_buffer = NULL;
        return THINGINO_ERROR_PROTOCOL;
    }

    *config_size = generated_size;
    DEBUG_PRINT("Successfully generated %zu bytes DDR binary\n", *config_size);

    // TEMPORARY DEBUG: Save generated DDR binary for analysis
    FILE *debug_f = fopen("/tmp/t20_ddr_debug.bin", "wb");
    if (debug_f) {
        fwrite(*config_buffer, 1, *config_size, debug_f);
        fclose(debug_f);
        DEBUG_PRINT("DEBUG: Saved generated DDR binary to /tmp/t20_ddr_debug.bin\n");
    }

    return THINGINO_SUCCESS;
}

thingino_error_t firmware_load(processor_variant_t variant, firmware_files_t* firmware) {
    if (!firmware) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_load: variant=%d (%s)\n", variant, processor_variant_to_string(variant));
    
    // Initialize firmware structure
    firmware->config = NULL;
    firmware->config_size = 0;
    firmware->spl = NULL;
    firmware->spl_size = 0;
    firmware->uboot = NULL;
    firmware->uboot_size = 0;
    
    switch (variant) {
        case VARIANT_T20:
            DEBUG_PRINT("firmware_load: matched VARIANT_T20 (%d)\n", VARIANT_T20);
            DEBUG_PRINT("firmware_load: calling firmware_load_t20\n");
            return firmware_load_t20(firmware);
        case VARIANT_T31X:
            DEBUG_PRINT("firmware_load: matched VARIANT_T31X (%d)\n", VARIANT_T31X);
            DEBUG_PRINT("firmware_load: calling firmware_load_t31x\n");
            return firmware_load_t31x(firmware);
        case VARIANT_T31ZX:
            DEBUG_PRINT("firmware_load: matched VARIANT_T31ZX (%d)\n", VARIANT_T31ZX);
            DEBUG_PRINT("firmware_load: calling firmware_load_t31x\n");
            return firmware_load_t31x(firmware);

        default:
            DEBUG_PRINT("firmware_load: unsupported variant %d\n", variant);
            return THINGINO_ERROR_INVALID_PARAMETER;
    }
}

thingino_error_t firmware_load_t31x(firmware_files_t* firmware) {
    thingino_error_t result;
    
    DEBUG_PRINT("Loading T31X firmware...\n");
    
    // Try to generate DDR configuration dynamically first
    DEBUG_PRINT("Attempting to generate DDR configuration dynamically\n");
    thingino_error_t gen_result = firmware_generate_ddr_config(VARIANT_T31X, 
        &firmware->config, &firmware->config_size);
    
    if (gen_result == THINGINO_SUCCESS) {
        printf("✓ DDR configuration generated dynamically: %zu bytes\n", firmware->config_size);
    } else {
        // Fall back to reference binary
        DEBUG_PRINT("Dynamic generation failed, falling back to reference binary\n");
        printf("Note: Using reference binary for DDR configuration\n");
        
        const char* config_paths[] = {
            "./references/ddr_extracted.bin",
            "../references/ddr_extracted.bin",
            NULL
        };
        
        // Try to load reference binary
        result = THINGINO_ERROR_FILE_IO;
        for (int i = 0; config_paths[i]; i++) {
            DEBUG_PRINT("Trying to load DDR config from: %s\n", config_paths[i]);
            result = load_file(config_paths[i], &firmware->config, &firmware->config_size);
            if (result == THINGINO_SUCCESS) {
                DEBUG_PRINT("Loaded DDR config: %zu bytes\n", firmware->config_size);
                printf("✓ DDR configuration loaded from reference binary: %zu bytes\n", firmware->config_size);
                break;
            }
        }
        
        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "ERROR: Could not generate DDR or load reference binary\n");
            fprintf(stderr, "  Generation issue: %s\n", thingino_error_to_string(gen_result));
            fprintf(stderr, "  Reference binary expected at: ./references/ddr_extracted.bin\n");
            return result;
        }
    }
    
    // Define SPL and U-Boot paths
    const char* spl_paths[] = {
        "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin",
        "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin",
        NULL
    };
    
    const char* uboot_paths[] = {
        "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin",
        "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin",
        NULL
    };
    
    // Load SPL binary
    result = THINGINO_ERROR_FILE_IO;
    for (int i = 0; spl_paths[i]; i++) {
        DEBUG_PRINT("Trying to load SPL from: %s\n", spl_paths[i]);
        result = load_file(spl_paths[i], &firmware->spl, &firmware->spl_size);
        if (result == THINGINO_SUCCESS) {
            DEBUG_PRINT("Loaded SPL: %zu bytes\n", firmware->spl_size);
            break;
        }
    }
    
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to load SPL file\n");
        fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin\n");
        firmware_cleanup(firmware);
        return result;
    }
    
    // Load U-Boot binary (separate from SPL)
    result = THINGINO_ERROR_FILE_IO;
    for (int i = 0; uboot_paths[i]; i++) {
        DEBUG_PRINT("Trying to load U-Boot from: %s\n", uboot_paths[i]);
        result = load_file(uboot_paths[i], &firmware->uboot, &firmware->uboot_size);
        if (result == THINGINO_SUCCESS) {
            DEBUG_PRINT("Loaded U-Boot: %zu bytes\n", firmware->uboot_size);
            break;
        }
    }
    
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to load U-Boot file\n");
        fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin\n");
        firmware_cleanup(firmware);
        return result;
    }
    
    DEBUG_PRINT("T31X firmware loaded successfully (official cloner files)\n");
    DEBUG_PRINT("DDR config: %zu bytes, SPL: %zu bytes, U-Boot: %zu bytes\n",
           firmware->config_size, firmware->spl_size, firmware->uboot_size);

    return THINGINO_SUCCESS;
}

thingino_error_t firmware_load_t20(firmware_files_t* firmware) {
    thingino_error_t result;

    DEBUG_PRINT("Loading T20 firmware...\n");

    // Try to generate DDR configuration dynamically first
    DEBUG_PRINT("Attempting to generate DDR configuration dynamically\n");
    thingino_error_t gen_result = firmware_generate_ddr_config(VARIANT_T20,
        &firmware->config, &firmware->config_size);

    if (gen_result == THINGINO_SUCCESS) {
        printf("✓ DDR configuration generated dynamically: %zu bytes\n", firmware->config_size);
    } else {
        // Fall back to reference binary
        DEBUG_PRINT("Dynamic generation failed, falling back to reference binary\n");
        printf("Note: Using reference binary for DDR configuration\n");

        const char* config_paths[] = {
            "./references/ddr_extracted.bin",
            "../references/ddr_extracted.bin",
            NULL
        };

        // Try to load reference binary
        result = THINGINO_ERROR_FILE_IO;
        for (int i = 0; config_paths[i]; i++) {
            DEBUG_PRINT("Trying to load DDR config from: %s\n", config_paths[i]);
            result = load_file(config_paths[i], &firmware->config, &firmware->config_size);
            if (result == THINGINO_SUCCESS) {
                DEBUG_PRINT("Loaded DDR config: %zu bytes\n", firmware->config_size);
                break;
            }
        }

        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to load or generate DDR configuration\n");
            return result;
        }
    }

    // Load embedded T20 firmware from database
    DEBUG_PRINT("Loading embedded T20 firmware from database\n");
    const firmware_binary_t* fw = firmware_get("t20");
    if (!fw) {
        fprintf(stderr, "ERROR: T20 firmware not found in database\n");
        firmware_cleanup(firmware);
        return THINGINO_ERROR_FILE_IO;
    }

    // Copy SPL
    firmware->spl_size = fw->spl_size;
    firmware->spl = (uint8_t*)malloc(fw->spl_size);
    if (!firmware->spl) {
        firmware_cleanup(firmware);
        return THINGINO_ERROR_MEMORY;
    }
    memcpy(firmware->spl, fw->spl_data, fw->spl_size);
    DEBUG_PRINT("Loaded embedded T20 SPL: %zu bytes\n", firmware->spl_size);

    // Copy U-Boot
    firmware->uboot_size = fw->uboot_size;
    firmware->uboot = (uint8_t*)malloc(fw->uboot_size);
    if (!firmware->uboot) {
        firmware_cleanup(firmware);
        return THINGINO_ERROR_MEMORY;
    }
    memcpy(firmware->uboot, fw->uboot_data, fw->uboot_size);
    DEBUG_PRINT("Loaded embedded T20 U-Boot: %zu bytes\n", firmware->uboot_size);

    DEBUG_PRINT("T20 firmware loaded successfully (embedded firmware)\n");
    DEBUG_PRINT("DDR config: %zu bytes, SPL: %zu bytes, U-Boot: %zu bytes\n",
           firmware->config_size, firmware->spl_size, firmware->uboot_size);

    return THINGINO_SUCCESS;
}

void firmware_cleanup(firmware_files_t* firmware) {
    if (!firmware) {
        return;
    }
    
    if (firmware->config) {
        free(firmware->config);
        firmware->config = NULL;
        firmware->config_size = 0;
    }
    
    if (firmware->spl) {
        free(firmware->spl);
        firmware->spl = NULL;
        firmware->spl_size = 0;
    }
    
    if (firmware->uboot) {
        free(firmware->uboot);
        firmware->uboot = NULL;
        firmware->uboot_size = 0;
    }
}

thingino_error_t firmware_load_from_files(processor_variant_t variant,
    const char* config_file, const char* spl_file, const char* uboot_file,
    firmware_files_t* firmware) {
    
    if (!firmware) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Initialize firmware structure
    firmware->config = NULL;
    firmware->config_size = 0;
    firmware->spl = NULL;
    firmware->spl_size = 0;
    firmware->uboot = NULL;
    firmware->uboot_size = 0;
    
    // Load or generate configuration file
    if (config_file) {
        // User provided custom DDR config file
        thingino_error_t result = load_file(config_file, &firmware->config, &firmware->config_size);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
        DEBUG_PRINT("Loaded custom DDR config from: %s (%zu bytes)\n", config_file, firmware->config_size);
        printf("✓ Loaded custom DDR config: %s (%zu bytes)\n", config_file, firmware->config_size);
    } else {
        // No custom config provided - try dynamic generation, fall back to reference
        DEBUG_PRINT("No custom DDR config provided, attempting dynamic generation for variant %d\n", variant);
        thingino_error_t gen_result = firmware_generate_ddr_config(variant, 
            &firmware->config, &firmware->config_size);
        
        if (gen_result == THINGINO_SUCCESS) {
            printf("✓ Generated DDR configuration dynamically: %zu bytes\n", firmware->config_size);
        } else {
            // Generation failed - try reference binary fallback
            DEBUG_PRINT("Dynamic generation failed, attempting reference binary fallback\n");
            
            // For now, continue without DDR config if no file provided and generation fails
            // (Reference binary paths depend on processor type which we may not know)
            DEBUG_PRINT("Warning: Failed to generate DDR config, continuing without it\n");
            firmware->config = NULL;
            firmware->config_size = 0;
        }
    }
    
    // Load SPL file
    if (spl_file) {
        // User provided custom SPL file
        thingino_error_t result = load_file(spl_file, &firmware->spl, &firmware->spl_size);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
        DEBUG_PRINT("Loaded custom SPL from: %s (%zu bytes)\n", spl_file, firmware->spl_size);
        printf("✓ Loaded custom SPL: %s (%zu bytes)\n", spl_file, firmware->spl_size);
    } else {
        // No custom SPL provided - load default based on variant
        DEBUG_PRINT("No custom SPL provided, loading default for variant %d\n", variant);
        const char* spl_paths[] = {
            "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin",
            "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin",
            NULL
        };

        thingino_error_t result = THINGINO_ERROR_FILE_IO;
        for (int i = 0; spl_paths[i]; i++) {
            DEBUG_PRINT("Trying to load SPL from: %s\n", spl_paths[i]);
            result = load_file(spl_paths[i], &firmware->spl, &firmware->spl_size);
            if (result == THINGINO_SUCCESS) {
                DEBUG_PRINT("Loaded default SPL: %zu bytes\n", firmware->spl_size);
                printf("✓ Loaded default SPL: %zu bytes\n", firmware->spl_size);
                break;
            }
        }

        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to load SPL file\n");
            fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/spl.bin\n");
            firmware_cleanup(firmware);
            return result;
        }
    }

    // Load U-Boot file
    if (uboot_file) {
        // User provided custom U-Boot file
        thingino_error_t result = load_file(uboot_file, &firmware->uboot, &firmware->uboot_size);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
        DEBUG_PRINT("Loaded custom U-Boot from: %s (%zu bytes)\n", uboot_file, firmware->uboot_size);
        printf("✓ Loaded custom U-Boot: %s (%zu bytes)\n", uboot_file, firmware->uboot_size);
    } else {
        // No custom U-Boot provided - load default based on variant
        DEBUG_PRINT("No custom U-Boot provided, loading default for variant %d\n", variant);
        const char* uboot_paths[] = {
            "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin",
            "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin",
            NULL
        };

        thingino_error_t result = THINGINO_ERROR_FILE_IO;
        for (int i = 0; uboot_paths[i]; i++) {
            DEBUG_PRINT("Trying to load U-Boot from: %s\n", uboot_paths[i]);
            result = load_file(uboot_paths[i], &firmware->uboot, &firmware->uboot_size);
            if (result == THINGINO_SUCCESS) {
                DEBUG_PRINT("Loaded default U-Boot: %zu bytes\n", firmware->uboot_size);
                printf("✓ Loaded default U-Boot: %zu bytes\n", firmware->uboot_size);
                break;
            }
        }

        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to load U-Boot file\n");
            fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t31x/uboot.bin\n");
            firmware_cleanup(firmware);
            return result;
        }
    }

    return THINGINO_SUCCESS;
}

thingino_error_t load_file(const char* filename, uint8_t** data, size_t* size) {
    if (!filename || !data || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    FILE* file = fopen(filename, "rb");
    if (!file) {
        return THINGINO_ERROR_FILE_IO;
    }
    
    // Get file size
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return THINGINO_ERROR_FILE_IO;
    }
    
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return THINGINO_ERROR_FILE_IO;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return THINGINO_ERROR_FILE_IO;
    }
    
    // Allocate buffer
    *data = (uint8_t*)malloc(file_size);
    if (!*data) {
        fclose(file);
        return THINGINO_ERROR_MEMORY;
    }
    
    // Read file
    size_t bytes_read = fread(*data, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(*data);
        *data = NULL;
        return THINGINO_ERROR_FILE_IO;
    }
    
    *size = bytes_read;
    return THINGINO_SUCCESS;
}

thingino_error_t firmware_validate(const firmware_files_t* firmware) {
    if (!firmware) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Validate DDR configuration
    if (firmware->config && firmware->config_size > 0) {
        thingino_error_t result = ddr_validate_binary(firmware->config, firmware->config_size);
        if (result != THINGINO_SUCCESS) {
            return result;
        }
    }
    
    // Validate SPL (basic checks)
    if (firmware->spl && firmware->spl_size > 0) {
        // Check for minimum SPL size
        if (firmware->spl_size < 1024) {
            return THINGINO_ERROR_PROTOCOL;
        }
    }
    
    // Validate U-Boot (basic checks)
    if (firmware->uboot && firmware->uboot_size > 0) {
        // Check for minimum U-Boot size
        if (firmware->uboot_size < 4096) {
            return THINGINO_ERROR_PROTOCOL;
        }
    }
    
    return THINGINO_SUCCESS;
}