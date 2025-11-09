/**
 * DDR Binary Builder - Matches Python script format
 * Builds FIDB (platform config) + RDD (DDR PHY params) format
 */

#include "ddr_binary_builder.h"
#include "ddr_config_database.h"
#include <string.h>
#include <stdio.h>
#include <zlib.h>

/**
 * Get default platform configuration from embedded database
 */
int ddr_get_platform_config(const char *platform_name, platform_config_t *config) {
    if (!platform_name || !config) return -1;

    const processor_config_t *proc_cfg = processor_config_get(platform_name);
    if (!proc_cfg) {
        // Default to T31 config if not found
        proc_cfg = processor_config_get("t31");
        if (!proc_cfg) return -1;
    }

    config->crystal_freq = proc_cfg->crystal_freq;
    config->cpu_freq = proc_cfg->cpu_freq;
    config->ddr_freq = proc_cfg->ddr_freq;
    config->uart_baud = proc_cfg->uart_baud;
    config->mem_size = proc_cfg->mem_size;

    return 0;
}

/**
 * Get default platform configuration by processor variant
 *
 * Wrapper around ddr_get_platform_config() that accepts processor_variant_t enum.
 * Maps variant enums to platform name strings.
 */
int ddr_get_platform_config_by_variant(int variant, platform_config_t *config) {
    if (!config) return -1;

    // Map processor variant enum to platform name
    // These values match the processor_variant_t enum from thingino.h:
    // VARIANT_T20 = 0, VARIANT_T21 = 1, VARIANT_T30 = 3, VARIANT_T31X = 5,
    // VARIANT_T31ZX = 6, VARIANT_T41 = 9
    const char *platform_name;

    switch (variant) {
        case 0:  // VARIANT_T20
            platform_name = "t20";
            break;
        case 1:  // VARIANT_T21
            platform_name = "t21";
            break;
        case 3:  // VARIANT_T30
            platform_name = "t30";
            break;
        case 5:  // VARIANT_T31X
        case 6:  // VARIANT_T31ZX
        case 4:  // VARIANT_T31 (generic)
            platform_name = "t31";
            break;
        case 9:  // VARIANT_T41
            platform_name = "t41";
            break;
        default:
            // Unsupported variant - default to T31
            platform_name = "t31";
            break;
    }

    return ddr_get_platform_config(platform_name, config);
}

/**
 * Write 32-bit little-endian value
 */
static void write_u32_le(uint8_t *buf, uint32_t value) {
    buf[0] = value & 0xFF;
    buf[1] = (value >> 8) & 0xFF;
    buf[2] = (value >> 16) & 0xFF;
    buf[3] = (value >> 24) & 0xFF;
}

/**
 * Build FIDB section (192 bytes: 8 header + 184 data)
 */
size_t ddr_build_fidb(const platform_config_t *platform, uint8_t *output) {
    if (!platform || !output) return 0;
    
    // Clear output buffer
    memset(output, 0, 192);
    
    // Write header: "FIDB" + size (184 bytes)
    output[0] = 'F';
    output[1] = 'I';
    output[2] = 'D';
    output[3] = 'B';
    write_u32_le(output + 4, 184);
    
    // FIDB data starts at offset 8
    uint8_t *fidb_data = output + 8;
    
    // Offset 0x00: crystal_freq
    write_u32_le(fidb_data + 0x00, platform->crystal_freq);
    
    // Offset 0x04: cpu_freq
    write_u32_le(fidb_data + 0x04, platform->cpu_freq);
    
    // Offset 0x08: ddr_freq
    write_u32_le(fidb_data + 0x08, platform->ddr_freq);
    
    // Offset 0x0c: Reserved
    write_u32_le(fidb_data + 0x0c, 0x00000000);
    
    // Offset 0x10: Enable flag
    write_u32_le(fidb_data + 0x10, 0x00000001);
    
    // Offset 0x14: uart_baud
    write_u32_le(fidb_data + 0x14, platform->uart_baud);
    
    // Offset 0x18: Flag
    write_u32_le(fidb_data + 0x18, 0x00000001);
    
    // Offset 0x20: mem_size
    write_u32_le(fidb_data + 0x20, platform->mem_size);
    
    // Offset 0x24: Flag
    write_u32_le(fidb_data + 0x24, 0x00000001);
    
    // Offset 0x2c: Flag
    write_u32_le(fidb_data + 0x2c, 0x00000011);
    
    // Offset 0x30: Platform ID (0x19800000)
    // This value appears in T31 reference binaries
    // May be platform-specific - not found in Ingenic u-boot source
    // Possibly a chip ID or bootloader version identifier
    write_u32_le(fidb_data + 0x30, 0x19800000);
    
    return 192;
}

/**
 * Build RDD section (132 bytes: 8 header + 124 data)
 */
size_t ddr_build_rdd(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output) {
    if (!platform || !params || !output) return 0;

    // Clear output buffer
    memset(output, 0, 132);

    // RDD data buffer (124 bytes)
    uint8_t rdd_data[124];
    memset(rdd_data, 0, 124);

    // Offset 0x00: CRC32 (fill in later)

    // Offset 0x04: DDR type
    write_u32_le(rdd_data + 0x04, params->ddr_type);

    // Offset 0x08-0x0f: Reserved

    // Offset 0x10: Frequency value (freq_hz / 100000)
    uint32_t freq_val = platform->ddr_freq / 100000;
    write_u32_le(rdd_data + 0x10, freq_val);

    // Offset 0x14: Another frequency value (0x2800 = 10240)
    // Purpose unknown - possibly related to tREFI or timing calculations
    // This value appears in all reference binaries examined
    write_u32_le(rdd_data + 0x14, 0x00002800);

    // Offset 0x18-0x1b: Fixed values from reverse engineering
    // Purpose unknown - these values appear consistently in reference binaries
    // Not found in Ingenic u-boot source code
    rdd_data[0x18] = 0x01;
    rdd_data[0x19] = 0x00;
    rdd_data[0x1a] = 0xc2;  // Not related to MXIC NAND ID (also 0xC2)
    rdd_data[0x1b] = 0x00;

    // Offset 0x1c: CL (CAS latency)
    rdd_data[0x1c] = params->cl;

    // Offset 0x1d: BL (Burst length)
    rdd_data[0x1d] = params->bl;

    // Offset 0x1e: ROW (stored directly)
    // NOTE: Different from DDRC CFG register which uses (row - 12)
    // See references/ingenic-u-boot-xburst1/tools/ingenic-tools/ddr_params_creator.c line 207
    rdd_data[0x1e] = params->row_bits;

    // Offset 0x1f: COL (encoded as col - 6)
    // NOTE: Different from DDRC CFG register which uses (col - 8)
    // See references/ingenic-u-boot-xburst1/tools/ingenic-tools/ddr_params_creator.c line 208
    rdd_data[0x1f] = params->col_bits - 6;

    // Offset 0x20-0x2b: Timing parameters (in clock cycles)
    // Calculated using ps2cycle_ceil formula from u-boot source
    rdd_data[0x20] = params->tRAS;
    rdd_data[0x21] = params->tRC;
    rdd_data[0x22] = params->tRCD;
    rdd_data[0x23] = params->tRP;
    rdd_data[0x24] = params->tRFC;
    rdd_data[0x25] = 0x04;  // Unknown purpose - appears in all reference binaries
    rdd_data[0x26] = params->tRTP;
    rdd_data[0x27] = 0x20;  // Unknown purpose (0x20 = 32 decimal)
    rdd_data[0x28] = params->tFAW;
    rdd_data[0x29] = 0x00;  // Unknown purpose
    rdd_data[0x2a] = params->tRRD;
    rdd_data[0x2b] = params->tWTR;

    // DQ mapping table (last 20 bytes, offset 0x68-0x7B in RDD data)
    // Maps logical DQ pins to physical PCB traces
    // This is BOARD-SPECIFIC and may need customization for different hardware
    // Default mapping from reference binaries - appears to work for T31 dev boards
    // Format: dq_mapping[logical_pin] = physical_pin
    uint8_t dq_mapping[20] = {12, 13, 14, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 15, 16, 17, 18, 19};
    memcpy(rdd_data + 124 - 20, dq_mapping, 20);

    // Calculate CRC32 checksum (skip first 4 bytes)
    uint32_t crc = crc32(0L, rdd_data + 4, 120);
    write_u32_le(rdd_data, crc);

    // Build header: padding + "RDD" + size (matches vendor layout)
    output[0] = 0x00;
    output[1] = 'R';
    output[2] = 'D';
    output[3] = 'D';
    write_u32_le(output + 4, 124);

    // Copy RDD data
    memcpy(output + 8, rdd_data, 124);

    return 132;
}

/**
 * Build complete DDR binary (324 bytes)
 */
size_t ddr_build_binary(const platform_config_t *platform, const ddr_phy_params_t *params, uint8_t *output) {
    if (!platform || !params || !output) return 0;

    // Build FIDB section (192 bytes)
    size_t fidb_size = ddr_build_fidb(platform, output);

    // Build RDD section (132 bytes)
    size_t rdd_size = ddr_build_rdd(platform, params, output + fidb_size);

    return fidb_size + rdd_size;
}

