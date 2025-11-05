#include "thingino.h"

// ============================================================================
// DDR CONFIGURATION PARSER IMPLEMENTATION
// ============================================================================

// For now, we'll use a pre-extracted binary that we know works
// This mirrors the approach in the Go implementation
static uint8_t* extracted_ddr_binary = NULL;
static size_t extracted_ddr_size = 0;
static bool init_once = false;

thingino_error_t load_extracted_binary(void) {
    if (init_once) {
        return THINGINO_SUCCESS;
    }
    
    // Try to load from references directory
    const char* paths[] = {
        "../references/ddr_extracted.bin",
        "../../references/ddr_extracted.bin",
        "references/ddr_extracted.bin",
        NULL
    };
    
    for (int i = 0; paths[i]; i++) {
        FILE* file = fopen(paths[i], "rb");
        if (file) {
            // Get file size
            if (fseek(file, 0, SEEK_END) != 0) {
                fclose(file);
                continue;
            }
            
            long file_size = ftell(file);
            if (file_size < 0) {
                fclose(file);
                continue;
            }
            
            if (fseek(file, 0, SEEK_SET) != 0) {
                fclose(file);
                continue;
            }
            
            // Allocate and read
            extracted_ddr_binary = (uint8_t*)malloc(file_size);
            if (!extracted_ddr_binary) {
                fclose(file);
                return THINGINO_ERROR_MEMORY;
            }
            
            size_t bytes_read = fread(extracted_ddr_binary, 1, file_size, file);
            fclose(file);
            
            if (bytes_read == (size_t)file_size) {
                extracted_ddr_size = bytes_read;
                init_once = true;
                DEBUG_PRINT("Loaded DDR binary from: %s (%zu bytes)\n", paths[i], bytes_read);
                return THINGINO_SUCCESS;
            }
            
            free(extracted_ddr_binary);
            extracted_ddr_binary = NULL;
        }
    }
    
    // If we can't find the extracted binary, create a minimal valid one
    printf("Warning: Could not find extracted DDR binary, creating minimal one\n");
    return create_minimal_ddr_binary();
}

thingino_error_t create_minimal_ddr_binary(void) {
    // Create a minimal 324-byte DDR binary with "FIDB" signature
    extracted_ddr_size = 324;
    extracted_ddr_binary = (uint8_t*)malloc(extracted_ddr_size);
    if (!extracted_ddr_binary) {
        return THINGINO_ERROR_MEMORY;
    }
    
    // Initialize to zero
    memset(extracted_ddr_binary, 0, extracted_ddr_size);
    
    // Set "FIDB" signature
    extracted_ddr_binary[0] = 'F';
    extracted_ddr_binary[1] = 'I';
    extracted_ddr_binary[2] = 'D';
    extracted_ddr_binary[3] = 'B';
    
    // Set some basic DDR parameters (these would need to be properly calculated)
    // For now, using placeholder values that might work
    extracted_ddr_binary[4] = 0x01;  // Version
    extracted_ddr_binary[5] = 0x00;
    extracted_ddr_binary[6] = 0x00;
    extracted_ddr_binary[7] = 0x00;
    
    init_once = true;
    return THINGINO_SUCCESS;
}

thingino_error_t ddr_parse_config(const char* config_path, uint8_t** binary, size_t* size) {
    if (!binary || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    (void)config_path; // Suppress unused parameter warning
    
    thingino_error_t result = load_extracted_binary();
    if (result != THINGINO_SUCCESS) {
        return result;
    }
    
    // Return a copy of the extracted binary
    *binary = (uint8_t*)malloc(extracted_ddr_size);
    if (!*binary) {
        return THINGINO_ERROR_MEMORY;
    }
    
    memcpy(*binary, extracted_ddr_binary, extracted_ddr_size);
    *size = extracted_ddr_size;
    
    return THINGINO_SUCCESS;
}

thingino_error_t ddr_parse_config_bytes(const char* config_text, uint8_t** binary, size_t* size) {
    if (!binary || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // Suppress unused parameter warning
    (void)config_text;
    
    // For now, ignore the config text and return the working binary
    // In a full implementation, we would parse the text config
    // and convert it to the binary format
    return ddr_parse_config(NULL, binary, size);
}

thingino_error_t ddr_validate_binary(const uint8_t* data, size_t size) {
    if (!data) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    if (size != 324) {
        return THINGINO_ERROR_PROTOCOL;
    }
    
    // Check for "FIDB" signature
    if (size >= 4) {
        if (data[0] != 'F' || data[1] != 'I' || 
            data[2] != 'D' || data[3] != 'B') {
            return THINGINO_ERROR_PROTOCOL;
        }
    }
    
    return THINGINO_SUCCESS;
}

thingino_error_t ddr_parse_text_to_binary(const char* config_text, uint8_t** binary, size_t* size) {
    if (!config_text || !binary || !size) {
        return THINGINO_ERROR_INVALID_PARAMETER;
    }
    
    // TODO: Implement proper DDR config text to binary conversion
    // The current extracted binary has a complex structure that needs reverse engineering
    // Our current simple algorithm doesn't match the vendor's format
    
    DEBUG_PRINT("TODO: Implement proper DDR config text to binary conversion\n");
    DEBUG_PRINT("Using extracted binary for now\n");
    
    return ddr_parse_config(NULL, binary, size);
}

void ddr_cleanup(void) {
    if (extracted_ddr_binary) {
        free(extracted_ddr_binary);
        extracted_ddr_binary = NULL;
        extracted_ddr_size = 0;
        init_once = false;
    }
}

// Helper function to print DDR binary info for debugging
void ddr_print_info(const uint8_t* data, size_t size) {
    if (!data || size < 4) {
        DEBUG_PRINT("Invalid DDR binary data\n");
        return;
    }
    
    DEBUG_PRINT("DDR Binary Info:\n");
    DEBUG_PRINT("  Size: %zu bytes\n", size);
    DEBUG_PRINT("  Signature: %.4s\n", data);
    
    if (size >= 8) {
        DEBUG_PRINT("  Version: %d.%d\n", data[4], data[5]);
    }
    
    if (size >= 16) {
        DEBUG_PRINT("  Header: ");
        for (int i = 0; i < 16 && i < (int)size; i++) {
            DEBUG_PRINT("%02x ", data[i]);
        }
        DEBUG_PRINT("\n");
    }
}