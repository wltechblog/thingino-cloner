/**
 * DDR Configuration Database - Implementation
 */

#include "ddr_config_database.h"
#include "thingino.h"
#include <string.h>
#include <strings.h>

// ============================================================================
// PROCESSOR CONFIGURATIONS
// ============================================================================

static const processor_config_t processor_configs[] = {
    // A-Series processors
    {
        .name = "a1",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "a1ne",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "a1nt",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },

    // T-Series processors
    {
        .name = "t20",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,      // Updated to match vendor reference configs
        .ddr_freq = 400000000,      // Updated to match vendor reference configs
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t21",
        .crystal_freq = 24000000,
        .cpu_freq = 1000000000,
        .ddr_freq = 300000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "t23",
        .crystal_freq = 24000000,
        .cpu_freq = 1200000000,
        .ddr_freq = 300000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "t30",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t30a",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "t30nl",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t30x",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t31",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t31x",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t31zx",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t31a",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "t31nl",
        .crystal_freq = 24000000,
        .cpu_freq = 576000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 8 * 1024 * 1024  // 8 MB
    },
    {
        .name = "t40",
        .crystal_freq = 24000000,
        .cpu_freq = 1000000000,
        .ddr_freq = 400000000,
        .uart_baud = 115200,
        .mem_size = 16 * 1024 * 1024  // 16 MB
    },
    {
        .name = "t41",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,
        .ddr_freq = 600000000,
        .uart_baud = 115200,
        .mem_size = 32 * 1024 * 1024  // 32 MB
    },
    {
        .name = "t41n",
        .crystal_freq = 24000000,
        .cpu_freq = 800000000,
        .ddr_freq = 400000000,  // 400 MHz DDR (vs 600 MHz on T41)
        .uart_baud = 115200,
        .mem_size = 32 * 1024 * 1024  // 32 MB
    },
};

static const size_t processor_configs_count = sizeof(processor_configs) / sizeof(processor_configs[0]);

// ============================================================================
// FUNCTION IMPLEMENTATIONS
// ============================================================================

const processor_config_t* processor_config_get(const char *name) {
    if (!name) return NULL;
    
    for (size_t i = 0; i < processor_configs_count; i++) {
        if (thingino_strcasecmp(processor_configs[i].name, name) == 0) {
            return &processor_configs[i];
        }
    }
    
    return NULL;
}

const processor_config_t* processor_config_list(size_t *count) {
    if (count) *count = processor_configs_count;
    return processor_configs;
}

// ============================================================================
// DDR CHIP CONFIGURATIONS
// ============================================================================

static const ddr_chip_config_t ddr_chip_configs[] = {
    // DDR2 chips
    {
        .name = "M14D1G1664A_DDR2",
        .vendor = "ESMT",
        .ddr_type = 1,  // DDR2
        .row_bits = 13,
        .col_bits = 10,
        .cl = 7,
        .bl = 8,
        .rl = 7,
        .wl = 6,
        .tRAS = 45000,      // 45 ns
        .tRC = 56250,       // 56.25 ns
        .tRCD = 16000,      // 16 ns
        .tRP = 16000,       // 16 ns
        .tRFC = 127500,     // 127.5 ns
        .tRTP = 7500,       // 7.5 ns
        .tFAW = 45000,      // 45 ns
        .tRRD = 10000,      // 10 ns
        .tWTR = 7500,       // 7.5 ns
        .tWR = 15000,       // 15 ns
        .tREFI = 7800000,   // 7.8 us
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "M14D5121632A_DDR2",
        .vendor = "ESMT",
        .ddr_type = 1,  // DDR2
        .row_bits = 13,
        .col_bits = 10,  // Fixed: vendor reference shows COL=10
        .cl = 7,         // Fixed: vendor reference shows CL=7
        .bl = 8,
        .rl = 7,         // Fixed: vendor reference shows RL=7
        .wl = 6,         // Fixed: vendor reference shows WL=6
        .tRAS = 45000,
        .tRC = 58125,    // Fixed: vendor reference shows tRC=58125 ps
        .tRCD = 13125,   // Fixed: vendor reference shows tRCD=13125 ps
        .tRP = 13250,    // Fixed: vendor reference shows tRP=13250 ps
        .tRFC = 105000,  // Fixed: vendor reference shows tRFC=105 ns
        .tRTP = 8000,    // Fixed: vendor reference shows tRTP=8 ns
        .tFAW = 45000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "M14D2561616A_DDR2",
        .vendor = "ESMT",
        .ddr_type = 1,  // DDR2
        .row_bits = 13,
        .col_bits = 10,
        .cl = 6,
        .bl = 8,
        .rl = 6,
        .wl = 5,
        .tRAS = 45000,
        .tRC = 60000,
        .tRCD = 15000,
        .tRP = 15000,
        .tRFC = 127500,
        .tRTP = 7500,
        .tFAW = 50000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "W971GV6NG_DDR2",
        .vendor = "Winbond",
        .ddr_type = 1,  // DDR2
        .row_bits = 13,
        .col_bits = 10,
        .cl = 6,
        .bl = 8,
        .rl = 6,
        .wl = 5,
        .tRAS = 45000,
        .tRC = 60000,
        .tRCD = 15000,
        .tRP = 15000,
        .tRFC = 127500,
        .tRTP = 7500,
        .tFAW = 50000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "W9751V6NG_DDR2",
        .vendor = "Winbond",
        .ddr_type = 1,  // DDR2
        .row_bits = 13,
        .col_bits = 9,
        .cl = 6,
        .bl = 8,
        .rl = 6,
        .wl = 5,
        .tRAS = 45000,
        .tRC = 60000,
        .tRCD = 15000,
        .tRP = 15000,
        .tRFC = 127500,
        .tRTP = 7500,
        .tFAW = 50000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },

    // DDR3 chips
    {
        .name = "W631GU6NG_DDR3",
        .vendor = "Winbond",
        .ddr_type = 0,  // DDR3
        .row_bits = 13,
        .col_bits = 10,
        .cl = 11,
        .bl = 8,
        .rl = 11,
        .wl = 8,
        .tRAS = 35000,
        .tRC = 48750,
        .tRCD = 13750,
        .tRP = 13750,
        .tRFC = 160000,
        .tRTP = 7500,
        .tFAW = 40000,
        .tRRD = 7500,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "H5TQ1G83DFR_DDR3",
        .vendor = "Hynix",
        .ddr_type = 0,  // DDR3
        .row_bits = 13,
        .col_bits = 10,
        .cl = 11,
        .bl = 8,
        .rl = 11,
        .wl = 8,
        .tRAS = 35000,
        .tRC = 48750,
        .tRCD = 13750,
        .tRP = 13750,
        .tRFC = 160000,
        .tRTP = 7500,
        .tFAW = 40000,
        .tRRD = 7500,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "H5TQ2G83CFR_DDR3",
        .vendor = "Hynix",
        .ddr_type = 0,  // DDR3
        .row_bits = 14,
        .col_bits = 10,
        .cl = 11,
        .bl = 8,
        .rl = 11,
        .wl = 8,
        .tRAS = 35000,
        .tRC = 48750,
        .tRCD = 13750,
        .tRP = 13750,
        .tRFC = 260000,
        .tRTP = 7500,
        .tFAW = 40000,
        .tRRD = 7500,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "M15T1G1664A_DDR3",
        .vendor = "ESMT",
        .ddr_type = 0,  // DDR3
        .row_bits = 13,
        .col_bits = 10,
        .cl = 11,
        .bl = 8,
        .rl = 11,
        .wl = 8,
        .tRAS = 35000,
        .tRC = 48750,
        .tRCD = 13750,
        .tRP = 13750,
        .tRFC = 160000,
        .tRTP = 7500,
        .tFAW = 40000,
        .tRRD = 7500,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },
    {
        .name = "W632GU6NG_DDR3",
        .vendor = "Winbond",
        .ddr_type = 0,  // DDR3
        .row_bits = 14,
        .col_bits = 10,
        .cl = 11,
        .bl = 8,
        .rl = 11,
        .wl = 8,
        .tRAS = 35000,
        .tRC = 48750,
        .tRCD = 13750,
        .tRP = 13750,
        .tRFC = 260000,
        .tRTP = 7500,
        .tFAW = 40000,
        .tRRD = 7500,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 7800000,
        .tCKE = 3,
        .tXP = 3
    },

    // LPDDR2 chips
    {
        .name = "W94AD6KB_LPDDR2",
        .vendor = "Winbond",
        .ddr_type = 2,  // LPDDR2
        .row_bits = 13,
        .col_bits = 10,
        .cl = 6,
        .bl = 8,
        .rl = 6,
        .wl = 3,
        .tRAS = 42000,
        .tRC = 60000,
        .tRCD = 18000,
        .tRP = 18000,
        .tRFC = 130000,
        .tRTP = 7500,
        .tFAW = 50000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 3900000,
        .tCKE = 3,
        .tXP = 3
    },

    // LPDDR3 chips
    {
        .name = "W63CH2MBVABE_LPDDR3",
        .vendor = "Winbond",
        .ddr_type = 4,  // LPDDR3
        .row_bits = 14,
        .col_bits = 10,
        .cl = 12,
        .bl = 8,
        .rl = 12,
        .wl = 6,
        .tRAS = 42000,
        .tRC = 60000,
        .tRCD = 18000,
        .tRP = 18000,
        .tRFC = 130000,
        .tRTP = 7500,
        .tFAW = 50000,
        .tRRD = 10000,
        .tWTR = 7500,
        .tWR = 15000,
        .tREFI = 3900000,
        .tCKE = 3,
        .tXP = 3
    },
};

static const size_t ddr_chip_configs_count = sizeof(ddr_chip_configs) / sizeof(ddr_chip_configs[0]);

// Default DDR chip mappings for each processor
static const struct {
    const char *processor;
    const char *default_ddr;
} default_ddr_mappings[] = {
    {"a1", "M15T1G1664A_DDR3"},
    {"a1ne", "M15T1G1664A_DDR3"},
    {"a1nt", "W632GU6NG_DDR3"},
    {"t20", "M14D5121632A_DDR2"},  // Updated to match vendor reference configs
    {"t21", "W9751V6NG_DDR2"},
    {"t23", "M14D1G1664A_DDR2"},
    {"t30", "M14D1G1664A_DDR2"},
    {"t30a", "W631GU6NG_DDR3"},
    {"t30nl", "M14D1G1664A_DDR2"},
    {"t30x", "M14D1G1664A_DDR2"},
    {"t31", "M14D1G1664A_DDR2"},
    {"t31x", "M14D1G1664A_DDR2"},
    {"t31zx", "M14D1G1664A_DDR2"},
    {"t31a", "W631GU6NG_DDR3"},
    {"t31nl", "M14D1G1664A_DDR2"},
    {"t40", "W631GU6NG_DDR3"},
    {"t41", "H5TQ2G83CFR_DDR3"},
    {"t41n", "W631GU6NG_DDR3"},
};

static const size_t default_ddr_mappings_count = sizeof(default_ddr_mappings) / sizeof(default_ddr_mappings[0]);

// ============================================================================
// DDR CHIP FUNCTIONS
// ============================================================================

const ddr_chip_config_t* ddr_chip_config_get(const char *name) {
    if (!name) return NULL;

    for (size_t i = 0; i < ddr_chip_configs_count; i++) {
        if (thingino_strcasecmp(ddr_chip_configs[i].name, name) == 0) {
            return &ddr_chip_configs[i];
        }
    }

    return NULL;
}

const ddr_chip_config_t* ddr_chip_config_get_default(const char *processor_name) {
    if (!processor_name) return NULL;

    // Find default DDR for this processor
    for (size_t i = 0; i < default_ddr_mappings_count; i++) {
        if (thingino_strcasecmp(default_ddr_mappings[i].processor, processor_name) == 0) {
            return ddr_chip_config_get(default_ddr_mappings[i].default_ddr);
        }
    }

    // Fallback to M14D1G1664A_DDR2 if no specific mapping
    return ddr_chip_config_get("M14D1G1664A_DDR2");
}

const ddr_chip_config_t* ddr_chip_config_list(size_t *count) {
    if (count) *count = ddr_chip_configs_count;
    return ddr_chip_configs;
}

const ddr_chip_config_t** ddr_chip_config_list_by_type(uint32_t ddr_type, size_t *count) {
    static const ddr_chip_config_t* filtered[32];  // Max 32 chips per type
    size_t filtered_count = 0;

    for (size_t i = 0; i < ddr_chip_configs_count && filtered_count < 32; i++) {
        if (ddr_chip_configs[i].ddr_type == ddr_type) {
            filtered[filtered_count++] = &ddr_chip_configs[i];
        }
    }

    if (count) *count = filtered_count;
    return filtered;
}

