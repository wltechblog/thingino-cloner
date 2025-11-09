#include "thingino.h"
#include "ddr_param_builder.h"
#include "ddr_generator.h"
#include "ddr_types.h"

// ============================================================================
// FIRMWARE LOADER IMPLEMENTATION
// ============================================================================
// Loads real firmware files from disk (no fallback to placeholders)
// DDR configuration is now generated dynamically from chip parameters

// ============================================================================
// DDR GENERATION HELPER FUNCTIONS
// ============================================================================

/**
 * Convert processor variant to chip type for DDR generation
 */
static chip_type_t variant_to_chip_type(processor_variant_t variant) {
    switch (variant) {
        case VARIANT_T23:
            return CHIP_T23N;
        case VARIANT_T31:
            return CHIP_T31L;  // T31 (generic) maps to T31L
        case VARIANT_T31X:
        case VARIANT_T31ZX:
            return CHIP_T31X;
        case VARIANT_T41:
        case VARIANT_T41N:
            return CHIP_T41N;  // T41/T41N map to T41N
        default:
            // Default to T31X for compatibility with other T-series
            return CHIP_T31X;
    }
}

/**
 * Bridge function: convert ddr_chip_config_t to ddr_config_t
 */
static int bridge_chip_config_to_ddr_config(const ddr_chip_config_t* chip_cfg, ddr_config_t* ddr_cfg) {
    if (!chip_cfg || !ddr_cfg) {
        return -1;
    }
    
    memset(ddr_cfg, 0, sizeof(ddr_config_t));
    
    // Set DDR type
    // Map vendor ddr_type to internal enum
    switch (chip_cfg->ddr_type) {
        case 0: ddr_cfg->type = DDR_TYPE_DDR3; break;   // Vendor "0,DDR3"
        case 1: ddr_cfg->type = DDR_TYPE_DDR2; break;
        case 2: ddr_cfg->type = DDR_TYPE_LPDDR2; break;
        case 3: ddr_cfg->type = DDR_TYPE_LPDDR; break;
        case 4: ddr_cfg->type = DDR_TYPE_LPDDR3; break;
        default: ddr_cfg->type = DDR_TYPE_DDR2; break;
    }

    // Set frequency
    ddr_cfg->clock_mhz = chip_cfg->ddr_freq / 1000000;  // Convert Hz to MHz
    
    // Set timing parameters (convert from ps to ns by dividing by 1000)
    ddr_cfg->tWR = chip_cfg->tWR / 1000;
    ddr_cfg->tRL = chip_cfg->tRL / 1000;
    ddr_cfg->tRP = chip_cfg->tRP / 1000;
    ddr_cfg->tRCD = chip_cfg->tRCD / 1000;
    ddr_cfg->tRAS = chip_cfg->tRAS / 1000;
    ddr_cfg->tRC = chip_cfg->tRC / 1000;
    ddr_cfg->tRRD = chip_cfg->tRRD / 1000;
    ddr_cfg->tREFI = chip_cfg->tREFI / 1000;
    ddr_cfg->tCKE = chip_cfg->tCKE / 1000;
    ddr_cfg->tXP = chip_cfg->tXP / 1000;
    ddr_cfg->tRFC = chip_cfg->tRFC / 1000;
    
    // tWL (Write Latency) = CAS - 1 in cycles, convert to ns
    // CAS latency is 3 for all DDR2 chips, so tWL = 2 cycles
    // At clock_mhz MHz: 2 cycles * (1000ns / clock_mhz cycles) = 2000 / clock_mhz ns
    uint32_t cas_latency = 3;  // Fixed for DDR2
    uint32_t tWL_cycles = (cas_latency > 0) ? (cas_latency - 1) : 1;
    ddr_cfg->tWL = (tWL_cycles * 1000) / (chip_cfg->ddr_freq / 1000000);
    
    // tWTR (Write-to-Read delay) - Standard DDR2 value is 2 cycles minimum
    // For 400 MHz: 2 cycles = 5ns
    uint32_t clock_mhz = chip_cfg->ddr_freq / 1000000;
    uint32_t tWTR_cycles = 2;  // DDR2 minimum
    ddr_cfg->tWTR = (tWTR_cycles * 1000) / clock_mhz;
    
    // Set memory configuration
    ddr_cfg->banks = chip_cfg->banks;
    ddr_cfg->data_width = chip_cfg->dram_width == 8 ? 8 : 16;  // 8 or 16 bit
    ddr_cfg->total_size_bytes = 64 * 1024 * 1024;  // Assume 64MB for standard config
    
    return 0;
}

/**
 * Generate DDR configuration binary dynamically
 */
static thingino_error_t firmware_generate_ddr_config(processor_variant_t variant,
    uint8_t** config_buffer, size_t* config_size) {
    
    if (!config_buffer || !config_size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    DEBUG_PRINT("firmware_generate_ddr_config: variant=%d\n", variant);
    
    // Get chip type from processor variant
    chip_type_t chip_type = variant_to_chip_type(variant);
    
    // Get reference DDR configuration for the chip
    ddr_chip_config_t chip_cfg;
    if (ddr_get_chip_config(chip_type, &chip_cfg) != 0) {
        fprintf(stderr, "ERROR: Unsupported processor variant for DDR generation: %d\n", variant);
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Bridge to ddr_config_t structure
    ddr_config_t ddr_cfg;
    if (bridge_chip_config_to_ddr_config(&chip_cfg, &ddr_cfg) != 0) {
        fprintf(stderr, "ERROR: Failed to bridge DDR configuration\n");
        return THINGINO_ERROR_MEMORY;
    }
    
    // Allocate buffer for DDR binary (324 bytes)
    *config_buffer = (uint8_t*)malloc(324);
    if (!*config_buffer) {
        fprintf(stderr, "ERROR: Failed to allocate DDR buffer\n");
        return THINGINO_ERROR_MEMORY;
    }
    
    // Generate the DDR binary
    DEBUG_PRINT("firmware_generate_ddr_config: generating 324-byte DDR binary\n");
    if (ddr_generate_binary(&ddr_cfg, *config_buffer, 324) != 0) {
        fprintf(stderr, "ERROR: Failed to generate DDR binary\n");
        free(*config_buffer);
        *config_buffer = NULL;
        return THINGINO_ERROR_PROTOCOL;
    }
    
    *config_size = 324;
    DEBUG_PRINT("firmware_generate_ddr_config: Generated %zu bytes\n", *config_size);
    
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
        case VARIANT_T31X:
            DEBUG_PRINT("firmware_load: matched VARIANT_T31X (%d)\n", VARIANT_T31X);
            DEBUG_PRINT("firmware_load: calling firmware_load_t31x\n");
            return firmware_load_t31x(firmware);
        case VARIANT_T31ZX:
            DEBUG_PRINT("firmware_load: matched VARIANT_T31ZX (%d)\n", VARIANT_T31ZX);
            DEBUG_PRINT("firmware_load: calling firmware_load_t31x\n");
            return firmware_load_t31x(firmware);
        case VARIANT_T41:
            DEBUG_PRINT("firmware_load: matched VARIANT_T41 (%d)\n", VARIANT_T41);
            DEBUG_PRINT("firmware_load: calling firmware_load_t41\n");
            return firmware_load_t41(firmware);
        case VARIANT_T41N:
            DEBUG_PRINT("firmware_load: matched VARIANT_T41N (%d)\n", VARIANT_T41N);
            DEBUG_PRINT("firmware_load: calling firmware_load_t41\n");
            return firmware_load_t41(firmware);

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

thingino_error_t firmware_load_t41(firmware_files_t* firmware) {
    thingino_error_t result;

    DEBUG_PRINT("Loading T41 firmware...\n");

    // Try to generate DDR configuration dynamically first
    DEBUG_PRINT("Attempting to generate DDR configuration dynamically\n");
    thingino_error_t gen_result = firmware_generate_ddr_config(VARIANT_T41,
        &firmware->config, &firmware->config_size);

    if (gen_result == THINGINO_SUCCESS) {
        DEBUG_PRINT("Successfully generated DDR configuration: %zu bytes\n", firmware->config_size);
        printf("✓ Generated DDR configuration: %zu bytes\n", firmware->config_size);
    } else {
        DEBUG_PRINT("Failed to generate DDR configuration, trying reference binary\n");

        // Try to load reference DDR binary
        const char* ddr_paths[] = {
            "./references/ddr_extracted.bin",
            "../references/ddr_extracted.bin",
            "./ddr_extracted.bin",
            "../ddr_extracted.bin",
            NULL
        };

        result = THINGINO_ERROR_FILE_IO;
        for (int i = 0; ddr_paths[i]; i++) {
            DEBUG_PRINT("Trying to load DDR config from: %s\n", ddr_paths[i]);
            result = load_file(ddr_paths[i], &firmware->config, &firmware->config_size);
            if (result == THINGINO_SUCCESS) {
                DEBUG_PRINT("Loaded reference DDR config: %zu bytes\n", firmware->config_size);
                printf("Note: Using reference binary for DDR configuration\n");
                printf("✓ DDR configuration loaded from reference binary: %zu bytes\n", firmware->config_size);
                break;
            }
        }

        if (result != THINGINO_SUCCESS) {
            fprintf(stderr, "ERROR: Failed to generate or load DDR configuration\n");
            return result;
        }
    }

    // Define SPL and U-Boot paths for T41
    const char* spl_paths[] = {
        "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/spl.bin",
        "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/spl.bin",
        "./firmwares/t41/spl.bin",
        "../firmwares/t41/spl.bin",
        NULL
    };

    // Prefer secure U-Boot if available (matches vendor tool behavior)
    const char* uboot_paths[] = {
        "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/uboot_sec.bin",
        "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/uboot_sec.bin",
        "./firmwares/t41/uboot_sec.bin",
        "../firmwares/t41/uboot_sec.bin",
        // Fallback to non-secure U-Boot
        "./references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/uboot.bin",
        "../references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/uboot.bin",
        "./firmwares/t41/uboot.bin",
        "../firmwares/t41/uboot.bin",
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
        fprintf(stderr, "ERROR: Failed to load SPL file for T41\n");
        fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/spl.bin\n");
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
        fprintf(stderr, "ERROR: Failed to load U-Boot file for T41\n");
        fprintf(stderr, "  Expected at: ./references/cloner-2.5.43-ubuntu_thingino/firmwares/t41/uboot.bin\n");
        firmware_cleanup(firmware);
        return result;
    }

    DEBUG_PRINT("T41 firmware loaded successfully\n");
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

        // Determine variant directory name
        const char* variant_dir = "t31x";  // default
        if (variant == VARIANT_T41 || variant == VARIANT_T41N) variant_dir = "t41";
        else if (variant == VARIANT_T31X || variant == VARIANT_T31ZX) variant_dir = "t31x";
        else if (variant == VARIANT_T23) variant_dir = "t23";

        char spl_path1[256], spl_path2[256];
        snprintf(spl_path1, sizeof(spl_path1), "./references/cloner-2.5.43-ubuntu_thingino/firmwares/%s/spl.bin", variant_dir);
        snprintf(spl_path2, sizeof(spl_path2), "../references/cloner-2.5.43-ubuntu_thingino/firmwares/%s/spl.bin", variant_dir);

        const char* spl_paths[] = { spl_path1, spl_path2, NULL };

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
            fprintf(stderr, "  Expected at: %s\n", spl_path1);
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

        // Determine variant directory name
        const char* variant_dir = "t31x";  // default
        if (variant == VARIANT_T41 || variant == VARIANT_T41N) variant_dir = "t41";
        else if (variant == VARIANT_T31X || variant == VARIANT_T31ZX) variant_dir = "t31x";
        else if (variant == VARIANT_T23) variant_dir = "t23";

        char uboot_path1[256], uboot_path2[256];
        snprintf(uboot_path1, sizeof(uboot_path1), "./references/cloner-2.5.43-ubuntu_thingino/firmwares/%s/uboot.bin", variant_dir);
        snprintf(uboot_path2, sizeof(uboot_path2), "../references/cloner-2.5.43-ubuntu_thingino/firmwares/%s/uboot.bin", variant_dir);

        const char* uboot_paths[] = { uboot_path1, uboot_path2, NULL };

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
            fprintf(stderr, "  Expected at: %s\n", uboot_path1);
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