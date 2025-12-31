#!/bin/bash
#
# Release Script for TSL Tally Light Firmware
# Creates a new GitHub release with firmware binary attached
#
# Usage: ./release.sh <version>
# Example: ./release.sh 1.0.1
#
# Prerequisites:
# - GitHub CLI (gh) installed and authenticated
# - Git repository with remote 'origin'

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PIO="/Users/richard/Library/Python/3.10/bin/pio"
FIRMWARE_PATH=".pio/build/esp32-s3/firmware.bin"

# Check for version argument
if [ -z "$1" ]; then
    echo "Usage: ./release.sh <version>"
    echo "Example: ./release.sh 1.0.1"
    exit 1
fi

VERSION="$1"
TAG="v$VERSION"

echo "=== TSL Tally Firmware Release ==="
echo "Version: $VERSION"
echo "Tag: $TAG"
echo ""

# Check if gh is installed
if ! command -v gh &> /dev/null; then
    echo "Error: GitHub CLI (gh) is not installed."
    echo "Install with: brew install gh"
    exit 1
fi

# Check if authenticated
if ! gh auth status &> /dev/null; then
    echo "Error: Not authenticated with GitHub CLI."
    echo "Run: gh auth login"
    exit 1
fi

cd "$SCRIPT_DIR"

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo "Warning: You have uncommitted changes."
    read -p "Continue anyway? [y/N] " -n 1 -r
    echo ""
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborted."
        exit 1
    fi
fi

# Update FIRMWARE_VERSION in source
echo "Updating FIRMWARE_VERSION to $VERSION..."
sed -i '' "s/#define FIRMWARE_VERSION \"[^\"]*\"/#define FIRMWARE_VERSION \"$VERSION\"/" src/main.cpp

# Verify the change
CURRENT_VERSION=$(grep '#define FIRMWARE_VERSION' src/main.cpp | sed 's/.*"\([^"]*\)".*/\1/')
if [ "$CURRENT_VERSION" != "$VERSION" ]; then
    echo "Error: Failed to update FIRMWARE_VERSION"
    exit 1
fi
echo "FIRMWARE_VERSION updated to: $CURRENT_VERSION"

# Build firmware
echo ""
echo "Building firmware..."
$PIO run -e esp32-s3

if [ ! -f "$FIRMWARE_PATH" ]; then
    echo "Error: Firmware binary not found at $FIRMWARE_PATH"
    exit 1
fi

FIRMWARE_SIZE=$(ls -lh "$FIRMWARE_PATH" | awk '{print $5}')
echo "Firmware built: $FIRMWARE_PATH ($FIRMWARE_SIZE)"

# Commit version change
echo ""
echo "Committing version change..."
git add src/main.cpp
git commit -m "Release $TAG

- Bump FIRMWARE_VERSION to $VERSION

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>"

# Create git tag
echo ""
echo "Creating git tag $TAG..."
git tag -a "$TAG" -m "Release $VERSION"

# Push to GitHub
echo ""
echo "Pushing to GitHub..."
git push origin master
git push origin "$TAG"

# Create GitHub release with firmware binary
echo ""
echo "Creating GitHub release..."
gh release create "$TAG" \
    --title "TSL Tally Firmware $TAG" \
    --notes "## TSL Tally Light Firmware $VERSION

### Installation

#### New Devices
Flash via USB:
\`\`\`bash
pio run -t upload
\`\`\`

#### Existing Devices (OTA)
1. Open device web interface
2. Click **Check** next to Firmware version
3. Click **Install** when update is available

Or use bulk update script:
\`\`\`bash
./ota-update-all.sh <any-device-ip>
\`\`\`

### Firmware Binary
Download \`firmware.bin\` below and flash manually if needed.
" \
    "$FIRMWARE_PATH"

echo ""
echo "=== Release Complete ==="
echo ""
echo "Release URL: https://github.com/videojedi/esp32-s3-tally/releases/tag/$TAG"
echo ""
echo "Devices can now update via:"
echo "  1. Web UI: Click 'Check' then 'Install'"
echo "  2. Script: ./ota-update-all.sh <device-ip>"
