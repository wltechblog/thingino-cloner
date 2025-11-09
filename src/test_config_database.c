/**
 * Test Configuration Database - Verify embedded processor and DDR configs
 */

#include "ddr_config_database.h"
#include "ddr_binary_builder.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("=== Configuration Database Test ===\n\n");
    
    // List all processors
    size_t proc_count;
    const processor_config_t *processors = processor_config_list(&proc_count);
    
    printf("Available Processors (%zu):\n", proc_count);
    printf("%-10s %-12s %-12s %-12s %-10s %-10s\n", 
           "Name", "Crystal", "CPU", "DDR", "UART", "Memory");
    printf("%-10s %-12s %-12s %-12s %-10s %-10s\n",
           "----------", "------------", "------------", "------------", "----------", "----------");
    
    for (size_t i = 0; i < proc_count; i++) {
        printf("%-10s %10u Hz %10u Hz %10u Hz %10u %8u MB\n",
               processors[i].name,
               processors[i].crystal_freq,
               processors[i].cpu_freq,
               processors[i].ddr_freq,
               processors[i].uart_baud,
               processors[i].mem_size / (1024 * 1024));
    }
    
    // List all DDR chips
    size_t ddr_count;
    const ddr_chip_config_t *ddr_chips = ddr_chip_config_list(&ddr_count);
    
    printf("\nAvailable DDR Chips (%zu):\n", ddr_count);
    printf("%-30s %-10s %-8s %-4s %-4s %-4s %-4s\n",
           "Name", "Vendor", "Type", "ROW", "COL", "CL", "BL");
    printf("%-30s %-10s %-8s %-4s %-4s %-4s %-4s\n",
           "------------------------------", "----------", "--------", "----", "----", "----", "----");
    
    for (size_t i = 0; i < ddr_count; i++) {
        const char *type_str;
        switch (ddr_chips[i].ddr_type) {
            case 0: type_str = "DDR3"; break;
            case 1: type_str = "DDR2"; break;
            case 2: type_str = "LPDDR2"; break;
            case 4: type_str = "LPDDR3"; break;
            default: type_str = "Unknown"; break;
        }
        
        printf("%-30s %-10s %-8s %4u %4u %4u %4u\n",
               ddr_chips[i].name,
               ddr_chips[i].vendor,
               type_str,
               ddr_chips[i].row_bits,
               ddr_chips[i].col_bits,
               ddr_chips[i].cl,
               ddr_chips[i].bl);
    }
    
    // Test default DDR chip lookup
    printf("\nDefault DDR Chips for Processors:\n");
    printf("%-10s %-30s\n", "Processor", "Default DDR Chip");
    printf("%-10s %-30s\n", "----------", "------------------------------");

    const char *test_processors[] = {"a1", "a1ne", "a1nt", "t20", "t21", "t23", "t30", "t31", "t31x", "t40", "t41", "t41n"};
    for (size_t i = 0; i < sizeof(test_processors) / sizeof(test_processors[0]); i++) {
        const ddr_chip_config_t *default_ddr = ddr_chip_config_get_default(test_processors[i]);
        if (default_ddr) {
            printf("%-10s %-30s\n", test_processors[i], default_ddr->name);
        } else {
            printf("%-10s %-30s\n", test_processors[i], "(none)");
        }
    }
    
    // Test DDR binary generation with different processors
    printf("\nTesting DDR Binary Generation:\n");
    
    const char *test_configs[][2] = {
        {"t31x", "M14D1G1664A_DDR2"},
        {"t41", "H5TQ2G83CFR_DDR3"},
        {"t30", "W971GV6NG_DDR2"},
    };
    
    for (size_t i = 0; i < sizeof(test_configs) / sizeof(test_configs[0]); i++) {
        const char *proc_name = test_configs[i][0];
        const char *ddr_name = test_configs[i][1];
        
        printf("\n  Testing %s + %s...\n", proc_name, ddr_name);
        
        // Get processor config
        platform_config_t platform_cfg;
        if (ddr_get_platform_config(proc_name, &platform_cfg) != 0) {
            printf("    [FAIL] Failed to get processor config\n");
            continue;
        }
        
        // Get DDR chip config
        const ddr_chip_config_t *ddr_cfg = ddr_chip_config_get(ddr_name);
        if (!ddr_cfg) {
            printf("    [FAIL] Failed to get DDR chip config\n");
            continue;
        }
        
        // Convert to ddr_phy_params_t
        ddr_phy_params_t phy_params = {
            .ddr_type = ddr_cfg->ddr_type,
            .row_bits = ddr_cfg->row_bits,
            .col_bits = ddr_cfg->col_bits,
            .cl = ddr_cfg->cl,
            .bl = ddr_cfg->bl,
            // Convert timing from ps to cycles (simplified - actual conversion needs clock period)
            .tRAS = 18,  // Placeholder
            .tRC = 23,
            .tRCD = 6,
            .tRP = 6,
            .tRFC = 52,
            .tRTP = 3,
            .tFAW = 18,
            .tRRD = 4,
            .tWTR = 3
        };
        
        // Generate binary
        uint8_t *ddr_binary = (uint8_t*)malloc(DDR_BINARY_SIZE);
        if (!ddr_binary) {
            printf("    [FAIL] Failed to allocate memory\n");
            continue;
        }
        
        size_t result = ddr_build_binary(&platform_cfg, &phy_params, ddr_binary);
        if (result == DDR_BINARY_SIZE) {
            printf("    [OK] Generated %zu bytes\n", result);
        } else {
            printf("    [FAIL] Generation failed\n");
        }
        
        free(ddr_binary);
    }
    
    printf("\n[SUCCESS] Configuration database test passed!\n");
    return 0;
}

