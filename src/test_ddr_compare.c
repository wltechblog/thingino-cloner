#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Helper function to load binary file
static uint8_t* load_binary_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        printf("[ERROR] Cannot open file: %s\n", path);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    
    long file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return NULL;
    }
    
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    
    uint8_t *buffer = (uint8_t *)malloc(file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    size_t bytes_read = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (bytes_read != (size_t)file_size) {
        free(buffer);
        return NULL;
    }
    
    *size = bytes_read;
    return buffer;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <reference.bin> <generated.bin>\n", argv[0]);
        printf("\nCompare two DDR binaries and show differences\n");
        return 1;
    }
    
    printf("=== DDR Binary Comparison Tool ===\n\n");
    
    // Load reference binary
    size_t ref_size = 0;
    uint8_t *reference = load_binary_file(argv[1], &ref_size);
    if (!reference) {
        printf("[ERROR] Failed to load reference: %s\n", argv[1]);
        return 1;
    }
    printf("[OK] Loaded reference: %s (%zu bytes)\n", argv[1], ref_size);
    
    // Load generated binary
    size_t gen_size = 0;
    uint8_t *generated = load_binary_file(argv[2], &gen_size);
    if (!generated) {
        printf("[ERROR] Failed to load generated: %s\n", argv[2]);
        free(reference);
        return 1;
    }
    printf("[OK] Loaded generated: %s (%zu bytes)\n", argv[2], gen_size);
    
    if (ref_size != gen_size) {
        printf("\n[WARNING] Size mismatch: reference=%zu, generated=%zu\n", ref_size, gen_size);
    }
    
    size_t min_size = ref_size < gen_size ? ref_size : gen_size;
    
    // Count differences
    size_t total_diffs = 0;
    for (size_t i = 0; i < min_size; i++) {
        if (reference[i] != generated[i]) {
            total_diffs++;
        }
    }
    
    printf("\n");
    if (total_diffs == 0 && ref_size == gen_size) {
        printf("✅ PERFECT MATCH! Binaries are identical!\n");
        free(reference);
        free(generated);
        return 0;
    }
    
    printf("Found %zu byte differences (%.1f%% match)\n\n", 
           total_diffs, 100.0 * (min_size - total_diffs) / min_size);
    
    // Analyze sections
    printf("=== Section Analysis ===\n\n");
    
    // FIDB section (0x00-0xC7)
    size_t fidb_diffs = 0;
    for (size_t i = 0; i < 0xC8 && i < min_size; i++) {
        if (reference[i] != generated[i]) {
            fidb_diffs++;
        }
    }
    printf("FIDB (0x00-0xC7): %zu differences\n", fidb_diffs);
    
    // RDD section (0xC8-0x143)
    size_t rdd_diffs = 0;
    for (size_t i = 0xC8; i < 0x144 && i < min_size; i++) {
        if (reference[i] != generated[i]) {
            rdd_diffs++;
        }
    }
    printf("RDD  (0xC8-0x143): %zu differences\n", rdd_diffs);
    
    if (rdd_diffs == 0) {
        printf("\n🎉 RDD section is PERFECT! 🎉\n");
    }
    
    // Show key fields
    printf("\n=== Key Fields ===\n\n");
    
    if (min_size >= 0xCC) {
        uint32_t ref_type = *(uint32_t*)(reference + 0xCC);
        uint32_t gen_type = *(uint32_t*)(generated + 0xCC);
        printf("DDR Type (0xCC):     ref=%u, gen=%u %s\n", 
               ref_type, gen_type, ref_type == gen_type ? "✅" : "❌");
    }
    
    if (min_size >= 0xD8) {
        uint32_t ref_freq = *(uint32_t*)(reference + 0xD8);
        uint32_t gen_freq = *(uint32_t*)(generated + 0xD8);
        printf("Frequency (0xD8):    ref=%u (%.0f MHz), gen=%u (%.0f MHz) %s\n",
               ref_freq, ref_freq * 0.1, gen_freq, gen_freq * 0.1,
               ref_freq == gen_freq ? "✅" : "❌");
    }
    
    if (min_size >= 0xE8) {
        printf("Geometry (0xE4-0xE7):\n");
        printf("  RL/WL (0xE4): ref=%u, gen=%u %s\n",
               reference[0xE4], generated[0xE4],
               reference[0xE4] == generated[0xE4] ? "✅" : "❌");
        printf("  RL/WL (0xE5): ref=%u, gen=%u %s\n",
               reference[0xE5], generated[0xE5],
               reference[0xE5] == generated[0xE5] ? "✅" : "❌");
        printf("  ROW   (0xE6): ref=%u, gen=%u %s\n",
               reference[0xE6], generated[0xE6],
               reference[0xE6] == generated[0xE6] ? "✅" : "❌");
        printf("  COL   (0xE7): ref=%u, gen=%u %s\n",
               reference[0xE7], generated[0xE7],
               reference[0xE7] == generated[0xE7] ? "✅" : "❌");
    }
    
    // Show first 20 differences
    printf("\n=== First 20 Differences ===\n\n");
    printf("Offset   Ref  Gen  Section\n");
    printf("------   ---  ---  -------\n");
    
    size_t shown = 0;
    for (size_t i = 0; i < min_size && shown < 20; i++) {
        if (reference[i] != generated[i]) {
            const char *section = i < 0xC8 ? "FIDB" : "RDD ";
            printf("0x%04zx   %3u  %3u  %s\n", i, reference[i], generated[i], section);
            shown++;
        }
    }
    
    if (total_diffs > 20) {
        printf("... and %zu more differences\n", total_diffs - 20);
    }
    
    free(reference);
    free(generated);
    
    return total_diffs > 0 ? 1 : 0;
}

