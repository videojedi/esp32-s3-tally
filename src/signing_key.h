/*
    Public half of the firmware signing key (ECDSA P-256). release.sh signs every binary
    with the private key kept outside the repo (macOS Keychain item
    videowalrus-tsl-tally-signing, file fallback ~/.config/videowalrus/tsl-tally-signing.key);
    the device refuses images that do not verify. Regenerating the key pair orphans every
    device in the field: they will never accept a release signed with a different key
    until reflashed over USB.
*/
#pragma once
#include <Arduino.h>

static const char FIRMWARE_SIGNING_PUBKEY[] PROGMEM = R"KEY(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEM1Mp61pZ9/GFozQvuO1GQsvt/keD
TWaj8uRz8OeXcW2Kl3jwDxwAxIuBWgXox/5Q0mDRUE8X6lYhzJwKkOj3GA==
-----END PUBLIC KEY-----
)KEY";
