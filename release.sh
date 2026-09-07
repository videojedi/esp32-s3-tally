#!/bin/bash
#
# Release script for the TSL Tally Light firmware.
# Bumps FIRMWARE_VERSION, builds, commits, tags, pushes, then uploads the binary and
# the update manifest to the Video Walrus S3 bucket. Devices poll the manifest.
#
# Usage: ./release.sh <version> ["release note" ...]
# Example: ./release.sh 1.1.0 "Tabbed web UI" "Light and dark theme"
#
# GITHUB=1 ./release.sh ...   also creates a GitHub release with firmware.bin attached.
#   Firmware 1.0.12 and earlier looks for its updates on GitHub, so a release that
#   those devices should be able to reach needs this. Devices on 1.1.0 or later use
#   the S3 manifest and never look at GitHub again.
#
# Prerequisites:
# - PlatformIO (pio) on PATH
# - AWS CLI, and a .env in this directory with:
#     AWS_ACCESS_KEY_ID=...  AWS_SECRET_ACCESS_KEY=...  AWS_REGION=us-east-1  S3_BUCKET=videowalrus-releases
# - The firmware signing key (see below)
# - Git repository with remote 'origin'
# - GitHub CLI (gh) authenticated, only when GITHUB=1

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

PIO="$(command -v pio || echo "$HOME/.platformio/penv/bin/pio")"
FIRMWARE_PATH=".pio/build/esp32-s3/firmware.bin"
MANIFEST_KEY="tsl-tally-update.json"

if [ -z "$1" ]; then
    echo "Usage: ./release.sh <version> [\"release note\" ...]"
    exit 1
fi
VERSION="$1"; shift
NOTES=("$@")
TAG="v$VERSION"
BIN_KEY="tsl-tally-$VERSION.bin"

echo "=== TSL Tally Firmware Release ==="
echo "Version: $VERSION   Tag: $TAG"
echo ""

if [ -f .env ]; then
    set -a; source .env; set +a
fi
: "${AWS_REGION:=us-east-1}"
: "${S3_BUCKET:=videowalrus-releases}"
BASE_URL="https://$S3_BUCKET.s3.$AWS_REGION.amazonaws.com"

command -v aws >/dev/null || { echo "Error: aws CLI not installed"; exit 1; }
aws sts get-caller-identity >/dev/null 2>&1 || { echo "Error: AWS credentials not available (create .env)"; exit 1; }

# Firmware signing. The private key never enters the repo; the public key is compiled in
# (src/signing_key.h). Preferred home is the macOS Keychain, as a generic password holding
# the PEM base64-encoded:
#   security add-generic-password -a "$USER" -s videowalrus-tsl-tally-signing \
#     -l "TSL Tally firmware signing key" -w "$(base64 < key.pem | tr -d '\n')"
# Fallback is a PEM file at $SIGNING_KEY. The key only exists decrypted in this shell's memory.
KEYCHAIN_ITEM="videowalrus-tsl-tally-signing"
: "${SIGNING_KEY:=$HOME/.config/videowalrus/tsl-tally-signing.key}"
SIGNING_PEM=""
if B64=$(security find-generic-password -s "$KEYCHAIN_ITEM" -w 2>/dev/null); then
    SIGNING_PEM=$(printf '%s' "$B64" | base64 -d 2>/dev/null || true)
    KEY_SOURCE="Keychain item $KEYCHAIN_ITEM"
elif [ -f "$SIGNING_KEY" ]; then
    SIGNING_PEM=$(cat "$SIGNING_KEY")
    KEY_SOURCE="$SIGNING_KEY"
else
    echo "Error: no signing key. Add it to the Keychain (see comment above) or set SIGNING_KEY=<pem file>."
    exit 1
fi
case "$SIGNING_PEM" in *"BEGIN EC PRIVATE KEY"*|*"BEGIN PRIVATE KEY"*) ;; *) echo "Error: signing key from $KEY_SOURCE is not a PEM private key"; exit 1;; esac
if ! diff -q <(printf '%s\n' "$SIGNING_PEM" | openssl ec -pubout 2>/dev/null) <(sed -n '/BEGIN PUBLIC KEY/,/END PUBLIC KEY/p' src/signing_key.h) >/dev/null; then
    echo "Error: src/signing_key.h does not match the key from $KEY_SOURCE. Devices would reject this release."
    exit 1
fi
echo "Signing key: $KEY_SOURCE"
if [ -n "$GITHUB" ]; then
    command -v gh >/dev/null || { echo "Error: GitHub CLI (gh) not installed"; exit 1; }
    gh auth status >/dev/null 2>&1 || { echo "Error: not authenticated with gh"; exit 1; }
fi

if ! git diff-index --quiet HEAD --; then
    echo "Warning: You have uncommitted changes."
    read -p "Continue anyway? [y/N] " -n 1 -r; echo ""
    [[ $REPLY =~ ^[Yy]$ ]] || { echo "Aborted."; exit 1; }
fi

echo "Updating FIRMWARE_VERSION to $VERSION..."
sed -i '' "s/#define FIRMWARE_VERSION \"[^\"]*\"/#define FIRMWARE_VERSION \"$VERSION\"/" src/main.cpp
grep -q "#define FIRMWARE_VERSION \"$VERSION\"" src/main.cpp || { echo "Error: failed to update FIRMWARE_VERSION"; exit 1; }

echo ""
echo "Building firmware..."
$PIO run -e esp32-s3
[ -f "$FIRMWARE_PATH" ] || { echo "Error: firmware binary not found at $FIRMWARE_PATH"; exit 1; }
SIZE=$(stat -f %z "$FIRMWARE_PATH")
MD5=$(md5 -q "$FIRMWARE_PATH")
SHA256=$(shasum -a 256 "$FIRMWARE_PATH" | cut -d' ' -f1)
SIG=$(openssl dgst -sha256 -sign <(printf '%s\n' "$SIGNING_PEM") "$FIRMWARE_PATH" | base64 | tr -d '\n')
echo "Firmware built: $FIRMWARE_PATH ($SIZE bytes, md5 $MD5)"
echo "Signed: sha256 $SHA256, signature ${#SIG} chars"

echo ""
echo "Committing, tagging, pushing..."
if git diff --quiet -- src/main.cpp; then
    echo "FIRMWARE_VERSION was already $VERSION, no version commit needed"
else
    git add src/main.cpp
    git commit -m "Release $TAG

- Bump FIRMWARE_VERSION to $VERSION

Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>"
fi
if git rev-parse -q --verify "refs/tags/$TAG" >/dev/null; then
    echo "Tag $TAG already exists, moving it to HEAD so it matches the uploaded binary"
    git tag -f -a "$TAG" -m "Release $VERSION"
    TAG_PUSH="--force"
else
    git tag -a "$TAG" -m "Release $VERSION"
    TAG_PUSH=""
fi
BRANCH=$(git rev-parse --abbrev-ref HEAD)
git push origin "$BRANCH"
git push $TAG_PUSH origin "$TAG"

echo ""
echo "Uploading to s3://$S3_BUCKET..."
aws s3 cp "$FIRMWARE_PATH" "s3://$S3_BUCKET/$BIN_KEY" --content-type "application/octet-stream"

mkdir -p dist
NOTES_JSON=$(printf '%s\n' "${NOTES[@]}" | python3 -c 'import sys,json; print(json.dumps([l.rstrip("\n") for l in sys.stdin if l.strip()]))')
cat > "dist/$MANIFEST_KEY" <<JSON
{
  "version": "$VERSION",
  "url": "$BASE_URL/$BIN_KEY",
  "md5": "$MD5",
  "sha256": "$SHA256",
  "sig": "$SIG",
  "size": $SIZE,
  "release_date": "$(date +%Y-%m-%d)",
  "notes": $NOTES_JSON
}
JSON
aws s3 cp "dist/$MANIFEST_KEY" "s3://$S3_BUCKET/$MANIFEST_KEY" --content-type "application/json" --cache-control "no-cache"

if [ -n "$GITHUB" ]; then
    echo ""
    echo "Creating GitHub release (legacy update path for firmware 1.0.12 and earlier)..."
    BODY="## TSL Tally Light Firmware $VERSION"$'\n'
    if [ ${#NOTES[@]} -gt 0 ]; then
        BODY+=$'\n'"### What's New"$'\n'
        for n in "${NOTES[@]}"; do BODY+="- $n"$'\n'; done
    fi
    BODY+=$'\n'"### Installation"$'\n\n'
    BODY+="Devices on 1.0.12 or earlier: click **Check** then **Install** on the web page, or run \`./ota-update-all.sh <any-device-ip>\`."$'\n'
    BODY+="Devices on 1.1.0 or later update from the S3 manifest and do not use this page."$'\n\n'
    BODY+="New devices: \`pio run -t upload\` over USB, or flash \`firmware.bin\` below manually."$'\n'
    if gh release view "$TAG" >/dev/null 2>&1; then
        echo "Release $TAG already exists, replacing its firmware.bin"
        gh release upload "$TAG" "$FIRMWARE_PATH" --clobber
        gh release edit "$TAG" --notes "$BODY"
    else
        gh release create "$TAG" --title "TSL Tally Firmware $TAG" --notes "$BODY" "$FIRMWARE_PATH"
    fi
fi

echo ""
echo "=== Release Complete ==="
echo "Binary:   $BASE_URL/$BIN_KEY"
echo "Manifest: $BASE_URL/$MANIFEST_KEY"
[ -n "$GITHUB" ] && echo "GitHub:   https://github.com/videojedi/esp32-s3-tally/releases/tag/$TAG"
cat "dist/$MANIFEST_KEY"
