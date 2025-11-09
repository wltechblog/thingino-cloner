#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ddr/ddr_types.h"
#include "ddr/ddr_generator.h"

int main(int argc, char *argv[]) {
    printf("=== T31X DDR Binary Generator ===\n\n");
    
    const char *output_file = argc > 1 ? argv[1] : "generated_t31x.bin";
    
    // Create configuration matching the reference binary
    // Based on reverse engineering: LPDDR2, 844 MHz, RL=8, WL=4, ROW=13, COL=9
    // Using actual register values from reference (not nanoseconds!)
    ddr_config_t config = {
        .type = DDR_TYPE_LPDDR2,
        .clock_mhz = 844,

        // LPDDR2 timing - using register values from reference binary
        .tRL = 8,              // Read Latency = 8
        .tWL = 4,              // Write Latency = 4
        .tRAS = 7,             // From reference 0x20
        .tRP = 2,              // From reference 0x23
        .tRCD = 18,            // From reference 0x22
        .tRC = 7,              // From reference 0x21
        .tWR = 49,             // From reference 0x28
        .tRRD = 2,             // Guess (within range 1-8)
        .tWTR = 32,            // From reference 0x27
        .tRFC = 23,            // From reference 0x24
        .tREFI = 6500,         // From reference 0x2a-0x2b (100 + 25*256 = 6500)

        // Memory geometry
        .row_bits = 13,
        .col_bits = 9,
        .banks = 8,
        .data_width = 16,
        .total_size_bytes = 128 * 1024 * 1024,  // 128 MB
    };
    
    printf("Configuration:\n");
    printf("  Type: LPDDR2\n");
    printf("  Clock: %u MHz\n", config.clock_mhz);
    printf("  RL: %u, WL: %u\n", config.tRL, config.tWL);
    printf("  ROW: %u, COL: %u\n", config.row_bits, config.col_bits);
    printf("  Banks: %u\n", config.banks);
    printf("\n");
    
    // Generate binary
    uint8_t output[324];
    int result = ddr_generate_binary(&config, output, sizeof(output));
    
    if (result != 0) {
        printf("[ERROR] Failed to generate binary: %d\n", result);
        return 1;
    }
    
    printf("[OK] Generated 324-byte binary\n");
    
    // Write to file
    FILE *fp = fopen(output_file, "wb");
    if (!fp) {
        printf("[ERROR] Cannot open output file: %s\n", output_file);
        return 1;
    }
    
    size_t written = fwrite(output, 1, 324, fp);
    fclose(fp);
    
    if (written != 324) {
        printf("[ERROR] Failed to write complete binary\n");
        return 1;
    }
    
    printf("[OK] Written to: %s\n", output_file);
    
    // Show hex dump of key sections
    printf("\nKey sections:\n");
    printf("  FIDB signature: %c%c%c%c\n", 
           output[0], output[1], output[2], output[3]);
    printf("  RDD signature at 0xC0: %02x %02x %02x %02x\n",
           output[0xc0], output[0xc1], output[0xc2], output[0xc3]);
    
    // Show DDRP section (RDD data)
    printf("\nDDRP section (0xC4-0xD3):\n  ");
    for (int i = 0xc4; i < 0xd4; i++) {
        printf("%02x ", output[i]);
    }
    printf("\n");
    
    printf("\nUse test_ddr_compare to compare with reference:\n");
    printf("  ./test_ddr_compare references/ddr_extracted.bin %s\n", output_file);
    
    return 0;
}

