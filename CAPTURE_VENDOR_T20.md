# Capturing Vendor T20 DDR Binary

This guide explains how to capture the DDR binary that the vendor's cloner tool sends to a T20 device, so we can compare it with our generated binary.

## Prerequisites

```bash
# Install required tools
sudo apt-get install tcpdump tshark wireshark
```

## Step 1: Start USB Capture

In **Terminal 1**, run the capture script:

```bash
sudo ./capture_vendor_t20.sh
```

This will:
- Start capturing all USB traffic
- Wait for you to run the vendor tool
- Save the capture to `vendor_t20_analysis/vendor_t20_capture_TIMESTAMP.pcap`

## Step 2: Run Vendor Tool

In **Terminal 2**, run the vendor's cloner tool to read from the T20 device:

```bash
cd references/cloner-2.5.43-ubuntu_thingino
sudo ./cloner --config configs/t20/t20_sfc_nor_reader_16MB.cfg
```

Let it complete the read operation (or at least get past the DDR/SPL loading phase).

## Step 3: Stop Capture

Back in **Terminal 1**, press `Ctrl+C` to stop the capture.

The script will automatically try to extract the DDR binary.

## Step 4: Extract DDR Binary (if needed)

If the automatic extraction didn't work, use the Python script:

```bash
python3 extract_ddr_from_pcap.py vendor_t20_analysis/vendor_t20_capture_*.pcap vendor_ddr_t20.bin
```

This will:
- Parse the pcap file
- Find the 324-byte DDR binary (looks for "FIDB" marker)
- Extract and analyze it
- Save to `vendor_ddr_t20.bin`

## Step 5: Compare with Our Generated Binary

Compare the vendor's DDR binary with ours:

```bash
# Our generated binary (from running our tool)
xxd /tmp/t20_ddr_debug.bin > our_ddr.hex

# Vendor's binary (from capture)
xxd vendor_ddr_t20.bin > vendor_ddr.hex

# Compare side-by-side
diff -y our_ddr.hex vendor_ddr.hex | less
```

Or use a visual diff tool:

```bash
# Meld (GUI)
meld our_ddr.hex vendor_ddr.hex

# Or vimdiff
vimdiff our_ddr.hex vendor_ddr.hex
```

## Step 6: Analyze Differences

The Python extraction script will show you the decoded values:
- Platform config (CPU/DDR frequencies, crystal, UART, memory size)
- DDR type and geometry (row/col bits, CL, BL)
- Timing parameters (tRAS, tRC, tRCD, tRP, tRFC, tRTP, tFAW, tRRD, tWTR)

Look for differences in:
1. **Frequencies**: Should be 800 MHz CPU / 400 MHz DDR
2. **Geometry**: Should be 13 row bits, 10 col bits, CL=7, BL=8
3. **Timing**: Compare cycle values - these are the most likely to differ
4. **Unknown fields**: There are some hardcoded values we reverse-engineered that might be wrong for T20

## Alternative: Manual Analysis with Wireshark

If you prefer a GUI:

```bash
wireshark vendor_t20_analysis/vendor_t20_capture_*.pcap
```

Filter for USB bulk transfers:
```
usb.transfer_type == 0x03 && usb.endpoint_address.direction == 0
```

Look for a 324-byte transfer that starts with "FIDB" (46 49 44 42 in hex).

## Troubleshooting

### "tshark not found"
```bash
sudo apt-get install tshark
```

### "Permission denied" on usbmon
```bash
sudo modprobe usbmon
sudo chmod a+r /dev/usbmon*
```

### No DDR binary found
- Make sure the capture includes the initial DDR config upload
- The DDR binary is sent very early in the process (before SPL)
- Try capturing from the moment you plug in the device

### Multiple candidates found
- The script will use the first one
- You can manually check each candidate in the pcap file
- The correct one should be sent right before the SPL binary

## Next Steps

Once you have both binaries:
1. Compare them byte-by-byte to find differences
2. Update our DDR generation code to match the vendor's values
3. Test with the corrected binary
4. If vendor binary works but ours doesn't, we know it's a generation issue
5. If vendor binary also fails, it might be an SPL/U-Boot compatibility issue

