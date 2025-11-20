#!/bin/bash
#
# Smart USB Traffic Capture Script for Ingenic Cloner Analysis
#
# This script automatically detects Ingenic devices and captures ONLY their traffic.
# It polls lsusb until an Ingenic device appears, then starts filtered capture.
#
# Usage:
#   sudo ./capture_usb_traffic.sh [operation_name] [--wait-for-device]
#
# Examples:
#   sudo ./capture_usb_traffic.sh vendor_read
#   sudo ./capture_usb_traffic.sh vendor_write --wait-for-device
#   sudo ./capture_usb_traffic.sh thingino_read
#

set -e

# Configuration
WAIT_FOR_DEVICE=false
OUTPUT_DIR="usb_captures"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Parse arguments
OPERATION="capture"
for arg in "$@"; do
    if [[ "$arg" == "--wait-for-device" ]]; then
        WAIT_FOR_DEVICE=true
    else
        OPERATION="$arg"
    fi
done

CAPTURE_FILE="${OPERATION}_${TIMESTAMP}.pcap"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Smart USB Traffic Capture for Ingenic Cloner ===${NC}"
echo -e "${BLUE}Operation: ${OPERATION}${NC}"
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}ERROR: This script must be run as root${NC}"
    echo "Usage: sudo $0 [operation_name] [--wait-for-device]"
    exit 1
fi

# Check if tcpdump is installed
if ! command -v tcpdump &> /dev/null; then
    echo -e "${RED}ERROR: tcpdump is not installed${NC}"
    echo "Install with: sudo apt-get install tcpdump tshark wireshark"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Load usbmon module if not loaded
if ! lsmod | grep -q usbmon; then
    echo -e "${YELLOW}Loading usbmon kernel module...${NC}"
    modprobe usbmon
fi

# Function to detect Ingenic device
detect_ingenic_device() {
    # Look for Ingenic devices by vendor ID or name
    # Ingenic vendor IDs: 0xa108 (bootrom), 0x601a (some models)
    lsusb | grep -i "ingenic\|a108\|601a" | head -1
}

# Poll for Ingenic device
echo -e "${YELLOW}Detecting Ingenic USB device...${NC}"
INGENIC_INFO=$(detect_ingenic_device)

if [ -z "$INGENIC_INFO" ]; then
    if [ "$WAIT_FOR_DEVICE" = true ]; then
        echo -e "${CYAN}Waiting for Ingenic device to appear...${NC}"
        echo -e "${CYAN}(Plug in your device or put it in bootrom mode)${NC}"
        echo ""

        # Poll every second for up to 60 seconds
        for i in {1..60}; do
            INGENIC_INFO=$(detect_ingenic_device)
            if [ -n "$INGENIC_INFO" ]; then
                echo -e "${GREEN}Device detected!${NC}"
                break
            fi
            echo -ne "\rWaiting... ${i}s "
            sleep 1
        done
        echo ""

        if [ -z "$INGENIC_INFO" ]; then
            echo -e "${RED}ERROR: Ingenic device not found after 60 seconds${NC}"
            echo "Please check:"
            echo "  1. Device is plugged in"
            echo "  2. Device is in bootrom mode (power on while holding boot button)"
            echo "  3. USB cable is working"
            exit 1
        fi
    else
        echo -e "${YELLOW}WARNING: Ingenic device not found${NC}"
        echo "Will capture all USB traffic on usbmon0"
        echo ""
        echo "Tip: Use --wait-for-device to automatically detect the device"
        echo ""
    fi
fi

# Parse device information
if [ -n "$INGENIC_INFO" ]; then
    INGENIC_BUS=$(echo "$INGENIC_INFO" | awk '{print $2}')
    INGENIC_DEV=$(echo "$INGENIC_INFO" | awk '{print $4}' | tr -d ':')
    INGENIC_BUS_NUM=$(echo "$INGENIC_BUS" | sed 's/^0*//')
    INGENIC_DEV_NUM=$(echo "$INGENIC_DEV" | sed 's/^0*//')

    echo -e "${GREEN}Found Ingenic device:${NC}"
    echo "  $INGENIC_INFO"
    echo -e "${GREEN}Bus: ${INGENIC_BUS} (usbmon${INGENIC_BUS_NUM})${NC}"
    echo -e "${GREEN}Device: ${INGENIC_DEV}${NC}"
    echo ""
fi

# Start capture
CAPTURE_PATH="$OUTPUT_DIR/$CAPTURE_FILE"
echo -e "${GREEN}Starting USB capture...${NC}"
echo "Capture file: $CAPTURE_PATH"
echo ""

# Determine capture interface and filter
if [ -n "$INGENIC_INFO" ]; then
    # Capture only the specific USB bus (we'll filter by device in analysis)
    CAPTURE_INTERFACE="usbmon${INGENIC_BUS_NUM}"

    echo -e "${CYAN}Capture mode: BUS-LEVEL (Ingenic bus only)${NC}"
    echo "  Interface: $CAPTURE_INTERFACE"
    echo "  Note: libpcap cannot filter by usb.device_address at capture time;\n        analysis tools will filter by device later."
    echo ""
    echo -e "${YELLOW}This will capture ALL traffic on this USB bus${NC}"
    echo -e "${YELLOW}(Other devices on the same bus may appear in the trace)${NC}"
else
    # Fallback: capture all USB traffic
    CAPTURE_INTERFACE="usbmon0"

    echo -e "${CYAN}Capture mode: UNFILTERED (all USB devices)${NC}"
    echo "  Interface: $CAPTURE_INTERFACE"
    echo ""
    echo -e "${YELLOW}WARNING: This will capture ALL USB traffic${NC}"
    echo -e "${YELLOW}(Including keyboard, mouse, etc.)${NC}"
fi

echo ""
echo -e "${YELLOW}Instructions:${NC}"
echo "1. Run your cloner tool in another terminal"
echo "2. Let the operation complete"
echo "3. Press Ctrl+C here when done"
echo ""
echo -e "${GREEN}Capturing... (Press Ctrl+C to stop)${NC}"
echo ""

# Trap Ctrl+C to handle cleanup
trap 'echo -e "\n${YELLOW}Stopping capture...${NC}"' INT

# Capture USB traffic
# -i interface to capture from
# -w writes to file
# -s 0 captures full packets (no truncation)
if [ -n "$INGENIC_INFO" ]; then
    # Filtered capture for specific device (bus-level capture; filter by device in analysis)
    # Note: libpcap/usbmon do NOT support USB capture filters, so we capture the whole bus
    # and later filter by usb.device_address using tshark or Python tools.

    if command -v tshark &> /dev/null; then
        echo -e "${CYAN}Using tshark (whole bus capture on ${CAPTURE_INTERFACE})...${NC}"
        # Use '-w -' and let the shell write the file. This avoids dumpcap/AppArmor
        # path restrictions while still using tshark's capture engine.
        if ! tshark -i "$CAPTURE_INTERFACE" -w - > "$CAPTURE_PATH"; then
            echo -e "${YELLOW}WARNING: tshark failed; falling back to tcpdump${NC}"
            tcpdump -i "$CAPTURE_INTERFACE" -w "$CAPTURE_PATH" -s 0
        fi
    else
        # Use tcpdump (captures whole bus)
        echo -e "${CYAN}Using tcpdump (whole bus capture on ${CAPTURE_INTERFACE})...${NC}"
        tcpdump -i "$CAPTURE_INTERFACE" -w "$CAPTURE_PATH" -s 0
    fi
else
    # Unfiltered capture (no Ingenic device detected)
    tcpdump -i "$CAPTURE_INTERFACE" -w "$CAPTURE_PATH" -s 0
fi

echo ""
echo -e "${GREEN}Capture stopped${NC}"
echo ""

# Quick analysis
echo -e "${YELLOW}Quick Analysis:${NC}"
if command -v tshark &> /dev/null; then
    PACKET_COUNT=$(tshark -r "$CAPTURE_PATH" 2>/dev/null | wc -l)
    USB_BULK_COUNT=$(tshark -r "$CAPTURE_PATH" -Y "usb.transfer_type == 0x03" 2>/dev/null | wc -l)
    USB_CONTROL_COUNT=$(tshark -r "$CAPTURE_PATH" -Y "usb.transfer_type == 0x02" 2>/dev/null | wc -l)
    
    echo "  Total packets: $PACKET_COUNT"
    echo "  USB Bulk transfers: $USB_BULK_COUNT"
    echo "  USB Control transfers: $USB_CONTROL_COUNT"
else
    echo "  Install tshark for detailed analysis: sudo apt-get install tshark"
fi

echo ""
echo -e "${GREEN}=== Capture Complete ===${NC}"
echo ""
echo "Capture saved to: $CAPTURE_PATH"
echo ""
echo "Next steps:"
echo "  1. Analyze with: python3 tools/analyze_usb_capture.py $CAPTURE_PATH"
echo "  2. Compare with: python3 tools/compare_usb_captures.py vendor.pcap thingino.pcap"
echo "  3. View in Wireshark: wireshark $CAPTURE_PATH"
echo ""

