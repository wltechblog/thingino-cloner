#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main() {
    FILE *fp = fopen("references/ddr_extracted.bin", "rb");
    if (!fp) {
        printf("Failed to open reference binary\n");
        return 1;
    }
    
    uint8_t reference[324];
    size_t bytes_read = fread(reference, 1, 324, fp);
    fclose(fp);

    if (bytes_read != 324) {
        printf("Warning: only read %zu bytes instead of 324\n", bytes_read);
    }
    
    printf("=== DDRC Register Analysis (from DDRP section) ===\n\n");
    printf("TXX mapping copies DDRC values to DDRP section:\n");
    printf("DDRP[0x04-0x3B] = obj[0x7c-0xc4] (DDRC registers)\n\n");
    
    // DDRP starts at file offset 0xC4
    // DDRP[0x04] = obj[0x7c], DDRP[0x08] = obj[0x80], etc.
    
    printf("Offset  File     DDRP     Obj      Value    Description\n");
    printf("------  ----     ----     ---      -----    -----------\n");
    
    // Map from TXX ddr_convert_param
    uint16_t obj_offsets[] = {
        0x7c, 0x80, 0x8c, 0x84, 0x90, 0x94, 0x88, 0xac,
        0xb0, 0xb4, 0xb8, 0xbc, 0xc0, 0xc4
    };
    
    const char *descriptions[] = {
        "Unknown",
        "Unknown", 
        "Unknown",
        "Unknown",
        "Unknown",
        "Unknown",
        "tREFI config",
        "tRTP cycles",
        "tRC cycles",
        "tRP cycles",
        "tRTR/tRFC bits",
        "tRTP-1",
        "Unknown",
        "Enable flag"
    };
    
    for (int i = 0; i < 14; i++) {
        uint16_t ddrp_offset = 0x04 + (i * 4);
        uint16_t file_offset = 0xC4 + ddrp_offset;
        uint32_t value = *(uint32_t *)(reference + file_offset);
        
        printf("0x%02x    0x%03x    0x%02x     0x%03x    0x%08x    %s\n",
               i*4, file_offset, ddrp_offset, obj_offsets[i], value, descriptions[i]);
    }
    
    printf("\n=== Detailed Byte Analysis ===\n\n");
    
    // Analyze obj[0xac-0xc4] which are the main DDRC timing registers
    printf("obj[0xac] = 0x%02x (tRTP cycles)\n", reference[0xC4 + 0x1C]);
    printf("obj[0xad] = 0x%02x (tWR cycles)\n", reference[0xC4 + 0x1C + 1]);
    printf("obj[0xae] = 0x%02x (tWL+CL-1+width/2)\n", reference[0xC4 + 0x1C + 2]);
    printf("obj[0xaf] = 0x%02x (tWR DDR2)\n", reference[0xC4 + 0x1C + 3]);
    
    printf("\nobj[0xb0] = 0x%02x (tRC cycles)\n", reference[0xC4 + 0x24]);
    printf("obj[0xb1] = 0x%02x (tRAS cycles)\n", reference[0xC4 + 0x24 + 1]);
    printf("obj[0xb2] = 0x%02x (tCCD cycles)\n", reference[0xC4 + 0x24 + 2]);
    printf("obj[0xb3] = 0x%02x (tWTR cycles)\n", reference[0xC4 + 0x24 + 3]);
    
    printf("\nobj[0xb4] = 0x%02x (tRP cycles)\n", reference[0xC4 + 0x28]);
    printf("obj[0xb5] = 0x%02x (tRRD cycles)\n", reference[0xC4 + 0x28 + 1]);
    printf("obj[0xb6] = 0x%02x (tRCD cycles)\n", reference[0xC4 + 0x28 + 2]);
    printf("obj[0xb7] = 0x%02x (bits [6:3]=0x4)\n", reference[0xC4 + 0x28 + 3]);
    
    printf("\nobj[0xb8] = 0x%02x (tRFC/tRTR bits)\n", reference[0xC4 + 0x2C]);
    printf("obj[0xb9] = 0x%02x (tRFC/8-1)\n", reference[0xC4 + 0x2C + 1]);
    printf("obj[0xba] = 0x%02x (tWTR+1, bits[6:5]=3)\n", reference[0xC4 + 0x2C + 2]);
    printf("obj[0xbb] = 0x%02x (tRTW)\n", reference[0xC4 + 0x2C + 3]);
    
    printf("\nobj[0xbc] = 0x%02x (tRTP-1)\n", reference[0xC4 + 0x30]);
    printf("obj[0xbd] = 0x%02x (tRC-3)\n", reference[0xC4 + 0x30 + 1]);
    printf("obj[0xbe] = 0x%02x (data width: 4 or 6)\n", reference[0xC4 + 0x30 + 2]);
    printf("obj[0xbf] = 0x%02x (constant 0xff)\n", reference[0xC4 + 0x30 + 3]);
    
    printf("\nobj[0xc0] = 0x%02x (constant 5)\n", reference[0xC4 + 0x34]);
    printf("obj[0xc1] = 0x%02x (constant 5)\n", reference[0xC4 + 0x34 + 1]);
    printf("obj[0xc2] = 0x%02x (tRRD cycles)\n", reference[0xC4 + 0x34 + 2]);
    printf("obj[0xc3] = 0x%02x (max(tRAS,tRC)/4)\n", reference[0xC4 + 0x34 + 3]);
    
    return 0;
}

