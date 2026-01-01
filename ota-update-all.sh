#!/bin/bash
#
# Bulk OTA Update Script for TSL Tally Lights
# Discovers all tally devices on the network and updates them
#
# Usage: ./ota-update-all.sh [known-device-ip]
#
# If no IP provided, uses mDNS to discover devices

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OTA_PASSWORD="password"
PIO="/Users/richard/Library/Python/3.10/bin/pio"

echo "=== TSL Tally Bulk OTA Update ==="
echo ""

# Build firmware first
echo "Building firmware..."
cd "$SCRIPT_DIR"
$PIO run -e esp32-s3 -s 2>/dev/null
echo "Build complete."
echo ""

# Find devices
if [ -n "$1" ]; then
    # Use provided IP to discover others
    echo "Querying $1 for device list..."
    DEVICES=$(curl -s "http://$1/discover" | python3 -c "
import sys, json
data = json.load(sys.stdin)
# Include the queried device itself
print('$1')
for d in data.get('devices', []):
    print(d['ip'])
" 2>/dev/null)
else
    # mDNS discovery without a known device IP is unreliable
    # Recommend providing an IP instead
    echo "No device IP provided."
    echo ""
    echo "Usage: ./ota-update-all.sh <known-device-ip>"
    echo ""
    echo "Example: ./ota-update-all.sh 192.168.1.100"
    echo ""
    echo "The script will query that device to discover all other tally lights on the network."
    exit 1
fi

# Convert to array and count
IFS=$'\n' read -d '' -ra DEVICE_ARRAY <<< "$DEVICES" || true
DEVICE_COUNT=${#DEVICE_ARRAY[@]}

if [ "$DEVICE_COUNT" -eq 0 ]; then
    echo "No devices found."
    exit 1
fi

echo "Found $DEVICE_COUNT device(s):"
for ip in "${DEVICE_ARRAY[@]}"; do
    # Get device info
    INFO=$(curl -s "http://$ip/info" 2>/dev/null || echo "{}")
    HOSTNAME=$(echo "$INFO" | python3 -c "import sys,json; print(json.load(sys.stdin).get('hostname','unknown'))" 2>/dev/null || echo "unknown")
    echo "  - $HOSTNAME ($ip)"
done
echo ""

# Confirm
read -p "Update all devices? [y/N] " -n 1 -r
echo ""
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Cancelled."
    exit 0
fi
echo ""

# Update each device
SUCCESS=0
FAILED=0

for ip in "${DEVICE_ARRAY[@]}"; do
    INFO=$(curl -s "http://$ip/info" 2>/dev/null || echo "{}")
    HOSTNAME=$(echo "$INFO" | python3 -c "import sys,json; print(json.load(sys.stdin).get('hostname','unknown'))" 2>/dev/null || echo "unknown")

    echo "Updating $HOSTNAME ($ip)..."

    if TALLY_IP="$ip" $PIO run -t upload -e ota -s 2>/dev/null; then
        echo "  ✓ $HOSTNAME updated successfully"
        ((SUCCESS++))
    else
        echo "  ✗ $HOSTNAME update FAILED"
        ((FAILED++))
    fi
    echo ""
done

# Summary
echo "=== Update Complete ==="
echo "  Success: $SUCCESS"
echo "  Failed:  $FAILED"
echo ""

if [ "$FAILED" -gt 0 ]; then
    exit 1
fi
