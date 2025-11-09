#ifndef DDR_PARAM_BUILDER_H
#define DDR_PARAM_BUILDER_H

#include <stdint.h>
#include <string.h>

/**
 * DDR Parameter Builder - Constructs ddr_params structure from chip configuration
 * Based on reverse engineering of vendor binary DDR generation code
 */

// Note: DDR_TYPE constants are defined in ddr_types.h as enum
// These defines are internal vendor constants and should not conflict

// Chip type constants
typedef enum {
    CHIP_T23N = 0x23,
    CHIP_T31L = 0x32,   // T31NL - different identifier from T31X
    CHIP_T31X = 0x31,
    CHIP_T41N = 0x41,   // T41N
} chip_type_t;

/**
 * DDR Parameters Structure
 * This is the internal structure passed to ddrc_config_creator and ddrp_config_creator
 * Offset in object: this + 0x154
 */
typedef struct {
    uint32_t ddr_type;           // +0x00: DDR type (0-4)
    uint32_t param1;             // +0x04: reserved
    uint32_t param2;             // +0x08: reserved
    uint32_t dram_width;         // +0x0c: 4 or 8 (16-bit or 32-bit)
    uint32_t chip_count;         // +0x10: 1 or 2
    uint32_t banks;              // +0x14: banks per chip (8 or 16)
    uint32_t tWR;                // +0x18: write recovery time (ps)
    uint32_t tRL;                // +0x1c: read latency (ps)
    uint32_t tRP;                // +0x20: precharge to activate delay (ps)
    uint32_t tRCD;               // +0x24: activate to read/write delay (ps)
    uint32_t tRAS;               // +0x28: row active time (ps)
    uint32_t tRC;                // +0x2c: row cycle time (ps)
    uint32_t tRRD;               // +0x30: row to row delay (ps)
    uint32_t tREFI;              // +0x34: refresh interval (ps)
    uint32_t row_size;           // +0x38: total memory size (bytes)
    uint32_t col_size;           // +0x3c: size per chip (bytes)
    uint32_t param15;            // +0x40: reserved
    uint32_t param16;            // +0x44: reserved
    // ... more parameters follow
} ddr_params_t;

/**
 * Reference DDR Configuration for each chip
 * Extracted from vendor tool reference binaries
 */
typedef struct {
    chip_type_t chip_type;
    uint32_t ddr_type;
    
    // DDRC output values (from vendor binary analysis)
    uint32_t ddrc_0x7c;  // Address remap low
    uint32_t ddrc_0x80;  // Controller timing base  
    uint32_t ddrc_0x90;  // Memory config low
    uint32_t ddrc_0x94;  // Memory config high
    
    // Memory configuration
    uint32_t dram_width;
    uint32_t chip_count;
    uint32_t banks;
    
    // Frequency (Hz)
    uint32_t ddr_freq;
    
    // Timing parameters (picoseconds)
    uint32_t tWR;
    uint32_t tRL;
    uint32_t tRP;
    uint32_t tRCD;
    uint32_t tRAS;
    uint32_t tRC;
    uint32_t tRRD;
    uint32_t tCKE;
    uint32_t tXP;
    uint32_t tRFC;
    uint32_t tREFI;
} ddr_chip_config_t;

/**
 * Get reference DDR configuration for a chip type
 * @param chip: chip type (T23N, T31L, T31X)
 * @param config: pointer to fill with configuration
 * @return: 0 on success, -1 if chip not supported
 */
int ddr_get_chip_config(chip_type_t chip, ddr_chip_config_t *config);

/**
 * Build DDR parameters structure from chip configuration
 * @param config: reference chip configuration
 * @param params: output parameter structure
 * @return: 0 on success
 */
int ddr_build_params(const ddr_chip_config_t *config, ddr_params_t *params);

/**
 * Print DDR configuration for debugging
 * @param config: configuration to print
 */
void ddr_print_config(const ddr_chip_config_t *config);

#endif // DDR_PARAM_BUILDER_H