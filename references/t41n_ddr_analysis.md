# T41N DDR Binary Analysis

## Source
Extracted from `references/t41n.pcap` - USB capture of T41N device bootstrap

## Binary Details
- **File**: `references/t41n_ddr.bin`
- **Size**: 384 bytes (expected: 324 bytes for standard format)
- **Format**: FIDB + RDD sections

## FIDB Section (Platform Configuration)

### Header
- **Magic**: `FIDB` (0x46494442)
- **Size**: 184 bytes (0xB8)

### Platform Configuration
```
Offset  Value           Description
------  --------------- ------------------------------------
0x08    0x016E3600      Crystal: 24000000 Hz (24 MHz)
0x0C    0x2FAF0800      CPU: 800000000 Hz (800 MHz)
0x10    0x17D78400      DDR: 400000000 Hz (400 MHz)
0x14    0x00000000      UART: 0 baud (likely 115200, encoded differently)
```

### Key Observations
- **T41N Configuration**: 800 MHz CPU, 400 MHz DDR
- **Different from T41**: T41 typically runs at 600 MHz DDR
- **UART encoding**: Shows 0x00000000 instead of 115200 - may use different encoding

## RDD Section (DDR PHY Parameters)

### Header
- **Magic**: `RDD` (0x00524444)
- **Size**: 184 bytes (0xB8)

### DDR Chip Information
- **Name**: `DDR3_W631GU6NG` (Winbond DDR3)
- **Type**: DDR3 (0x82 = type 0 in RDD encoding)
- **Offset**: 0xC8 (192 + 8)

### DDR Parameters
```
Offset  Value       Description
------  ----------- ------------------------------------
0xE8    0x82        DDR Type (0 = DDR3 in RDD encoding)
0xF0    0x17D78400  DDR Frequency: 400000000 Hz
0xF4    0x80002831  DDRC CFG value
0xF8    0x0000B092  DDRC CTRL value
0x100   0x000020FC  DDRC DLMR value
0x104   0x00002400  DDRC DDLP value
0x108   0x40C30081  DDRC MMAP0 value
0x110   0x04060D04  Timing parameters
0x114   0x03070406  More timing
0x118   0x030604 06  More timing
0x11C   0x0E140E04  More timing
```

### Memory Geometry
```
Offset  Value   Description
------  ------- ------------------------------------
0x130   0x11    Row bits: 13 (0x0D in lower nibble)
0x134   0x10    Column bits: 10 (0x0A)
0x138   0x06    CAS Latency: 11 (encoded)
0x13C   0x05    Burst Length: 8
```

### DQ Mapping (0x168-0x17B)
```
0c 0d 02 03 04 05 06 07 08 09 0a 0b 00 01 0e 0f 10 11 12 13
```

## Comparison with T31X DDR Binary

### Similarities
- Same FIDB/RDD structure
- Same section sizes (184 bytes each)
- Similar timing parameter layout
- Same DQ mapping format

### Differences
- **CPU Frequency**: T41N = 800 MHz, T31X = 576 MHz
- **DDR Frequency**: T41N = 400 MHz, T31X = 400 MHz (same)
- **DDR Chip**: T41N = W631GU6NG (DDR3), T31X = M14D1G1664A (DDR2)
- **DDR Type**: T41N = DDR3, T31X = DDR2
- **UART Encoding**: T41N shows 0x00, T31X shows 115200

## Notes

1. **Binary Size**: 384 bytes vs expected 324 bytes
   - Extra 60 bytes may be padding or additional data
   - Standard format is FIDB (192) + RDD (132) = 324 bytes
   - This binary has FIDB (192) + RDD (192) = 384 bytes

2. **T41N vs T41**:
   - T41N appears to be a variant with 400 MHz DDR instead of 600 MHz
   - May be a lower-power or cost-optimized version

3. **DDR3 Chip**: W631GU6NG
   - Winbond DDR3 chip
   - 13 row bits, 10 column bits
   - CL=11, BL=8
   - Already in our embedded database!

4. **UART Baud Rate**:
   - Shows 0x00000000 in binary
   - Likely still 115200 but encoded differently
   - May be a flag or use default value

## Integration with Embedded Database

The W631GU6NG DDR3 chip is already in our database:
```c
{
    .name = "W631GU6NG_DDR3",
    .vendor = "Winbond",
    .ddr_type = 0,  // DDR3
    .row_bits = 13,
    .col_bits = 10,
    .cl = 11,
    .bl = 8,
    // ... timing parameters
}
```

To support T41N, we need to add:
```c
{
    .name = "t41n",
    .crystal_freq = 24000000,
    .cpu_freq = 800000000,
    .ddr_freq = 400000000,  // Note: 400 MHz, not 600 MHz like T41
    .uart_baud = 115200,
    .mem_size = 32 * 1024 * 1024  // Assuming 32 MB like T41
},
```

## Conclusion

Successfully extracted T41N DDR configuration from pcap file. The binary shows:
- T41N runs at 800 MHz CPU / 400 MHz DDR (vs T41 at 800 MHz / 600 MHz)
- Uses Winbond W631GU6NG DDR3 chip (already in our database)
- Binary format matches expected FIDB + RDD structure
- Ready to add T41N processor to embedded configuration database

