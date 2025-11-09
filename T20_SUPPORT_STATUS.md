# T20 Support Status

## Summary

T20 support has been partially implemented. The device successfully loads DDR configuration and SPL, but U-Boot does not boot to firmware stage.

## What Works ✅

1. **T20 Detection**: Device correctly identified as T20 from CPU magic
2. **DDR Configuration**: Using vendor's reference DDR binary (324 bytes)
3. **SPL Loading**: SPL loads and executes successfully
4. **Device Re-enumeration**: Device re-enumerates after SPL execution
5. **U-Boot Transfer**: U-Boot binary transfers successfully

## What Doesn't Work ❌

1. **U-Boot Boot**: Device doesn't reach firmware stage after U-Boot load
2. **Firmware Stage Detection**: Device remains in bootrom mode after bootstrap

## Technical Details

### DDR Binary Format

T20 uses a **different DDR binary format** than T31X:

- **FIDB Section** (192 bytes): ✅ Identical format - platform configuration
- **RDD Section** (132 bytes): ❌ Completely different format

#### T31X RDD Format (124 bytes data):
- Simplified parameter format
- Direct timing values in cycles
- Standard DQ mapping

#### T20 RDD Format (124 bytes data, but header claims 196):
- Appears to contain raw DDRC/DDRP register values
- Different byte layout
- Values scattered at different offsets
- Size field in header says 196 bytes, but only 124 bytes present

### Current Workaround

Using vendor's reference DDR binary embedded in `src/ddr/t20_reference_ddr.h`:
- Extracted from USB capture of vendor's cloner tool
- 324 bytes total (192 FIDB + 8 RDD header + 124 RDD data)
- Works correctly for DDR initialization

### Files Modified

1. **`src/firmware/loader.c`**:
   - Added `#include "t20_reference_ddr.h"`
   - Modified `firmware_generate_ddr_config()` to use vendor binary for T20
   - Added workaround comment explaining the format difference

2. **`src/ddr/t20_reference_ddr.h`** (new file):
   - Embedded vendor's DDR binary as C array
   - 324 bytes extracted from USB capture

3. **`src/ddr/ddr_binary_builder.c`**:
   - Added VARIANT_T20 (case 0) to `ddr_get_platform_config_by_variant()`
   - Maps T20 to correct platform config (800 MHz CPU / 400 MHz DDR)

4. **`src/ddr/ddr_config_database.c`**:
   - Updated T20 processor config: 800 MHz CPU, 400 MHz DDR (was 600/200)
   - Fixed M14D5121632A_DDR2 chip config to match vendor reference

5. **`src/bootstrap.c`**:
   - Added code to preserve T20 variant during re-enumeration
   - Fixed fast enumeration hardcoding T31X

## Test Results

### Latest Test (with vendor DDR binary):
```
✓ DDR configuration loaded (324 bytes)
✓ SPL loaded (10,176 bytes)
✓ SPL execution started
✓ Device re-enumerated
✓ U-Boot loaded (390,480 bytes)
✓ Bootstrap sequence completed
✗ Device not found in firmware stage (still in bootrom mode)
```

### USB Device State:
- Before bootstrap: `Bus 003 Device 104: ID a108:c309` (bootrom)
- After bootstrap: `Bus 003 Device 104: ID a108:c309` (still bootrom!)

## Possible Causes

1. ~~**U-Boot Compatibility**~~: ✅ Firmware binaries verified - same as vendor's working binaries
2. ~~**Memory Addressing**~~: ✅ Addresses verified correct (0x80001000 DDR, 0x80001800 SPL, 0x80100000 U-Boot)
3. **Boot Timeout**: Device might need more time to boot (unlikely - vendor tool works quickly)
4. ~~**DDR Initialization**~~: ✅ DDR works - device successfully re-enumerates after SPL
5. ~~**SPL/U-Boot Mismatch**~~: ✅ Binaries are from same source as vendor tool
6. **Protocol Sequence**: Our USB protocol sequence might differ from vendor's
7. **Timing Issues**: We might not be waiting long enough between steps
8. **Cache Flush**: Cache flush might not be working correctly for T20
9. **ProgStage2 Execution**: The VR_PROGRAM_START2 command might need different parameters for T20

## Next Steps

### Short Term (Workarounds):
1. ✅ Use vendor's DDR binary (implemented)
2. ⏳ Increase boot timeout and retry detection
3. ⏳ Test with vendor's SPL/U-Boot binaries directly
4. ⏳ Add more detailed logging during bootstrap

### Long Term (Proper Fix):
1. ⏳ Reverse-engineer complete T20 RDD format from vendor binary
2. ⏳ Create T20-specific DDR binary builder
3. ⏳ Add processor-specific format detection
4. ⏳ Implement proper T20 RDD generation

## Vendor DDR Binary Analysis

### Captured Data:
- **Source**: USB capture of vendor's cloner tool
- **File**: `vendor_ddr_t20.bin` (324 bytes)
- **Extraction**: `extract_ddr_from_pcap.py`

### Key Findings:
- FIDB section identical to our generated version
- RDD section has values at different offsets:
  - ROW bits at 0x16 (vs 0x1E in T31X)
  - COL bits at 0x17 (vs 0x1F in T31X)
  - CL at 0x18 (vs 0x1C in T31X)
  - tRFC at 0x54 (vs 0x24 in T31X) - much later!

## Files for Reference

- `vendor_ddr_t20.bin` - Vendor's DDR binary (324 bytes)
- `capture_vendor_t20.sh` - USB capture script
- `extract_ddr_from_pcap.py` - DDR extraction tool
- `CAPTURE_VENDOR_T20.md` - Capture guide
- `test_t20_vendor_ddr_log.txt` - Latest test log

## Recommendations

### Immediate Next Steps:
1. **Capture vendor's complete bootstrap sequence** - Use tcpdump to capture the full USB traffic when vendor tool successfully bootstraps T20
2. **Compare protocol sequences** - Analyze differences in:
   - VR_SET_DATA_ADDRESS / VR_SET_DATA_LENGTH timing
   - VR_FLUSH_CACHE usage
   - VR_PROGRAM_START2 parameters
   - Wait times between steps
3. **Test with increased timeouts** - Try waiting longer after ProgStage2 before checking device stage
4. **Add detailed USB protocol logging** - Log every USB transaction to identify where our sequence diverges

### Debugging Approach:
```bash
# Terminal 1: Start USB capture
sudo tcpdump -i usbmon0 -w vendor_t20_bootstrap.pcap

# Terminal 2: Run vendor tool
cd references/cloner-2.5.43-ubuntu_thingino
sudo ./cloner --config configs/t20/t20_sfc_nor_reader_16MB.cfg

# Terminal 1: Stop capture (Ctrl+C)

# Analyze the capture
tshark -r vendor_t20_bootstrap.pcap -Y "usb.src == host" -T fields \
  -e frame.number -e usb.setup.bRequest -e usb.setup.wValue -e usb.setup.wIndex
```

## Conclusion

T20 support is **partially working**. The DDR and SPL stages work correctly with the vendor's DDR binary, but U-Boot doesn't boot to firmware stage.

**Root Cause**: The issue is NOT with the firmware binaries or memory addresses (all verified correct), but likely with the **USB protocol sequence** or **timing** used to load and execute U-Boot.

**Evidence**:
- ✅ DDR binary works (vendor's reference)
- ✅ SPL loads and executes (device re-enumerates)
- ✅ U-Boot transfers successfully (no errors)
- ❌ U-Boot doesn't execute (device stays in bootrom mode)
- ✅ Vendor's tool works with same binaries

**Next Action**: Capture and compare the vendor's complete USB protocol sequence to identify the difference.

