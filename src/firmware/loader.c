#include "thingino.h"

// ============================================================================
// FIRMWARE LOADER IMPLEMENTATION
// ============================================================================
// Loads real firmware files from disk (no fallback to placeholders)

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
            
        default:
            DEBUG_PRINT("firmware_load: unsupported variant %d\n", variant);
            return THINGINO_ERROR_INVALID_PARAMETER;
    }
}

thingino_error_t firmware_load_t31x(firmware_files_t* firmware) {
    DEBUG_PRINT("Loading T31X firmware from official cloner...\n");
    
    // Paths to DDR configuration (pre-processed binary)
    // TODO: Implement DDR config processing to generate this dynamically
    const char* config_paths[] = {
        "./references/ddr_extracted.bin",
        "../references/ddr_extracted.bin",
        NULL
    };
    
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
    
    // Load DDR config
    thingino_error_t result = THINGINO_ERROR_FILE_IO;
    for (int i = 0; config_paths[i]; i++) {
        DEBUG_PRINT("Trying to load DDR config from: %s\n", config_paths[i]);
        result = load_file(config_paths[i], &firmware->config, &firmware->config_size);
        if (result == THINGINO_SUCCESS) {
            DEBUG_PRINT("Loaded DDR config: %zu bytes\n", firmware->config_size);
            break;
        }
    }
    
    if (result != THINGINO_SUCCESS) {
        fprintf(stderr, "ERROR: Failed to load DDR configuration file\n");
        fprintf(stderr, "  Expected at: ./references/ddr_extracted.bin\n");
        fprintf(stderr, "  Note: Using pre-processed DDR binary (not raw config.cfg)\n");
        return result;
    }
    
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
    (void)variant; // Suppress unused parameter warning
    
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
    
    // Load configuration file
    if (config_file) {
        thingino_error_t result = load_file(config_file, &firmware->config, &firmware->config_size);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
    }
    
    // Load SPL file
    if (spl_file) {
        thingino_error_t result = load_file(spl_file, &firmware->spl, &firmware->spl_size);
        if (result != THINGINO_SUCCESS) {
            firmware_cleanup(firmware);
            return result;
        }
    }
    
    // Load U-Boot file
    if (uboot_file) {
        thingino_error_t result = load_file(uboot_file, &firmware->uboot, &firmware->uboot_size);
        if (result != THINGINO_SUCCESS) {
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