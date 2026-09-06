# ESP32-S3 TSL Tally Light

A TSL 3.1 protocol tally light with web-based configuration, built for ESP32-S3 with W5500 Ethernet.

**Video Walrus 2026**

## Features

- **TSL 3.1 Protocol Support** - Receives multicast UDP tally commands, including brightness
- **Dual-Core Processing** - UDP listener runs on core 0 for reliable packet reception
- **Web Configuration Interface** - Configure all settings via browser
- **Network Priority** - Ethernet preferred, WiFi fallback, AP mode for configuration
- **Auto Device Discovery** - Automatically finds all tally lights on the network via mDNS
- **Bulk Control** - Test all devices simultaneously from any tally's web interface
- **Captive Portal** - Automatic configuration page popup in AP mode
- **Unique Device Identity** - Each device gets a unique hostname based on MAC address
- **mDNS Support** - Access via hostname.local (e.g., `http://Tally-AABBCC.local`)
- **OTA Updates** - Over-the-air firmware updates via PlatformIO or GitHub releases
- **Persistent Settings** - Configuration stored in NVS flash
- **Factory Reset** - Hold BOOT button for 3 seconds, or use web interface button

## Hardware

### Components

| Component | Description |
|-----------|-------------|
| ESP32-S3 DevKitC-1 | Main microcontroller |
| W5500 Ethernet Module | SPI Ethernet PHY |
| WS2812B LED Ring | 7 addressable RGB LEDs (1 middle + 6 ring, e.g. NeoPixel Jewel) |

### Pin Configuration

| Function | GPIO |
|----------|------|
| LED Data | 16 |
| ETH CS | 14 |
| ETH SCLK | 13 |
| ETH MISO | 12 |
| ETH MOSI | 11 |
| ETH RST | 9 |
| Factory Reset | 0 (BOOT button) |

### W5500 SPI Ethernet

The W5500 module connects via SPI. These defines must be set before including ETH.h:
- `ETH_PHY_TYPE` = ETH_PHY_W5500
- `ETH_PHY_ADDR` = 1
- `ETH_PHY_CS` = 14 (Chip Select)
- `ETH_PHY_IRQ` = -1 (polled, no interrupt pin)
- `ETH_PHY_RST` = 9 (Reset)
- `ETH_PHY_SPI_HOST` = SPI2_HOST
- `ETH_PHY_SPI_SCK/MISO/MOSI` = 13/12/11

## Web Interface

Access the configuration page at the device IP or via mDNS (`http://hostname.local`).

### Status Display

- Current connection type (Ethernet/WiFi/AP)
- IP Address
- Tally State (Off/Green/Red/Yellow), refreshed every 2 seconds
- TSL Text label
- MAC addresses
- Firmware version with **Check** / **Install** update buttons

The page background follows the tally colour. The footer shows the firmware build date and time.

### Test Buttons

Manual tally control buttons for testing (momentary - hold to activate):
- **GREEN** - Preview/safe (state 1)
- **RED** - Program/on-air (state 2)
- **YELLOW** - Both tallies active (state 3)

Releasing a button returns the light to the last state and brightness received from the switcher (Off if nothing has been received since boot).

### Network Devices

Automatically discovers and displays other tally lights on your network:
- **Auto-discovery** - Devices are scanned on page load, and the firmware rescans in the background every 60 seconds (Ethernet/WiFi only)
- **Scan Network** - Manual refresh button; results are cached for 10 seconds
- Device list shows hostname, TSL address, IP, and a live status dot (polled every 5 seconds)
- **Open** link on each device opens its configuration page
- **Bulk control buttons** - **All GREEN**, **All RED**, **All OFF** set every discovered device plus this one. These are latching, not momentary

### Disco Mode

Type `disco` anywhere on the configuration page to run a 30-second rainbow party on this device and every discovered device. Tap **Stop** on the overlay to end it early.

### TSL Settings

| Setting | Description | Default |
|---------|-------------|---------|
| TSL Address | Tally address 0-126 | 0 |
| Multicast Address | TSL multicast group | 239.1.2.3 |
| TSL Port | UDP port | 8901 |
| Max Brightness | LED brightness limit (1-255) | 50 |
| LED Animation | How an active tally is drawn: Solid or Spin | Solid |

TSL brightness levels map to the LEDs as a fraction of Max Brightness:

| TSL brightness | LED output |
|----------------|------------|
| 0 | Off (dark) |
| 1 | ⅓ of max |
| 2 | ⅔ of max |
| 3 | Max brightness |

**LED Animation** controls how Green, Red and Yellow are shown on the ring. **Solid** lights all seven LEDs. **Spin** keeps the middle LED on at the tally colour, holds the outer six at a dim level and sweeps a bright point with a fading tail round them (one revolution every 0.8 s). Off is always dark. The firmware assumes the middle LED is first in the data chain (`CENTER_LED 0` in main.cpp); set it to 6 if the ring is wired before the middle.

### WiFi Settings

| Setting | Description |
|---------|-------------|
| WiFi Enable | Enable/disable WiFi client |
| SSID | WiFi network name |
| Scan for Networks | Lists nearby networks, strongest first, one row per SSID with a lock for secured networks. Tap a row to fill in the SSID. Works in Ethernet, WiFi and AP mode |
| Password | WiFi password |

### Ethernet Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Hostname | Device hostname for mDNS | Tally-XXYYZZ (unique per device) |
| IP Mode | DHCP or Static | DHCP |
| Static IP | IP address (if static) | 192.168.1.100 |
| Gateway | Gateway address | 192.168.1.1 |
| Subnet | Subnet mask | 255.255.255.0 |
| DNS | DNS server | 8.8.8.8 |

Saving settings reboots the device.

## Network Modes

The device uses a single network interface at a time for simplicity and reliability:

### Priority Order

1. **Ethernet** - Used exclusively if a link comes up within 10 seconds of boot (best for production)
2. **WiFi** - Falls back if Ethernet unavailable (10 second connect timeout)
3. **AP Mode** - Creates access point if both fail (for initial configuration)

### AP Mode (Fallback)

When no network is available, the device creates its own access point:
- SSID: `Tally-XXYYZZ-Setup` (unique per device)
- Password: `tallytally`
- IP: `192.168.4.1`
- LED indicator: cyan spin, then dim cyan
- **Captive Portal** - Configuration page opens automatically when you connect
- TSL reception, mDNS and PlatformIO OTA are disabled in AP mode

## LED Indicators

| Pattern | Meaning |
|---------|---------|
| Orange spin | Waiting for Ethernet link |
| Purple spin | Connecting to WiFi |
| Cyan spin | AP mode starting |
| Dim cyan | AP mode active |
| Red blink | Factory reset in progress |
| Blue flash | Factory reset complete |
| Red, green, blue cycle | Network connected, ready |
| Purple (solid) | GitHub firmware update in progress |
| Green (solid) then reboot | GitHub firmware update succeeded |
| Red (2 s) | GitHub firmware update failed |

The boot stages use the same spin animation as the **LED Animation** setting: the middle LED stays on in the stage colour, the outer six glow dimly and a bright point with a fading tail sweeps round them.

### Tally Colors

| State | Color | Description |
|-------|-------|-------------|
| 0 | Off (black) | No tally |
| 1 | Green | Preview/safe (tally 1) |
| 2 | Red | Program/on-air (tally 2) |
| 3 | Yellow | Both preview and program |

## TSL 3.1 Protocol

The device listens for TSL 3.1 UMD protocol messages on the configured multicast address and port. Only packets whose address matches the configured TSL Address are acted on.

### Message Format

| Byte | Description |
|------|-------------|
| 0 | Address + 128 |
| 1 | Control byte (tally + brightness) |
| 2-17 | 16-character text label |

### Control Byte

| Bits | Description |
|------|-------------|
| 0 | Tally 1 → Green |
| 1 | Tally 2 → Red (both set → Yellow) |
| 2-3 | Tally 3 and 4 (ignored) |
| 4-5 | Brightness (0-3) |

The text label is filtered to printable ASCII and trimmed.

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Configuration page |
| `/status` | GET | JSON status (tally, text, IP, connection) |
| `/info` | GET | JSON device info (hostname, MAC, TSL address, firmware, build) |
| `/test?state=N` | GET | Set tally state (0-3) at max brightness |
| `/test?restore=1` | GET | Return to the last state received over TSL |
| `/discover` | GET | Scan network (cached 10 s) and return found tally devices |
| `/api/wifi-scan` | GET | Start an async WiFi scan (`?start=1`) or return its result; `{"scanning":true}` while running |
| `/disco?duration=N` | GET | Start disco mode for N seconds (1-120, default 30) |
| `/disco-stop` | GET | Stop disco mode and restore the tally state |
| `/api/check-update` | GET | Check GitHub for firmware updates |
| `/api/update` | GET | Download and install firmware from GitHub |
| `/save` | POST | Save settings and reboot |
| `/reset` | GET | Factory reset and reboot |

`/status`, `/test`, `/info`, `/disco` and `/disco-stop` send `Access-Control-Allow-Origin: *` so one tally's page can drive the others.

In AP mode, the captive-portal probe URLs (`/generate_204`, `/ncsi.txt`, `/connecttest.txt`, `/hotspot-detect.html`, `/library/test/success.html`) and any unknown path redirect to `/`.

### Status Response

```json
{
  "tally": "Green",
  "text": "CAM 1",
  "ip": "192.168.1.100",
  "connection": "Ethernet"
}
```

### Info Response

```json
{
  "hostname": "Tally-AABBCC",
  "ip": "192.168.1.100",
  "mac": "AA:BB:CC:DD:EE:FF",
  "tslAddress": 1,
  "tallyState": "Green",
  "tallyText": "CAM 1",
  "connection": "Ethernet",
  "firmware": "1.0.10",
  "build": "Sep  2 2026 18:40:12"
}
```

### Discover Response

```json
{
  "devices": [
    {"hostname": "Tally-112233", "ip": "192.168.1.51", "tslAddress": 2},
    {"hostname": "Tally-445566", "ip": "192.168.1.52", "tslAddress": 3}
  ],
  "count": 2
}
```

## OTA Updates

OTA is enabled when connected via Ethernet or WiFi (not in AP mode).

### GitHub Release Updates (Recommended)

Devices can check for and install updates directly from GitHub releases:

1. Open device web interface
2. Click **Check** next to Firmware version
3. If update available, click **Install**
4. Device downloads firmware and reboots automatically

The device reads `https://api.github.com/repos/videojedi/esp32-s3-tally/releases/latest` and installs the asset named `firmware.bin`.

### PlatformIO OTA

For development or manual updates:

- Hostname: Configured device hostname
- Password: `password`
- Port: Default (3232)

#### Single Device Update

```bash
# Set target IP and upload
TALLY_IP=192.168.1.100 pio run -t upload -e ota

# Or use mDNS hostname
TALLY_IP=Tally-AABBCC.local pio run -t upload -e ota
```

#### Bulk Update All Devices

Use the included script to update all tally lights on the network:

```bash
# Provide any known device IP - it discovers the rest
./ota-update-all.sh 192.168.1.100
```

The script will:
1. Build the firmware
2. Query the device to discover all tallies on the network
3. Show the device list and ask for confirmation
4. Update each device via OTA
5. Report success/failure summary

### Creating Releases

Use the release script to create a new GitHub release with firmware:

```bash
./release.sh 1.0.11
```

This will:
1. Update FIRMWARE_VERSION in source
2. Build the firmware
3. Commit and tag the release
4. Push to GitHub
5. Create GitHub release with firmware.bin attached

## Factory Reset

### Hardware Reset
1. Power on the device
2. Within 3 seconds, press and hold the BOOT button (GPIO 0). Do not hold it before power-on: GPIO 0 low at reset puts the ESP32-S3 into download mode instead
3. LEDs blink red while the button is held
4. After 3 seconds the LEDs turn blue and settings are reset; release the button
5. Device continues booting with factory defaults

### Web Interface Reset
1. Open the device configuration page
2. Click "Reset Defaults" button
3. Confirm the reset
4. Device reboots with factory settings

## Building

### Requirements

- PlatformIO
- ESP32 Arduino framework 3.x

### Build Commands

```bash
# Build
pio run

# Upload over USB
pio run -t upload

# Monitor serial (115200 baud; the firmware waits 3 s at boot for the port)
pio device monitor
```

### platformio.ini

```ini
[platformio]
default_envs = esp32-s3

[env:esp32-s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
lib_deps =
    fastled/FastLED@3.9.3
build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1
upload_speed = 921600

[env:ota]
extends = env:esp32-s3
upload_protocol = espota
upload_port = ${sysenv.TALLY_IP}
upload_flags =
    --auth=password
```

`default_envs` keeps IDE build/upload buttons on the USB environment; use `-e ota` explicitly for OTA.

## Architecture

### Dual-Core Design

- **Core 0**: UDP listener task - polls for TSL packets every 5ms
- **Core 1**: Main loop - web server, captive-portal DNS, OTA, disco animation, background discovery

This separation ensures reliable multicast reception even when the web interface is active.

### Libraries Used

- FastLED - WS2812B LED control
- ETH / SPI - W5500 Ethernet
- WiFi - Station and AP modes
- WebServer - HTTP server
- Preferences - NVS storage
- ESPmDNS - mDNS responder and service discovery (`_tally._tcp` with TXT records)
- NetworkUdp - UDP multicast
- ArduinoOTA - Over-the-air updates
- HTTPClient / WiFiClientSecure / Update - GitHub release updates
- DNSServer - Captive portal support

## License

MIT License - Video Walrus 2026
