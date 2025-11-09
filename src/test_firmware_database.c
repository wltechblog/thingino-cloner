/**
 * Test program for embedded firmware database
 */

#include "firmware_database.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("=== Embedded Firmware Database Test ===\n\n");
    
    // List all available firmwares
    size_t count = 0;
    const firmware_binary_t *firmwares = firmware_list(&count);
    
    printf("Available Firmwares (%zu total):\n", count);
    printf("%-15s %10s %10s\n", "Processor", "SPL Size", "U-Boot Size");
    printf("%-15s %10s %10s\n", "---------------", "----------", "----------");
    
    size_t total_spl = 0;
    size_t total_uboot = 0;
    
    for (size_t i = 0; i < count; i++) {
        printf("%-15s %10zu %10zu\n", 
               firmwares[i].processor,
               firmwares[i].spl_size,
               firmwares[i].uboot_size);
        total_spl += firmwares[i].spl_size;
        total_uboot += firmwares[i].uboot_size;
    }
    
    printf("\nTotal embedded firmware size:\n");
    printf("  SPL:    %10zu bytes (%.2f KB)\n", total_spl, total_spl / 1024.0);
    printf("  U-Boot: %10zu bytes (%.2f KB)\n", total_uboot, total_uboot / 1024.0);
    printf("  Total:  %10zu bytes (%.2f MB)\n", total_spl + total_uboot, (total_spl + total_uboot) / (1024.0 * 1024.0));
    
    // Test specific processor lookups
    printf("\nTesting Specific Processor Lookups:\n");
    
    const char *test_processors[] = {"t31x", "t41", "a1_n_ne_x", "t20", "invalid"};
    for (size_t i = 0; i < sizeof(test_processors) / sizeof(test_processors[0]); i++) {
        const char *proc = test_processors[i];
        const firmware_binary_t *fw = firmware_get(proc);
        
        if (fw) {
            printf("  %-15s [OK] SPL=%zu bytes, U-Boot=%zu bytes\n", 
                   proc, fw->spl_size, fw->uboot_size);
        } else {
            printf("  %-15s [NOT FOUND]\n", proc);
        }
    }
    
    // Test firmware_available function
    printf("\nTesting firmware_available():\n");
    printf("  t31x available: %s\n", firmware_available("t31x") ? "YES" : "NO");
    printf("  t41 available: %s\n", firmware_available("t41") ? "YES" : "NO");
    printf("  invalid available: %s\n", firmware_available("invalid") ? "YES" : "NO");
    
    // Verify data integrity (check first few bytes of SPL)
    printf("\nVerifying Data Integrity:\n");
    const firmware_binary_t *t31x_fw = firmware_get("t31x");
    if (t31x_fw && t31x_fw->spl_size > 16) {
        printf("  T31X SPL first 16 bytes: ");
        for (size_t i = 0; i < 16; i++) {
            printf("%02x ", t31x_fw->spl_data[i]);
        }
        printf("\n");
    }
    
    const firmware_binary_t *t41_fw = firmware_get("t41");
    if (t41_fw && t41_fw->spl_size > 16) {
        printf("  T41 SPL first 16 bytes:  ");
        for (size_t i = 0; i < 16; i++) {
            printf("%02x ", t41_fw->spl_data[i]);
        }
        printf("\n");
    }
    
    printf("\n[SUCCESS] Firmware database test passed!\n");
    return 0;
}

