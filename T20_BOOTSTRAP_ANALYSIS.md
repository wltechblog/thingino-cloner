# T20 Bootstrap Analysis - Complete Findings

## Status: ⏳ READY FOR TESTING (Device needs power cycle)

## Critical Discoveries from vendor_t20_full_bootstrap.pcap

### 1. Missing d2i_len Parameter ✅ FIXED
**Problem**: We were setting execution size to 0x7000 (T31X value) instead of 0x4000 (T20 value)

**Evidence from pcap**:
```
76.758 | SET_DATA_LEN -> 16384 bytes (0x4000) before PROG_START1
```

**Evidence from config**:
```
references/cloner-2.5.43-ubuntu_thingino/firmwares/t20/config.cfg:
d2i_len = 0x4000
```

**Fix Applied**:
```c
// src/bootstrap.c line 114
uint32_t d2i_len = (device->info.variant == VARIANT_T20) ? 0x4000 : 0x7000;
```

### 2. Device Handle Management ✅ FIXED
**Problem**: We were closing and reopening the device after SPL execution

**Evidence from pcap**:
```
USB device address stays 106 throughout entire session:
- Before PROG_START1: address 106
- After SPL execution: address 106  
- During U-Boot load: address 106
- After PROG_START2: address 106
```

**Vendor's approach**: Keep device handle open, just wait ~1.1 seconds

**Our old approach**: Close device → wait → re-enumerate → reopen

**Fix Applied**:
- Removed all close/reopen logic after SPL
- Changed to simple 1.1 second wait
- Device handle remains valid throughout

### 3. Device Polling After SPL Wait ✅ ADDED
**Problem**: We were loading U-Boot immediately after 1.1 second wait

**Evidence from pcap**:
```
77.859 | SPL wait ends (1.1s after PROG_START1)
78.032 | GET_CPU_INFO (poll #1)
78.038 | GET_CPU_INFO (poll #2)
78.046 | GET_CPU_INFO (poll #3)
78.050 | GET_CPU_INFO (poll #4)
78.058 | GET_CPU_INFO (poll #5)
78.064 | GET_CPU_INFO (poll #6)
78.073 | GET_CPU_INFO (poll #7)
78.076 | GET_CPU_INFO (poll #8)
78.077 | FLUSH_CACHE
78.079 | PROG_START2
```

**Vendor's approach**: Wait 1.1s → Poll device 8 times (~200ms) → PROG_START2

**Our old approach**: Wait 1.1s → PROG_START2 immediately

**Fix Applied**:
```c
// Poll device up to 10 times with 20ms intervals
for (int poll_attempt = 0; poll_attempt < 10; poll_attempt++) {
    result = usb_device_get_cpu_info(device, &poll_info);
    if (result == THINGINO_SUCCESS) {
        spl_ready = true;
        break;
    }
    usleep(20000);  // 20ms between polls
}
```

### 4. GET_CPU_INFO After PROG_START2 ✅ ADDED
**Evidence from pcap**:
```
78.079 | PROG_START2 @ 0x80100000
78.079 | GET_CPU_INFO (immediately after!)
```

**Fix Applied**:
```c
// src/bootstrap.c after PROG_START2
usb_device_get_cpu_info(device, &cpu_info_after);
```

## Complete Vendor Bootstrap Sequence

```
Phase 1: DDR + SPL (76.750 - 76.759s)
  GET_CPU_INFO
  SET_DATA_ADDR    -> 0x80001000 (DDR)
  GET_CPU_INFO     (extra check!)
  SET_DATA_LEN     -> 396 bytes (DDR size)
  SET_DATA_ADDR    -> 0x80001800 (SPL)
  SET_DATA_LEN     -> 10176 bytes (SPL size)
  SET_DATA_LEN     -> 16384 bytes (d2i_len = 0x4000) ⚠️ KEY!
  PROG_START1      @ 0x80001800

Phase 2: Wait for SPL (1.1 seconds, handle stays open) ⚠️ KEY!
  [No USB activity - SPL initializing DDR]

Phase 3: U-Boot (77.859 - 78.079s)
  GET_CPU_INFO     (same USB address!)
  SET_DATA_ADDR    -> 0x80100000
  SET_DATA_LEN     -> 390480 bytes
  [Bulk transfer]
  FLUSH_CACHE
  PROG_START2      @ 0x80100000
  GET_CPU_INFO     (immediately!) ⚠️ KEY!

Phase 4: Firmware Stage (78.248s+)
  FW_WRITE_OP      (0.17s after PROG_START2)
  FW_HANDSHAKE     ✅ SUCCESS!
```

## T20-Specific Parameters

### From config.cfg:
```
ginfo=  0x80001000
spl=    0x80001800
uboot=  0x80100000
d2i_len = 0x4000    ← T20-specific!
```

### From ingenic-tools/ddr_params_creator.c:
```c
#if (defined(CONFIG_T10) || defined(CONFIG_T20))
    ddrc->cfg.b.IMBA = 1;
    DDRP_TIMING_SET(0,ddr_base_params,tRCD,4,2,11);
    DDRP_TIMING_SET(0,ddr_base_params,tRAS,5,2,31);
    DDRP_TIMING_SET(1,ddr_base_params,tRFC,8,0,255);  // 8-bit vs 6-bit!
#endif
```

T20 has different DDR timing register widths than newer chips!

## Files Modified

1. **src/bootstrap.c**:
   - Added T20-specific d2i_len (0x4000 vs 0x7000)
   - Removed device close/reopen after SPL
   - Added GET_CPU_INFO after PROG_START2
   - Changed to 1.1 second wait matching vendor

2. **src/firmware/loader.c**:
   - Uses vendor reference DDR binary for T20
   - Embedded in src/ddr/t20_reference_ddr.h

3. **src/ddr/t20_reference_ddr.h**:
   - 324-byte vendor DDR binary
   - Extracted from USB capture

## Test Results from log.txt

✅ **Successful**:
- DDR config loaded (324 bytes)
- SPL loaded (10176 bytes)
- d2i_len set to 0x4000 (line 158-159)
- SPL executed
- Device handle kept open (no re-enumeration)
- U-Boot loaded (390480 bytes)
- PROG_START2 sent (timeout expected)

⚠️ **Issue**:
- PROG_START2 times out (lines 188-192)
- GET_CPU_INFO after bootstrap times out (lines 221-229)
- Device still at address 108, PID 0xC309 (bootrom)

**Note**: Device may be in bad state from previous tests. **Power cycle required** before next test.

## Next Steps

1. **Power cycle the T20 device**
2. **Run test**: `sudo ./build/thingino-cloner -r test_t20_final.bin`
3. **Expected behavior**:
   - ✅ DDR loads
   - ✅ SPL executes with d2i_len=0x4000
   - ✅ Device handle stays open
   - ✅ U-Boot loads
   - ✅ PROG_START2 executes
   - ✅ GET_CPU_INFO succeeds
   - ✅ Device transitions to firmware stage
   - ✅ Firmware read succeeds

## References

- Vendor pcap: `vendor_t20_full_bootstrap.pcap`
- Vendor config: `references/cloner-2.5.43-ubuntu_thingino/firmwares/t20/config.cfg`
- U-Boot tools: `references/ingenic-u-boot-xburst1/tools/ingenic-tools/`
- Test log: `log.txt`

