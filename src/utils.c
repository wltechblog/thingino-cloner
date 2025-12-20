#include "thingino.h"
#include <ctype.h>
#include <string.h>

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Forward declarations for functions that need to be implemented elsewhere
extern thingino_error_t bootstrap_device(usb_device_t* device, const bootstrap_config_t* config);

uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = CRC32_INITIAL;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}

const char* processor_variant_to_string(processor_variant_t variant) {
    switch (variant) {
        case VARIANT_T20:   return "t20";
        case VARIANT_T21:   return "t21";
        case VARIANT_T23:   return "t23";
        case VARIANT_T30:   return "t30";
        case VARIANT_T31:   return "t31";
        case VARIANT_T31X:  return "t31x";
        case VARIANT_T31ZX: return "t31zx";
        case VARIANT_A1:    return "a1";
        case VARIANT_T40:   return "t40";
        case VARIANT_T41:   return "t41";
        case VARIANT_X1000: return "x1000";
        case VARIANT_X1600: return "x1600";
        case VARIANT_X1700: return "x1700";
        case VARIANT_X2000: return "x2000";
        case VARIANT_X2100: return "x2100";
        case VARIANT_X2600: return "x2600";
        default:           return "unknown";
    }
}

processor_variant_t string_to_processor_variant(const char* str) {
    if (!str) return VARIANT_T31X;

    // Convert to lowercase for case-insensitive comparison
    char lower[32] = {0};
    for (int i = 0; str[i] && i < 31; i++) {
        lower[i] = tolower(str[i]);
    }

    if (strcmp(lower, "a1") == 0) return VARIANT_A1;
    if (strcmp(lower, "t20") == 0) return VARIANT_T20;
    if (strcmp(lower, "t21") == 0) return VARIANT_T21;
    if (strcmp(lower, "t23") == 0) return VARIANT_T23;
    if (strcmp(lower, "t30") == 0) return VARIANT_T30;
    if (strcmp(lower, "t31") == 0) return VARIANT_T31;
    if (strcmp(lower, "t31x") == 0) return VARIANT_T31X;
    if (strcmp(lower, "t31zx") == 0) return VARIANT_T31ZX;
    if (strcmp(lower, "t40") == 0) return VARIANT_T40;
    if (strcmp(lower, "t41") == 0) return VARIANT_T41;
    if (strcmp(lower, "x1000") == 0) return VARIANT_X1000;
    if (strcmp(lower, "x1600") == 0) return VARIANT_X1600;
    if (strcmp(lower, "x1700") == 0) return VARIANT_X1700;
    if (strcmp(lower, "x2000") == 0) return VARIANT_X2000;
    if (strcmp(lower, "x2100") == 0) return VARIANT_X2100;
    if (strcmp(lower, "x2600") == 0) return VARIANT_X2600;

    // Default to T31X if unknown
    return VARIANT_T31X;
}

const char* device_stage_to_string(device_stage_t stage) {
    switch (stage) {
        case STAGE_BOOTROM:  return "bootrom";
        case STAGE_FIRMWARE: return "firmware";
        default:             return "unknown";
    }
}

const char* thingino_error_to_string(thingino_error_t error) {
    switch (error) {
        case THINGINO_SUCCESS:              return "Success";
        case THINGINO_ERROR_INIT_FAILED:     return "Initialization failed";
        case THINGINO_ERROR_DEVICE_NOT_FOUND: return "Device not found";
        case THINGINO_ERROR_OPEN_FAILED:     return "Failed to open device";
        case THINGINO_ERROR_TRANSFER_FAILED:  return "Transfer failed";
        case THINGINO_ERROR_TIMEOUT:         return "Timeout";
        case THINGINO_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case THINGINO_ERROR_MEMORY:           return "Memory allocation failed";
        case THINGINO_ERROR_FILE_IO:         return "File I/O error";
        case THINGINO_ERROR_PROTOCOL:         return "Protocol error";
        case THINGINO_ERROR_TRANSFER_TIMEOUT: return "Transfer timeout";
        default:                             return "Unknown error";
    }
}

processor_variant_t detect_variant_from_magic(const char* magic) {
    if (!magic) {
        return VARIANT_T31X;
    }
    
    DEBUG_PRINT("detect_variant_from_magic: input='%s' (length=%zu)\n", magic, magic ? strlen(magic) : 0);
    
    // Special-case XBurst2/X2580 boards: CPU magic "X2580" indicates an
    // XBurst2 platform that is T41N-based (DDR3 W631GU6NG @ 400 MHz).
    if (strstr(magic, "X2580") || strstr(magic, "x2580")) {
        DEBUG_PRINT("detect_variant_from_magic: matched X2580 -> T41 (T41N board)\n");
        return VARIANT_T41;
    }

    // Check for X-series processors first (more specific)
    if (strstr(magic, "x1000") || strstr(magic, "X1000")) return VARIANT_X1000;
    if (strstr(magic, "x1600") || strstr(magic, "X1600")) return VARIANT_X1600;
    if (strstr(magic, "x1700") || strstr(magic, "X1700")) return VARIANT_X1700;
    if (strstr(magic, "x2000") || strstr(magic, "X2000")) return VARIANT_X2000;
    if (strstr(magic, "x2100") || strstr(magic, "X2100")) return VARIANT_X2100;
    if (strstr(magic, "x2600") || strstr(magic, "X2600")) return VARIANT_X2600;

    // Check for A1 (special case - reports "A1" in firmware stage)
    if (strcmp(magic, "A1") == 0 || strcmp(magic, "a1") == 0) {
        DEBUG_PRINT("detect_variant_from_magic: matched A1 -> A1\n");
        return VARIANT_A1;
    }

    // Check for T31 sub-variants
    if (strstr(magic, "t31zx") || strstr(magic, "T31ZX") || strstr(magic, "zx")) return VARIANT_T31ZX;

    // Parse common patterns from Ingenic CPUs
    // Format is typically "BOOT47XX" where XX indicates processor variant
    // But we're getting "T 3 1 V " format (with spaces), so handle that too
    if (strlen(magic) >= 4) {
        DEBUG_PRINT("detect_variant_from_magic: checking pattern match\n");
        
        // Create a compact version without spaces for comparison
        char compact_magic[9] = {0};
        int compact_pos = 0;
        for (int i = 0; magic[i] && compact_pos < 8; i++) {
            if (magic[i] != ' ') {
                compact_magic[compact_pos++] = magic[i];
            }
        }
        
        // Check for T31 pattern at the beginning
        if (strncmp(compact_magic, "T31V", 4) == 0) {
            DEBUG_PRINT("detect_variant_from_magic: matched T31V -> T31ZX\n");
            return VARIANT_T31ZX;  // T31V indicates T31ZX
        }
        if (strncmp(compact_magic, "T31", 3) == 0) {
            DEBUG_PRINT("detect_variant_from_magic: matched T31 -> T31\n");
            return VARIANT_T31;
        }
        if (strncmp(compact_magic, "T20", 3) == 0) return VARIANT_T20;
        if (strncmp(compact_magic, "T21", 3) == 0) return VARIANT_T21;
        if (strncmp(compact_magic, "T23", 3) == 0) return VARIANT_T23;
        if (strncmp(compact_magic, "T30", 3) == 0) return VARIANT_T30;
        if (strncmp(compact_magic, "T40", 3) == 0) return VARIANT_T40;
        if (strncmp(compact_magic, "T41", 3) == 0) return VARIANT_T41;
    }
    
    // Fallback to original pattern for 8-character strings
    if (strlen(magic) >= 8) {
        const char* suffix = &magic[6];
        
        if (strncmp(suffix, "20", 2) == 0) return VARIANT_T20;
        if (strncmp(suffix, "21", 2) == 0) return VARIANT_T21;
        if (strncmp(suffix, "23", 2) == 0) return VARIANT_T23;
        if (strncmp(suffix, "30", 2) == 0) return VARIANT_T30;
        if (strncmp(suffix, "31", 2) == 0) return VARIANT_T31;
        if (strncmp(suffix, "40", 2) == 0) return VARIANT_T40;
        if (strncmp(suffix, "41", 2) == 0) return VARIANT_T41;
    }
    
    DEBUG_PRINT("detect_variant_from_magic: defaulting to T31X\n");
    return VARIANT_T31X; // Default to T31X
}