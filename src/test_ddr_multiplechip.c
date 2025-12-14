/**
 * DDR Multi-Chip Test Program
 * Tests DDR generation for T23N, T31NL, and T31X chips
 * Verifies against reference binaries
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "ddr/ddr_controller.h"
#include "ddr/ddr_param_builder.h"
#include "ddr/ddr_generator.h"

// Reference DDR binaries for comparison
static const char *ref_file = 
    "/home/squash/go/src/github.com/wltechblog/thingino-cloner-c/references/ddr_extracted.bin";

/**
 * Load reference DDR binary
 */
uint8_t *load_reference_binary(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: Cannot open %s\n", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *buf = malloc(*size);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    
    if (fread(buf, 1, *size, f) != *size) {
        fprintf(stderr, "ERROR: Failed to read %s\n", path);
        free(buf);
        fclose(f);
        return NULL;
    }
    
    fclose(f);
    return buf;
}

/**
 * Compare generated DDR binary with reference
 */
void compare_binaries(const char *label, uint8_t *generated, uint8_t *reference, size_t size) {
    printf("\n=== %s ===\n", label);
    
    int differences = 0;
    int first_diff = -1;
    
    // Compare byte by byte
    for (size_t i = 0; i < size; i++) {
        if (generated[i] != reference[i]) {
            differences++;
            if (first_diff == -1) first_diff = i;
        }
    }
    
    if (differences == 0) {
        printf("✓ PERFECT MATCH - All %zu bytes identical\n", size);
    } else {
        printf("✗ MISMATCH - %d bytes differ (first at 0x%x)\n", differences, first_diff);
        
        // Show first few differences
        printf("\nFirst 10 differences:\n");
        int shown = 0;
        for (size_t i = 0; i < size && shown < 10; i++) {
            if (generated[i] != reference[i]) {
                printf("  Offset 0x%04zx: generated=0x%02x, reference=0x%02x\n",
                       i, generated[i], reference[i]);
                shown++;
            }
        }
    }
    
    // Show DDRC section comparison (first 4 values are most critical)
    printf("\nDDRC Header Comparison:\n");
    printf("  Generated: [0x%02x%02x%02x%02x] [0x%02x%02x%02x%02x]\n",
           generated[0], generated[1], generated[2], generated[3],
           generated[4], generated[5], generated[6], generated[7]);
    printf("  Reference: [0x%02x%02x%02x%02x] [0x%02x%02x%02x%02x]\n",
           reference[0], reference[1], reference[2], reference[3],
           reference[4], reference[5], reference[6], reference[7]);
}

/**
 * Test single chip configuration
 */
int test_chip(chip_type_t chip, const char *chip_name) {
    printf("\n" "================================================================\n");
    printf("Testing: %s (0x%02x)\n", chip_name, chip);
    printf("================================================================\n");
    
    // Get chip configuration
    ddr_chip_config_t chip_config;
    if (ddr_get_chip_config(chip, &chip_config) != 0) {
        fprintf(stderr, "ERROR: Failed to get config for chip 0x%02x\n", chip);
        return -1;
    }
    
    // Print chip configuration
    ddr_print_config(&chip_config);
    
    // Build DDR parameters
    ddr_params_t ddr_params;
    if (ddr_build_params(&chip_config, &ddr_params) != 0) {
        fprintf(stderr, "ERROR: Failed to build parameters\n");
        return -1;
    }
    
    // Create DDR config from parameters
    // Note: chip_config timing is in picoseconds, but ddr_config expects nanoseconds
    ddr_config_t config;
    memset(&config, 0, sizeof(config));
    config.type = chip_config.ddr_type;
    config.clock_mhz = chip_config.ddr_freq / 1000000;
    config.tWR = chip_config.tWR / 1000;      // Convert ps to ns
    config.tRAS = chip_config.tRAS / 1000;    // Convert ps to ns
    config.tRCD = chip_config.tRCD / 1000;    // Convert ps to ns
    config.tRL = chip_config.tRL / 1000;      // Convert ps to ns
    config.tRP = chip_config.tRP / 1000;      // Convert ps to ns
    config.tRRD = chip_config.tRRD / 1000;    // Convert ps to ns
    config.tRC = chip_config.tRC / 1000;      // Convert ps to ns
    config.tRFC = chip_config.tRFC / 1000;    // Convert ps to ns
    config.tCKE = chip_config.tCKE / 1000;    // Convert ps to ns
    config.tXP = chip_config.tXP / 1000;      // Convert ps to ns
    config.tREFI = chip_config.tREFI / 1000;  // Convert ps to ns
    config.cas_latency = 3;                    // CAS latency for DDR2
    
    // Calculate tWL and tWTR (matches bridge_chip_config_to_ddr_config logic)
    uint32_t tWL_cycles = (config.cas_latency > 0) ? (config.cas_latency - 1) : 1;
    config.tWL = (tWL_cycles * 1000) / config.clock_mhz;  // Convert cycles to ns
    
    uint32_t tWTR_cycles = 2;  // DDR2 minimum
    config.tWTR = (tWTR_cycles * 1000) / config.clock_mhz;  // Convert cycles to ns
    
    printf("\nDebug - Calculated timing parameters:\n");
    printf("  tWL: %u ns (from %u cycles)\n", config.tWL, tWL_cycles);
    printf("  tWTR: %u ns (from %u cycles)\n", config.tWTR, tWTR_cycles);
    
    // Allocate buffers
    uint8_t obj_buffer[0x220];
    uint8_t ddrc_regs[0xbc];
    
    memset(obj_buffer, 0, sizeof(obj_buffer));
    memset(ddrc_regs, 0, sizeof(ddrc_regs));
    
    // Generate DDR configuration
    printf("\nGenerating DDR configuration...\n");
    ddr_init_object_buffer(&config, obj_buffer);
    if (ddr_generate_ddrc_with_object(&config, obj_buffer, ddrc_regs) != 0) {
        fprintf(stderr, "ERROR: DDR generation failed\n");
        return -1;
    }
    
    // Generate full 324-byte binary for comparison
    uint8_t generated_binary[324];
    if (ddr_generate_binary(&config, generated_binary, 324) != 0) {
        fprintf(stderr, "ERROR: Failed to generate full DDR binary\n");
        return -1;
    }
    
    // Load reference binary
    size_t ref_size = 0;
    uint8_t *reference = load_reference_binary(ref_file, &ref_size);
    if (!reference) {
        fprintf(stderr, "WARNING: Could not load reference binary\n");
        return 0;
    }
    
    // Compare full binary section by section
    printf("\n=== Full Binary Comparison ===\n");
    
    // FIDB section
    printf("\nFIDB (0x00-0x03):\n");
    for (int i = 0; i < 4; i++) {
        if (generated_binary[i] != reference[i]) {
            printf("  [0x%02x] gen=0x%02x ref=0x%02x DIFF\n", i, generated_binary[i], reference[i]);
        }
    }
    
    // DDRC section (188 bytes)
    printf("\nDDRC (0x04-0xbf, 188 bytes):\n");
    int ddrc_diffs = 0;
    for (int i = 4; i < 0xc0; i++) {
        if (generated_binary[i] != reference[i]) {
            printf("  [0x%02x] gen=0x%02x ref=0x%02x DIFF\n", i, generated_binary[i], reference[i]);
            ddrc_diffs++;
            if (ddrc_diffs >= 20) {
                printf("  ... more differences\n");
                break;
            }
        }
    }
    if (ddrc_diffs == 0) printf("  ✓ DDRC matches reference exactly\n");
    
    // RDD marker
    printf("\nRDD (0xc0-0xc3):\n");
    for (int i = 0xc0; i < 0xc4; i++) {
        if (generated_binary[i] != reference[i]) {
            printf("  [0x%02x] gen=0x%02x ref=0x%02x DIFF\n", i, generated_binary[i], reference[i]);
        }
    }
    
    // DDRP section (128 bytes)
    printf("\nDDRP (0xc4-0x143, 128 bytes):\n");
    int ddrp_diffs = 0;
    for (int i = 0xc4; i < 0x144; i++) {
        if (generated_binary[i] != reference[i]) {
            printf("  [0x%02x] gen=0x%02x ref=0x%02x DIFF\n", i, generated_binary[i], reference[i]);
            ddrp_diffs++;
            if (ddrp_diffs >= 20) {
                printf("  ... more differences\n");
                break;
            }
        }
    }
    if (ddrp_diffs == 0) printf("  ✓ DDRP matches reference exactly\n");
    
    printf("\n=== Summary ===\n");
    printf("Total DDRC differences: %d bytes\n", ddrc_diffs);
    printf("Total DDRP differences: %d bytes\n", ddrp_diffs);
    
    free(reference);
    return 0;
}

int main() {
    printf("\n" "===========================================================================\n");
    printf("DDR Multi-Chip Generation Test - Path 3 Implementation\n");
    printf("Testing byte-perfect DDR binary generation for multiple Ingenic processors\n");
    printf("===========================================================================\n");
    
    // Test all supported chips
    test_chip(CHIP_T23N, "T23N");
    test_chip(CHIP_T31L, "T31NL (T31L)");
    test_chip(CHIP_T31X, "T31X");
    
    printf("\n" "===================================================================\n");
    printf("Testing Complete\n");
    printf("===================================================================\n");
    
    return 0;
}