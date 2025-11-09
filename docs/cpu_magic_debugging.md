# CPU Magic Debugging Guide

## Overview

The CPU magic is an 8-byte identifier returned by the Ingenic bootrom that indicates:
1. **Device stage**: Whether the device is in bootrom or firmware stage
2. **Processor variant**: Which specific Ingenic SoC is being used (T31, T41, etc.)

## Understanding CPU Magic Values

### Bootrom Stage
When a device is in **bootrom stage** (the initial state after power-on or reset), the CPU magic typically contains the processor model with spaces between characters:

- **T31 variants**: `54 20 33 31 20 56 20 XX` → "T 3 1 V " (T31ZX)
- **T31 variants**: `54 20 33 31 20 XX XX XX` → "T 3 1 " (T31/T31X)
- **T41 variants**: `54 20 34 31 20 4E 20 XX` → "T 4 1 N " (T41N)
- **T41 variants**: `54 20 34 31 20 XX XX XX` → "T 4 1 " (T41)

The spaces (0x20) are intentional and part of the bootrom's response format.

### Firmware Stage
When a device is in **firmware stage** (after SPL/U-Boot has been loaded), the CPU magic starts with "Boot":

- **Firmware stage**: `42 6F 6F 74 XX XX XX XX` → "Boot..." (various suffixes)

## Improved Debugging Output

The code now displays three pieces of information when checking CPU magic:

1. **Raw hex bytes**: Shows the actual 8 bytes received from the device
   ```
   CPU magic (raw hex): 54 20 33 31 20 56 20 00
   ```

2. **ASCII representation**: Shows the printable characters
   ```
   Current device stage: bootrom (CPU magic: T 3 1 V )
   ```

3. **Detected variant**: Shows which processor variant was detected
   ```
   Detected processor variant: t31zx (from magic: 'T 3 1 V ')
   ```

## Interpreting Your T41N Results

If you're testing with a T41N system and seeing "T 3 1 V ", this indicates:

### Scenario 1: Device is Actually T31ZX
The device might actually be a T31ZX, not a T41N. Check:
- Physical chip markings on the SoC
- Device documentation
- USB VID/PID (should be 0x601A:0xC309 for T31/T41 bootrom)

### Scenario 2: Incorrect Device Identification
You might have multiple devices connected. Verify:
- Only one Ingenic device is connected
- The correct device is being selected
- USB bus/address matches your target device

### Scenario 3: Device in Unexpected State
The device might be in a transitional or error state. Try:
- Power cycling the device completely
- Checking for firmware corruption
- Verifying the device is properly entering bootrom mode

## Expected T41N CPU Magic

For a genuine T41N device in bootrom stage, you should see:
```
CPU magic (raw hex): 54 20 34 31 20 4E 20 XX
Current device stage: bootrom (CPU magic: T 4 1 N )
Detected processor variant: t41 (from magic: 'T41N')
```

Note: The variant detection currently maps "T41N" to `VARIANT_T41` since T41N is a variant of T41 with different DDR frequency (400 MHz vs 600 MHz).

## Stage Detection Logic

The code determines device stage as follows:

```c
// Bootrom stage: CPU magic does NOT start with "Boot"
if (strncmp(cpu_str, "Boot", 4) != 0) {
    stage = STAGE_BOOTROM;
}

// Firmware stage: CPU magic starts with "Boot"
if (strncmp(cpu_str, "Boot", 4) == 0) {
    stage = STAGE_FIRMWARE;
}
```

This is **correct behavior** - if you see "T 3 1 V " or "T 4 1 N ", the device is genuinely in bootrom stage and needs to be bootstrapped.

## Next Steps

1. **Verify the raw hex bytes** - This will definitively show what the device is reporting
2. **Check physical chip markings** - Confirm the actual SoC model
3. **Compare with known good captures** - See `references/t41n.pcap` for T41N USB capture
4. **Enable DEBUG mode** - Rebuild with DEBUG=1 to see detailed USB communication

## Related Files

- `src/usb/device.c` - CPU info retrieval and parsing
- `src/utils.c` - Variant detection from CPU magic
- `src/bootstrap.c` - Bootstrap process and stage checking
- `references/t41n_ddr_analysis.md` - T41N DDR configuration analysis

