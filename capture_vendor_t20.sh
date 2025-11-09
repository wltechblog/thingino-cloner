#!/bin/bash
#
# USB Traffic Capture Script for Vendor T20 Tool
#
# This script captures USB traffic to analyze the DDR binary and firmware
# that the vendor's cloner tool sends to a T20 device.
#
# Usage:
#   1. Run this script as root: sudo ./capture_vendor_t20.sh
#   2. In another terminal, run the vendor's cloner tool
#   3. Press Ctrl+C when done to stop capture
#   4. The script will extract the DDR binary and firmware from the capture
#

set -e

# Configuration
CAPTURE_FILE="vendor_t20_capture.pcap"
OUTPUT_DIR="vendor_t20_analysis"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== USB Traffic Capture for Vendor T20 Tool ===${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}ERROR: This script must be run as root${NC}"
    echo "Usage: sudo $0"
    exit 1
fi

# Check if tcpdump is installed
if ! command -v tcpdump &> /dev/null; then
    echo -e "${RED}ERROR: tcpdump is not installed${NC}"
    echo "Install with: sudo apt-get install tcpdump"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Find USB bus for Ingenic device
echo -e "${YELLOW}Looking for Ingenic USB device...${NC}"
INGENIC_BUS=$(lsusb | grep -i "ingenic\|a108" | head -1 | awk '{print $2}')
INGENIC_DEV=$(lsusb | grep -i "ingenic\|a108" | head -1 | awk '{print $4}' | tr -d ':')

if [ -z "$INGENIC_BUS" ]; then
    echo -e "${YELLOW}WARNING: Ingenic device not found yet${NC}"
    echo "The device will be captured when it appears"
    echo ""
else
    echo -e "${GREEN}Found Ingenic device on Bus $INGENIC_BUS Device $INGENIC_DEV${NC}"
    echo ""
fi

# Start capture
CAPTURE_PATH="$OUTPUT_DIR/${CAPTURE_FILE%.pcap}_${TIMESTAMP}.pcap"
echo -e "${GREEN}Starting USB capture...${NC}"
echo "Capture file: $CAPTURE_PATH"
echo ""
echo -e "${YELLOW}Instructions:${NC}"
echo "1. Run the vendor's cloner tool in another terminal"
echo "2. Let it complete the read operation"
echo "3. Press Ctrl+C here when done"
echo ""
echo -e "${GREEN}Capturing... (Press Ctrl+C to stop)${NC}"
echo ""

# Capture USB traffic on usbmon
# -i usbmon0 captures all USB buses
# -w writes to file
# -s 0 captures full packets (no truncation)
tcpdump -i usbmon0 -w "$CAPTURE_PATH" -s 0

echo ""
echo -e "${GREEN}Capture stopped${NC}"
echo ""

# Extract DDR binary and firmware from capture
echo -e "${YELLOW}Extracting data from capture...${NC}"

# Use tshark to extract USB bulk data if available
if command -v tshark &> /dev/null; then
    echo "Using tshark to analyze capture..."
    
    # Extract all USB bulk OUT data
    tshark -r "$CAPTURE_PATH" -Y "usb.transfer_type == 0x03 && usb.endpoint_address.direction == 0" \
        -T fields -e usb.capdata 2>/dev/null | \
        grep -v "^$" | \
        xxd -r -p > "$OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin" 2>/dev/null || true
    
    if [ -s "$OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin" ]; then
        echo -e "${GREEN}Extracted bulk OUT data to: $OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin${NC}"
        
        # Try to find DDR binary (324 bytes starting with "FIDB")
        echo "Searching for DDR binary (FIDB marker)..."
        grep -obUaP "FIDB.{320}" "$OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin" | \
            head -1 | cut -d: -f2 | xxd -r -p > "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin" 2>/dev/null || true
        
        if [ -s "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin" ]; then
            SIZE=$(stat -f%z "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin" 2>/dev/null || stat -c%s "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin")
            echo -e "${GREEN}Found DDR binary: $OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin ($SIZE bytes)${NC}"
            
            # Display first 64 bytes in hex
            echo ""
            echo "First 64 bytes of DDR binary:"
            xxd -l 64 "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin"
        else
            echo -e "${YELLOW}DDR binary not found in automatic extraction${NC}"
        fi
    fi
else
    echo -e "${YELLOW}tshark not installed - skipping automatic extraction${NC}"
    echo "Install with: sudo apt-get install tshark"
fi

echo ""
echo -e "${GREEN}=== Capture Complete ===${NC}"
echo ""
echo "Files created:"
echo "  - Capture file: $CAPTURE_PATH"
if [ -s "$OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin" ]; then
    echo "  - Bulk data: $OUTPUT_DIR/bulk_out_data_${TIMESTAMP}.bin"
fi
if [ -s "$OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin" ]; then
    echo "  - DDR binary: $OUTPUT_DIR/ddr_binary_${TIMESTAMP}.bin"
fi
echo ""
echo "To manually extract data, use:"
echo "  tshark -r $CAPTURE_PATH -Y 'usb.transfer_type == 0x03' -T fields -e usb.capdata"
echo ""
echo "Or analyze with Wireshark:"
echo "  wireshark $CAPTURE_PATH"
echo ""

