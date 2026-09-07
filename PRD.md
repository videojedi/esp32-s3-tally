# ESP32-S3 TSL Tally Light - Product Requirements Document

## Product Overview

A professional-grade TSL 3.1/5.0 protocol tally light system built on ESP32-S3 with W5500 Ethernet. Designed for broadcast, live production, and streaming environments where camera operators need clear on-air/preview status indication.

**Version:** 1.0.8
**Author:** Video Walrus
**Copyright:** 2026 Video Walrus

## Problem Statement

Camera operators in live production environments need immediate visual feedback when their camera is selected for program (on-air) or preview. Traditional tally systems require proprietary hardware and complex wiring. This solution provides:

- Affordable, open-source tally lights using commodity hardware
- Network-based operation eliminating proprietary wiring
- Web-based configuration requiring no special software
- Multi-device management from any tally's web interface

## Target Users

1. **Broadcast Engineers** - Setting up multi-camera productions
2. **Live Event Producers** - Corporate events, worship, sports
3. **Content Creators** - Streamers and YouTubers with multi-camera setups
4. **Technical Directors** - Managing camera status in control rooms

## Hardware Requirements

### Core Components

| Component | Specification | Purpose |
|-----------|---------------|---------|
| ESP32-S3 DevKitC-1 | Dual-core, 240MHz | Main processor |
| W5500 Ethernet Module | SPI interface | Wired network connectivity |
| WS2812B LED Strip | 7 addressable RGB LEDs | Tally status display |

### Pin Configuration

| Function | GPIO | Notes |
|----------|------|-------|
| LED Data | 16 | WS2812B data line |
| ETH CS | 14 | W5500 chip select |
| ETH SCLK | 13 | SPI clock |
| ETH MISO | 12 | SPI data in |
| ETH MOSI | 11 | SPI data out |
| ETH RST | 9 | W5500 reset |
| Factory Reset | 0 | BOOT button |

## Functional Requirements

### FR-1: TSL Protocol Support (3.1 & 5.0)

**Priority:** Critical

The device shall receive and process TSL 3.1 and TSL 5.0 UMD protocol messages via UDP multicast. Protocol version is auto-detected.

| Requirement | Implementation |
|-------------|----------------|
| Multicast group | Configurable (default: 239.1.2.3) |
| UDP port | Configurable (default: 8901) |
| Address range | 0-126 |
| Tally states | Off (0), Green (1), Red (2), Yellow (3), Blue (4), Magenta (5), Cyan (6) |
| Brightness levels | 0-3, mapped to configurable max |
| Text label | 16-character display name |

**TSL 3.1 Message Format:**
- Byte 0: Address + 128
- Byte 1: Control byte (bits 0-3: tally state, bits 4-5: brightness)
- Bytes 2-17: 16-character text label

**TSL 5.0 UMD Message Format:**
- Byte 0: PBC (Packet Byte Count - 1)
- Byte 1: VER (Version, 0x00 for UMD)
- Bytes 2-3: FLAGS (little endian)
- Bytes 4-5: SCREEN (little endian)
- Bytes 6-7: INDEX (little endian) - display address
- Bytes 8-9: CONTROL (little endian) - tally and brightness
- Bytes 10+: Text (null terminated)

### FR-2: Network Connectivity

**Priority:** Critical

The device shall support multiple network connection methods with automatic fallback.

**Priority Order:**
1. **Ethernet** (W5500) - Primary, most reliable
2. **WiFi Client** - Secondary, if Ethernet unavailable
3. **Access Point** - Fallback for initial configuration

| Feature | Specification |
|---------|---------------|
| Ethernet | W5500 SPI, 10/100 Mbps |
| WiFi | 802.11 b/g/n, 2.4GHz |
| AP Mode SSID | Tally-XXYYZZ-Setup (unique per device) |
| AP Password | tallytally |
| AP IP | 192.168.4.1 |
| DHCP | Supported (default) |
| Static IP | Configurable |

### FR-3: Web Configuration Interface

**Priority:** Critical

The device shall provide a responsive web interface accessible via HTTP.

**Configuration Options:**
- TSL Settings: Address, multicast IP, port, max brightness
- WiFi Settings: Enable/disable, SSID, password
- Ethernet Settings: Hostname, DHCP/Static, IP configuration
- Factory Reset: Clear all settings

**Status Display:**
- Connection type (Ethernet/WiFi/AP)
- IP address
- Tally state with color indicator
- TSL text label
- MAC addresses
- Firmware version

**Test Controls:**
- Momentary Green, Red, Yellow buttons
- Hold to activate, release to return to off

### FR-4: Multi-Device Discovery

**Priority:** High

The device shall discover other tally lights on the network via mDNS.

| Feature | Implementation |
|---------|----------------|
| Service type | _tally._tcp |
| Discovery method | mDNS query |
| Auto-scan interval | 60 seconds |
| Manual scan | Button in web UI |
| Device limit | 16 devices |

**Device Info Exposed:**
- Hostname
- IP address
- TSL address
- Firmware version
- MAC address

### FR-5: Bulk Control

**Priority:** High

The device shall support controlling all discovered devices simultaneously.

**Bulk Actions:**
- Set all devices to Green
- Set all devices to Red
- Set all devices to Off

**Implementation:** Cross-origin AJAX requests to each device's `/test` endpoint.

### FR-6: OTA Updates

**Priority:** High

The device shall support over-the-air firmware updates via two methods.

**Method 1: ArduinoOTA (Development)**
- Port: 3232 (default)
- Password: `password`
- Requires PlatformIO

**Method 2: Release Manifest (Production)**
- Version check against the Video Walrus S3 manifest (`tsl-tally-update.json`); release notes and date shown in the web UI
- TLS to S3 validated against Amazon root CAs compiled in; SNTP clock required for certificate dates
- Every release signed with ECDSA P-256; the device hashes the download and verifies the signature before the image is marked bootable, and refuses unsigned manifests
- MD5 from the manifest also verified
- One-click download and install from web UI
- Progress indication via LED (purple during update)
- Automatic reboot after successful update

### FR-7: Persistent Storage

**Priority:** Critical

All configuration shall persist across power cycles using ESP32 NVS (Non-Volatile Storage).

**Stored Settings:**
- TSL address, multicast IP, port, max brightness, LED animation (Solid/Spin), settings PIN
- Settings apply live unless network, hostname or TSL socket settings changed (then reboot)
- Network mode (DHCP/Static), static IP configuration
- WiFi enabled flag, SSID, password
- Device hostname

### FR-8: Factory Reset

**Priority:** High

The device shall support two methods of factory reset.

**Hardware Reset:**
1. With the unit running, hold BOOT
2. Ring blinks red from 3 s, faster from 7 s
3. At 10 s the ring turns blue, settings are erased, device reboots
4. Release before 10 s to abort (release after 3 s unlocks the settings for 10 minutes)

**Web Interface Reset:**
1. Click "Reset Defaults" button
2. Confirm in dialog (PIN if set)
3. All settings cleared, device reboots

### FR-9: LED Status Indicators

**Priority:** Medium

The device shall provide visual feedback via LED patterns.

| Pattern | Meaning |
|---------|---------|
| Orange spin | Waiting for Ethernet |
| Purple spin | Connecting to WiFi |
| Cyan spin | AP mode started |
| Dim cyan | AP mode active |
| Red blink | Factory reset in progress |
| Blue flash | Factory reset complete |
| Color cycle | Network connected, ready |

**Tally Colors:**
| State | Color | Description |
|-------|-------|-------------|
| 0 | Off (black) | No tally |
| 1 | Green | Preview/safe |
| 2 | Red | Program/on-air |
| 3 | Yellow | Both preview and program |
| 4 | Blue | Extended (TSL 5.0 Tally 3) |
| 5 | Magenta | Extended (TSL 5.0 Tally 4) |
| 6 | Cyan | Extended (TSL 5.0 Tally 3+4) |

### FR-10: Captive Portal

**Priority:** Medium

When in AP mode, the device shall trigger captive portal detection on client devices.

**Supported Platforms:**
- Android (`/generate_204`)
- Windows (`/ncsi.txt`, `/connecttest.txt`)
- Apple (`/hotspot-detect.html`, `/library/test/success.html`)

**Behavior:** Redirect all captive portal probes to configuration page.

### FR-11: Disco Mode (Easter Egg)

**Priority:** Low

Type "disco" on any page to activate party mode.

**Features:**
- Activates on local and all discovered devices
- Random rainbow color cycling at 250ms intervals
- Full brightness override
- 30-second duration (configurable)
- Cancellable via "STOP THE PARTY" button

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Configuration page |
| `/status` | GET | JSON: tally, text, IP, connection |
| `/info` | GET | JSON: hostname, MAC, TSL addr, firmware |
| `/test?state=N` | GET | Set tally state (0-6) |
| `/discover` | GET | Scan network, return devices |
| `/api/check-update` | GET | Check the release manifest for updates |
| `/api/update` | GET | Download and install update |
| `/save` | POST | Save settings, reboot |
| `/reset` | GET | Factory reset, reboot |
| `/disco?duration=N` | GET | Activate disco mode |
| `/disco-stop` | GET | Deactivate disco mode |

**CORS:** All status/control endpoints include `Access-Control-Allow-Origin: *` for cross-device communication.

## Non-Functional Requirements

### NFR-1: Performance

| Metric | Requirement |
|--------|-------------|
| Tally response time | < 50ms from packet receipt |
| Web UI load time | < 2 seconds |
| Boot time to ready | < 10 seconds |
| UDP polling interval | 5ms |

### NFR-2: Reliability

| Metric | Requirement |
|--------|-------------|
| Uptime | 99.9% during production |
| Network recovery | Auto-reconnect on disconnect |
| Watchdog | Enabled, prevents lockups |

### NFR-3: Dual-Core Architecture

- **Core 0:** UDP listener task - dedicated to TSL packet reception
- **Core 1:** Main loop - web server, OTA, LED control, mDNS

This separation ensures reliable multicast reception even during web interface activity.

## Dependencies

### Libraries

| Library | Version | Purpose |
|---------|---------|---------|
| FastLED | 3.9.3 | WS2812B LED control |
| ESPAsyncWebServer | 1.2.3 | HTTP server |
| AsyncTCP | 1.1.1 | Async TCP for web server |
| ArduinoOTA | Built-in | OTA updates |
| ESPmDNS | Built-in | mDNS responder/discovery |
| Preferences | Built-in | NVS storage |
| HTTPClient | Built-in | Release manifest and firmware download |
| WiFiClientSecure | Built-in | HTTPS to S3 |

### Build Environment

- PlatformIO
- ESP32 Arduino Framework
- Board: esp32-s3-devkitc-1

## Security Considerations

| Risk | Mitigation |
|------|------------|
| OTA password | Default "password" - should be changed for production |
| No HTTPS | Local network only, not exposed to internet |
| No authentication | Web UI open access - assumes trusted network |
| Manifest OTA | Pinned TLS (Amazon roots), signed firmware (ECDSA P-256), MD5 |
| Settings lock | Optional PIN over plain HTTP; BOOT button unlock for 10 min; against accidents and LAN curiosity, not a determined attacker |

## Future Enhancements (Backlog)

1. **Authentication** - Optional login for web interface
2. **HTTPS** - Self-signed certificate support
3. **Multiple Addresses** - Monitor multiple TSL addresses
4. **Custom Colors** - User-defined tally colors
5. **Battery Support** - Low-power mode for portable operation
6. **PoE** - Power over Ethernet support
7. **TSL 5.0** - Modern protocol support
8. **DMX Output** - Trigger external tally lights
9. **REST Webhooks** - Notify external systems of state changes

## Testing Requirements

### Unit Tests
- TSL message parsing
- Version comparison
- IP address validation

### Integration Tests
- Multicast reception
- mDNS discovery
- Release manifest fetch
- NVS persistence

### Manual Tests
- Factory reset (button and web)
- Network failover (Ethernet → WiFi → AP)
- OTA update from the release manifest
- Multi-device bulk control
- Disco mode synchronization

## Release Process

1. `./release.sh <version> "note" ...` bumps `FIRMWARE_VERSION`, builds, commits, tags and pushes
2. Uploads `tsl-tally-<version>.bin` and `tsl-tally-update.json` to S3
3. `GITHUB=1` also creates a GitHub release with `firmware.bin` for devices on 1.0.12 or earlier
4. Devices can now check for and install the update

**Release Script:** `./release.sh <version>`

## Appendix A: Switcher Compatibility

| Switcher | TSL Output | Notes |
|----------|------------|-------|
| Blackmagic ATEM | Native | Multicast 239.1.2.3:8901 |
| vMix | Native | TSL 3.1 UDP output |
| OBS | Plugin required | Third-party TSL plugin |
| Tricaster | Native | Configure multicast |
| Ross | Native | TSL UMD support |

## Appendix B: Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|----------------|----------|
| No LED response | Power issue | Check USB cable |
| Can't find device | Network mismatch | Use AP mode for config |
| Tally not responding | Wrong address | Verify TSL address matches |
| Wrong colors | Address conflict | Check for duplicate addresses |
| OTA fails | Network timeout | Try wired Ethernet |
