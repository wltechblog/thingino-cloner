#!/usr/bin/env python3
"""
Extract DDR Binary from USB Capture

This script extracts the 324-byte DDR binary (FIDB + RDD format) from a
tcpdump/tshark USB capture of the vendor's cloner tool.

Usage:
    python3 extract_ddr_from_pcap.py <capture.pcap> [output.bin]
"""

import sys
import subprocess
import struct

def extract_usb_data(pcap_file):
    """Extract USB bulk OUT data from pcap file using tshark"""
    print(f"Analyzing {pcap_file}...")
    
    # Use tshark to extract all OUT transfers with payload (control + bulk)
    cmd = [
        'tshark', '-r', pcap_file,
        '-Y', 'usb.endpoint_address.direction == 0 && usb.data_len > 0',
        '-T', 'fields',
        '-e', 'usb.capdata'
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        
        # Combine all hex strings into one binary blob
        all_data = b''
        for line in result.stdout.strip().split('\n'):
            if line:
                # Remove colons and convert hex to bytes
                hex_str = line.replace(':', '')
                try:
                    all_data += bytes.fromhex(hex_str)
                except ValueError:
                    continue
        
        return all_data
    
    except subprocess.CalledProcessError as e:
        print(f"ERROR: tshark failed: {e}")
        return None
    except FileNotFoundError:
        print("ERROR: tshark not found. Install with: sudo apt-get install tshark")
        return None

def find_ddr_binary(data):
    """Find DDR binary in USB data by looking for FIDB marker"""
    print(f"Searching for DDR binary in {len(data)} bytes of USB data...")
    
    # Look for "FIDB" marker (0x46 0x49 0x44 0x42)
    fidb_marker = b'FIDB'
    
    pos = 0
    candidates = []
    
    while pos < len(data) - 324:
        pos = data.find(fidb_marker, pos)
        if pos == -1:
            break
        
        # Extract 324 bytes starting from FIDB
        candidate = data[pos:pos+324]
        
        if len(candidate) == 324:
            # Verify it looks like a valid DDR binary
            # Check for RDD marker at offset 0xC0 (192)
            if candidate[192:195] == b'\x00RD' or candidate[192:196] == b'\x00RDD':
                print(f"Found potential DDR binary at offset {pos}")
                candidates.append((pos, candidate))
        
        pos += 1
    
    return candidates

def analyze_ddr_binary(data):
    """Analyze and display DDR binary structure"""
    if len(data) != 324:
        print(f"WARNING: DDR binary is {len(data)} bytes, expected 324")
        return
    
    print("\n=== DDR Binary Analysis ===\n")
    
    # FIDB section (0x00-0xBF, 192 bytes)
    print("FIDB Section (Platform Config):")
    fidb_magic = data[0:4]
    fidb_size = struct.unpack('<I', data[4:8])[0]
    crystal_freq = struct.unpack('<I', data[8:12])[0]
    cpu_freq = struct.unpack('<I', data[12:16])[0]
    ddr_freq = struct.unpack('<I', data[16:20])[0]
    uart_baud = struct.unpack('<I', data[28:32])[0]
    mem_size = struct.unpack('<I', data[40:44])[0]
    
    print(f"  Magic: {fidb_magic}")
    print(f"  Size: {fidb_size} bytes")
    print(f"  Crystal: {crystal_freq} Hz ({crystal_freq/1000000:.1f} MHz)")
    print(f"  CPU: {cpu_freq} Hz ({cpu_freq/1000000:.1f} MHz)")
    print(f"  DDR: {ddr_freq} Hz ({ddr_freq/1000000:.1f} MHz)")
    print(f"  UART: {uart_baud} baud")
    print(f"  Memory: {mem_size} bytes ({mem_size/1024/1024:.1f} MB)")
    
    # RDD section (0xC0-0x143, 132 bytes)
    print("\nRDD Section (DDR PHY Params):")
    rdd_magic = data[192:196]
    rdd_size = struct.unpack('<I', data[196:200])[0]
    rdd_crc = struct.unpack('<I', data[200:204])[0]
    ddr_type = struct.unpack('<I', data[204:208])[0]
    
    ddr_type_names = {0: "DDR3", 1: "DDR2", 2: "LPDDR2/LPDDR", 4: "LPDDR3"}
    ddr_type_name = ddr_type_names.get(ddr_type, f"Unknown ({ddr_type})")
    
    print(f"  Magic: {rdd_magic}")
    print(f"  Size: {rdd_size} bytes")
    print(f"  CRC32: 0x{rdd_crc:08x}")
    print(f"  DDR Type: {ddr_type_name}")
    
    # Timing parameters at 0xE4-0xF3
    cl = data[228]
    bl = data[229]
    row_bits = data[230]
    col_bits = data[231] + 6  # Stored as col_bits - 6
    
    tRAS = data[232]
    tRC = data[233]
    tRCD = data[234]
    tRP = data[235]
    tRFC = data[236]
    tRTP = data[238]
    tFAW = data[240]
    tRRD = data[242]
    tWTR = data[243]
    
    print(f"\nMemory Geometry:")
    print(f"  CL: {cl}")
    print(f"  BL: {bl}")
    print(f"  Row bits: {row_bits}")
    print(f"  Col bits: {col_bits}")
    
    print(f"\nTiming Parameters (cycles):")
    print(f"  tRAS: {tRAS}")
    print(f"  tRC: {tRC}")
    print(f"  tRCD: {tRCD}")
    print(f"  tRP: {tRP}")
    print(f"  tRFC: {tRFC}")
    print(f"  tRTP: {tRTP}")
    print(f"  tFAW: {tFAW}")
    print(f"  tRRD: {tRRD}")
    print(f"  tWTR: {tWTR}")
    
    print("\nFirst 64 bytes (hex):")
    for i in range(0, 64, 16):
        hex_str = ' '.join(f'{b:02x}' for b in data[i:i+16])
        print(f"  {i:04x}: {hex_str}")

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract_ddr_from_pcap.py <capture.pcap> [output.bin]")
        sys.exit(1)
    
    pcap_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else "vendor_ddr_extracted.bin"
    
    # Extract USB data
    usb_data = extract_usb_data(pcap_file)
    if not usb_data:
        print("ERROR: Failed to extract USB data")
        sys.exit(1)
    
    print(f"Extracted {len(usb_data)} bytes of USB bulk OUT data")
    
    # Find DDR binary
    candidates = find_ddr_binary(usb_data)
    
    if not candidates:
        print("\nERROR: No DDR binary found in capture")
        print("Make sure the capture includes the DDR config upload phase")
        sys.exit(1)
    
    print(f"\nFound {len(candidates)} DDR binary candidate(s)")
    
    # Use the first candidate
    offset, ddr_binary = candidates[0]
    
    # Save to file
    with open(output_file, 'wb') as f:
        f.write(ddr_binary)
    
    print(f"\nSaved DDR binary to: {output_file}")
    
    # Analyze the binary
    analyze_ddr_binary(ddr_binary)
    
    print(f"\n=== Success ===")
    print(f"DDR binary extracted: {output_file}")

if __name__ == '__main__':
    main()

