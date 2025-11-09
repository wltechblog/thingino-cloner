#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ddr_binary_builder.h"
#include "ddr_config_database.h"

// Helper to convert ps to cycles
static inline uint32_t ps_to_cycles_ceil(uint32_t ps, uint32_t freq_hz) {
    uint64_t numerator = (uint64_t)ps * freq_hz + 999999999999ULL;
    return (uint32_t)(numerator / 1000000000000ULL);
}

int main() {
    // Get T20 platform config
    platform_config_t platform_cfg;
    if (ddr_get_platform_config("t20", &platform_cfg) != 0) {
        fprintf(stderr, "Failed to get T20 platform config\n");
        return 1;
    }
    
    printf("T20 Platform Config:\n");
    printf("  Crystal: %u Hz\n", platform_cfg.crystal_freq);
    printf("  CPU: %u Hz\n", platform_cfg.cpu_freq);
    printf("  DDR: %u Hz\n", platform_cfg.ddr_freq);
    printf("  UART: %u baud\n", platform_cfg.uart_baud);
    printf("  Memory: %u bytes\n", platform_cfg.mem_size);
    printf("\n");
    
    // Get default DDR chip for T20
    const ddr_chip_config_t *chip_cfg = ddr_chip_config_get_default("t20");
    if (!chip_cfg) {
        fprintf(stderr, "Failed to get default DDR chip for T20\n");
        return 1;
    }
    
    printf("DDR Chip: %s (%s)\n", chip_cfg->name, chip_cfg->vendor);
    printf("  Type: %u (1=DDR2, 2=DDR3)\n", chip_cfg->ddr_type);
    printf("  Row bits: %u\n", chip_cfg->row_bits);
    printf("  Col bits: %u\n", chip_cfg->col_bits);
    printf("  CL: %u\n", chip_cfg->cl);
    printf("  BL: %u\n", chip_cfg->bl);
    printf("  RL: %u\n", chip_cfg->rl);
    printf("  WL: %u\n", chip_cfg->wl);
    printf("\n");
    
    printf("Timing (picoseconds):\n");
    printf("  tRAS: %u ps\n", chip_cfg->tRAS);
    printf("  tRC: %u ps\n", chip_cfg->tRC);
    printf("  tRCD: %u ps\n", chip_cfg->tRCD);
    printf("  tRP: %u ps\n", chip_cfg->tRP);
    printf("  tRFC: %u ps\n", chip_cfg->tRFC);
    printf("  tRTP: %u ps\n", chip_cfg->tRTP);
    printf("  tFAW: %u ps\n", chip_cfg->tFAW);
    printf("  tRRD: %u ps\n", chip_cfg->tRRD);
    printf("  tWTR: %u ps\n", chip_cfg->tWTR);
    printf("  tWR: %u ps\n", chip_cfg->tWR);
    printf("  tREFI: %u ps\n", chip_cfg->tREFI);
    printf("\n");
    
    // Convert timing to cycles
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

    printf("Timing (cycles @ %u Hz):\n", platform_cfg.ddr_freq);
    printf("  tRAS: %u\n", phy_params.tRAS);
    printf("  tRC: %u\n", phy_params.tRC);
    printf("  tRCD: %u\n", phy_params.tRCD);
    printf("  tRP: %u\n", phy_params.tRP);
    printf("  tRFC: %u\n", phy_params.tRFC);
    printf("  tRTP: %u\n", phy_params.tRTP);
    printf("  tFAW: %u\n", phy_params.tFAW);
    printf("  tRRD: %u\n", phy_params.tRRD);
    printf("  tWTR: %u\n", phy_params.tWTR);
    printf("\n");
    
    // Generate DDR binary
    uint8_t *buffer = malloc(DDR_BINARY_SIZE);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return 1;
    }
    
    size_t size = ddr_build_binary(&platform_cfg, &phy_params, buffer);
    if (size == 0) {
        fprintf(stderr, "Failed to generate DDR binary\n");
        free(buffer);
        return 1;
    }
    
    printf("Generated DDR binary: %zu bytes\n\n", size);
    
    // Write to file
    FILE *f = fopen("/tmp/t20_generated.bin", "wb");
    if (f) {
        fwrite(buffer, 1, size, f);
        fclose(f);
        printf("Wrote DDR binary to /tmp/t20_generated.bin\n");
    }
    
    // Dump first 64 bytes in hex
    printf("\nFirst 64 bytes (hex):\n");
    for (int i = 0; i < 64 && i < size; i++) {
        printf("%02x ", buffer[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
    
    free(buffer);
    return 0;
}

