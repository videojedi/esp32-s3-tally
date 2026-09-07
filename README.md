# ESP32-S3 TSL Tally Light

A TSL 3.1 protocol tally light with web-based configuration, built for ESP32-S3 with W5500 Ethernet.

**Video Walrus 2026**

## Features

- **TSL 3.1 Protocol Support** - Receives multicast UDP tally commands, including brightness
- **Tally Arbiter listener** - Alternative source: follow a device in a Tally Arbiter server (socket.io, like the stock listener clients)
- **Dual-Core Processing** - UDP listener runs on core 0 for reliable packet reception
- **Web Configuration Interface** - Configure all settings via browser
- **Network Priority** - Ethernet preferred, WiFi fallback, AP mode for configuration
- **Auto Device Discovery** - Automatically finds all tally lights on the network via mDNS
- **Bulk Control** - Test all devices simultaneously from any tally's web interface
- **Captive Portal** - Automatic configuration page popup in AP mode
- **Unique Device Identity** - Each device gets a unique hostname based on MAC address
- **mDNS Support** - Access via hostname.local (e.g., `http://Tally-AABBCC.local`)
- **OTA Updates** - Over-the-air firmware updates from the Video Walrus release manifest, or via PlatformIO
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

Open the device IP or `http://<hostname>.local`. Three tabs:

- **Operation** (default): tally state, TSL text, TSL data counter, test buttons, network device list.
- **Configuration**: Tally Source, TSL Settings or Tally Arbiter, and LED Settings.
- **System**: device identity, clock and firmware update, Network and WiFi settings, settings lock, factory reset.

The header shows hostname, IP and connection on every tab, and the page background follows the tally colour. An Auto / Light / Dark switch above the tabs sets the page theme (Auto follows the browser or OS setting); the choice is stored in the browser and also applies to native form controls. The last tab used is remembered in the browser; in AP mode the page opens on System. Configuration and System share one form, so Save on either tab saves both.

**Save** applies most settings immediately: TSL address, max brightness and LED animation take effect without a reboot, and a toast confirms it. The device reboots only when a boot-time setting changed: hostname, IP configuration, WiFi, or the TSL multicast address and port. Hostnames are reduced to letters, digits and hyphens; IP fields that do not parse are ignored. With a settings PIN set, Save asks for it once per browser session (see [Settings Lock](#settings-lock)).

### Status

Operation tab: tally state (Off/Green/Red/Yellow), the TSL text label, and a data line: with the TSL source, packets received for this device's address, the sender and how long ago the last one arrived; with Tally Arbiter, the server, connection state, the assigned device and the update count. Polled every second. System tab: connection type, IP, hostname, MAC, clock (UTC, from SNTP), settings lock state, firmware version with **Check** / **Install** update buttons. The footer shows the firmware build date and time.

### Test Buttons

Manual tally control buttons for testing (momentary - hold to activate):
- **GREEN** - Preview/safe (state 1)
- **RED** - Program/on-air (state 2)
- **YELLOW** - Both tallies active (state 3)

Releasing a button, or dragging off it, returns the light to the last state and brightness received from the switcher (Off if nothing has been received since boot).

### Network Devices

Automatically discovers and displays other tally lights on your network:
- **Auto-discovery** - Devices are scanned on page load, and the firmware rescans in the background every 60 seconds (Ethernet/WiFi only)
- **Scan Network** - Manual refresh button; results are cached for 10 seconds
- Device list shows hostname, TSL address, IP, and a live status dot (polled every 5 seconds)
- **Open** link on each device opens its configuration page
- **Bulk control buttons** - **All GREEN**, **All RED**, **All OFF** set every discovered device plus this one. These are latching, not momentary

### Disco Mode

Type `disco` anywhere on the page (outside a text field) to run a 30-second rainbow party on this device and every discovered device. Tap **Stop** on the overlay to end it early.

### Tally Source

| Setting | Description | Default |
|---------|-------------|---------|
| Source | `TSL 3.1 (UDP multicast)` or `Tally Arbiter` (reboots) | TSL 3.1 |

### Tally Arbiter

The tally can be a [Tally Arbiter](https://github.com/josephdadams/TallyArbiter) listener client instead of a TSL receiver. It connects to the server over socket.io (port 4455), registers as a listener and follows one of the server's devices: **program lights red, preview green, both yellow**; other bus types (aux) are ignored. Tally Arbiter has no brightness, so Max Brightness applies.

| Setting | Description | Default |
|---------|-------------|---------|
| Server | Hostname or IP of the Tally Arbiter server. **Find server** lists servers announcing `_tally-arbiter._tcp` over mDNS (reboots) | |
| Port | Server port (reboots) | 4455 |
| Device | The Tally Arbiter device to follow, from the list the server sent. **Refresh device list** re-reads it. `Assign from Tally Arbiter` lets the server pick (its first device); applies live | Assign from Tally Arbiter |

The tally appears in Tally Arbiter's listener list as `Video Walrus <hostname>`. **Reassign** and **Flash** from the producer page work: a reassignment is stored on the tally and survives reboots, a flash blinks the ring white three times and returns to the current state. If the server goes away the ring holds its last state and the Operation tab shows "Not connected"; the tally reconnects on its own when the server is back.

### TSL Settings

| Setting | Description | Default |
|---------|-------------|---------|
| TSL Address | Tally address 0-126 (applies live) | 0 |
| Multicast Address | TSL multicast group, 224-239.x.x.x (reboots) | 239.1.2.3 |
| TSL Port | UDP port (reboots) | 8901 |

### LED Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Max Brightness | LED brightness limit (1-255), applies live | 50 |
| LED Animation | How an active tally is drawn: Solid or Spin, applies live | Solid |

TSL brightness levels map to the LEDs as a fraction of Max Brightness:

| TSL brightness | LED output |
|----------------|------------|
| 0 | Off (dark) |
| 1 | ⅓ of max |
| 2 | ⅔ of max |
| 3 | Max brightness |

**LED Animation** controls how Green, Red and Yellow are shown on the ring. **Solid** lights all seven LEDs. **Spin** keeps the middle LED on at the tally colour, holds the outer six at a dim level and sweeps a bright point with a fading tail round them (one revolution every 0.8 s). Off is always dark. The firmware assumes the middle LED is first in the data chain (`CENTER_LED 0` in main.cpp); set it to 6 if the ring is wired before the middle.

### Network Settings

Hostname, DHCP or static IP (address, gateway, subnet, DNS). Applies to Ethernet and WiFi. Changes here reboot the device.

### WiFi Settings

| Setting | Description |
|---------|-------------|
| WiFi | Disabled, or Enabled (used when Ethernet is down) |
| SSID | WiFi network name |
| Scan for Networks | Lists nearby networks, strongest first, one row per SSID with a lock for secured networks. Tap a row to fill in the SSID. Works in Ethernet, WiFi and AP mode |
| Password | WiFi password. Never shown once stored: leave blank to keep it, type to replace it (a **Show while typing** toggle appears), or tick **Network has no password** to clear it |

Changes here reboot the device.

### Settings Lock

An optional PIN (4 to 16 characters) gates every change made over the network: Save, Reset Defaults, firmware Install, the test buttons, bulk buttons and disco. Status and configuration reads stay open so a monitor wall can watch. The page asks for the PIN once per browser session and sends it with each change; wrong or missing gives HTTP 401. Changing or removing the PIN asks for the current one again, unless the unit was unlocked from its BOOT button. Bulk buttons and disco send this device's PIN to the other tallies they drive, so use the same PIN across a fleet.

**Forgotten PIN:** hold BOOT on the unit for 3 seconds (the ring blinks red) and release: the settings are unlocked for 10 minutes without the PIN, shown on the System tab. Anyone at the box can reflash it over USB anyway, so this costs nothing. Keep holding for 10 seconds to factory reset instead.

The PIN travels over plain HTTP. It protects against accidents and the curious on the LAN, not a determined attacker; put the device on a trusted or segmented network, as with any broadcast control gear.

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
| Red blink (BOOT held 3 s) | Release now to unlock the settings for 10 minutes; keep holding for factory reset |
| Red blink, faster (BOOT held 7 s) | Factory reset in 3 seconds, release to abort |
| Blue, then reboot | Factory reset done (BOOT held 10 s) |
| Red, green, blue cycle | Network connected, ready |
| Purple (solid) | Firmware update in progress |
| Green (solid) then reboot | Firmware update succeeded |
| Red (2 s) | Firmware update failed, then back to the current tally |

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
| `/` | GET | Configuration page (static; fills itself from `/api/config` and `/status`) |
| `/status` | GET | JSON status (tally, text, IP, connection, TSL packet count, age and sender) |
| `/api/config` | GET | JSON of every setting plus hostname, MAC, AP details, firmware and build; in Tally Arbiter mode also `taDevices` (the server's device list) and `taDeviceName` |
| `/info` | GET | JSON device info (hostname, MAC, TSL address, firmware, build) |
| `/test?state=N` | GET | Set tally state (0-3) at max brightness |
| `/test?restore=1` | GET | Return to the last state received over TSL |
| `/api/unlock?pin=N` | GET | `{"ok":true}` if the PIN is right, no PIN is set, or the unit is physically unlocked |
| `/api/ta-scan` | GET | Tally Arbiter servers found over mDNS: `{"servers":[{"host","ip","port"}]}` |
| `/discover` | GET | Scan network (cached 10 s) and return found tally devices |
| `/api/wifi-scan` | GET | Start an async WiFi scan (`?start=1`) or return its result; `{"scanning":true}` while running |
| `/disco?duration=N` | GET | Start disco mode for N seconds (1-120, default 30) |
| `/disco-stop` | GET | Stop disco mode and restore the tally state |
| `/api/check-update` | GET | Fetch the release manifest; returns current, latest, date, notes, `error` and `clock` |
| `/api/update` | GET | Download, verify signature, install |
| `/api/update-status` | GET | `fw`, `inProgress`, `error` for the install in progress |
| `/save` | POST | Save settings; applies live, or reboots if network, hostname or TSL socket settings changed |
| `/reset` | GET | Factory reset and reboot |

`/status`, `/test`, `/info`, `/api/unlock`, `/api/update-status`, `/disco` and `/disco-stop` send `Access-Control-Allow-Origin: *` so one tally's page can drive the others. With a settings PIN set, `/save`, `/reset`, `/test`, `/disco`, `/disco-stop` and `/api/update` need `pin=<PIN>` (query or form field) and answer 401 without it. `/api/config` reports `passSet` and `pinSet` instead of the values.

In AP mode, the captive-portal probe URLs (`/generate_204`, `/ncsi.txt`, `/connecttest.txt`, `/hotspot-detect.html`, `/library/test/success.html`) and any unknown path redirect to `/`.

### Status Response

```json
{
  "tally": "Green",
  "text": "CAM 1",
  "ip": "192.168.1.100",
  "connection": "Ethernet",
  "pkts": 1234,
  "age": 480,
  "from": "192.168.1.10",
  "time": 1788950000,
  "pinSet": false,
  "phys": false,
  "physLeft": 0,
  "src": 0,
  "taConn": false,
  "taHost": "",
  "taPort": 4455,
  "taDevice": ""
}
```

`src` is 0 for TSL, 1 for Tally Arbiter; `taConn` and `taDevice` (the device name) describe the Tally Arbiter connection, and `pkts`/`age` then count `device_states` updates from the server.

`pkts` counts packets addressed to this device since boot, `age` is milliseconds since the last one, `from` is its sender. `time` is the SNTP clock (Unix seconds, 0 until synced). `pinSet` says a settings PIN exists, `phys` that the unit was unlocked from its BOOT button, with `physLeft` seconds remaining.

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

### From the web page

Click **Check** on the System tab. If a newer version exists a popup lists the release notes with **Install** and **Later**; Install shows progress and reloads the page when the device is back. After an update, the page shows a "What's new" popup once per browser.

The device fetches `https://videowalrus-releases.s3.us-east-1.amazonaws.com/tsl-tally-update.json` (URL in [src/main.cpp](src/main.cpp)), compares `version` with its own, shows the release notes, and on Install downloads the binary at `url`.

Manifest format:

```json
{"version":"1.2.0","url":"https://videowalrus-releases.s3.us-east-1.amazonaws.com/tsl-tally-1.2.0.bin",
 "md5":"...","sha256":"...","sig":"<base64 ECDSA-SHA256 signature>","size":1325000,
 "release_date":"2026-09-07","notes":["...","..."]}
```

### Update security

- **Transport.** Manifest and binary are fetched over TLS validated against Amazon's root certificates compiled into the firmware ([src/certs.h](src/certs.h)). A device that cannot validate the chain refuses to talk. Validation needs the clock, so the device syncs SNTP when online and the update check waits until it has; the System tab shows the clock.
- **Authenticity.** Every release is signed with an ECDSA P-256 key that never enters the repo. `release.sh` reads it from the macOS Keychain (generic password `videowalrus-tsl-tally-signing`, PEM base64-encoded) and falls back to a PEM file at `~/.config/videowalrus/tsl-tally-signing.key` or `SIGNING_KEY=<file>`. The public key is compiled in ([src/signing_key.h](src/signing_key.h)). The device hashes the binary as it streams into the passive partition and verifies the signature before the image is marked bootable; a mismatch aborts and the running firmware stays. This holds even if the bucket or the AWS credentials are compromised.
- **Manifests without a signature are refused** by firmware 1.2.0 and later. `release.sh` checks that the compiled-in public key matches the private key before building, so a release can never be signed with a key the devices don't trust.
- **Key loss.** Losing the private key means no further OTA updates to devices in the field; they would need USB reflashing with a new public key. The login Keychain lives on one Mac, so keep a second copy somewhere encrypted and off the machine. The tally key is separate from the TSL relay's.
- **Older firmware.** 1.1.0 verifies MD5 only and ignores the signature fields, so it can still update to a signed release. 1.0.12 and earlier update from GitHub, where the v1.1.0 release remains as the stepping stone.

**Firmware 1.0.12 and earlier** looks for updates on GitHub releases instead (`https://api.github.com/repos/videojedi/esp32-s3-tally/releases/latest`, asset `firmware.bin`). The v1.1.0 GitHub release exists so those devices can reach the manifest-based firmware; after that they update from S3 like everything else.

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

```bash
./release.sh 1.1.0 "Tabbed web page" "Light and dark themes"
```

Bumps `FIRMWARE_VERSION`, builds, signs the binary, commits, tags, pushes, uploads `tsl-tally-<version>.bin` to S3 and rewrites the manifest with version, URL, MD5, SHA-256, signature, size, date and the notes given as arguments. Safe to rerun for the same version. Needs the AWS CLI and a `.env` in the project root with `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, `AWS_REGION` and `S3_BUCKET` (not committed).

`GITHUB=1 ./release.sh ...` also creates a GitHub release with `firmware.bin` attached, for devices still on 1.0.12 or earlier.

## Factory Reset

### Hardware Reset

With the unit running, hold BOOT (GPIO 0). After 3 seconds the ring blinks red; keep holding and the blink speeds up at 7 seconds. At 10 seconds the ring turns blue, settings are erased and the unit reboots into AP mode. Release before 10 seconds and nothing is erased (releasing after 3 seconds unlocks the settings instead, see [Settings Lock](#settings-lock)). Do not hold BOOT while powering on or pressing RESET: GPIO 0 low at reset enters the USB bootloader instead of the firmware.

### Web Interface Reset
1. Open the device configuration page, System tab
2. Click "Reset Defaults" and confirm (asks for the PIN if one is set)
3. Device reboots with factory settings

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

- **Core 0**: the tally source task: the UDP listener (polls for TSL packets every 5 ms) or the Tally Arbiter socket.io client (`src/arbiter.cpp`), whichever the Tally Source setting selects
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
- HTTPClient / WiFiClientSecure / Update - firmware updates from the release manifest over pinned TLS
- mbedtls (sha256, pk, base64) - firmware signature verification
- WebSockets (Links2004) - socket.io client for Tally Arbiter
- ArduinoJson - Tally Arbiter message parsing
- DNSServer - Captive portal support

## License

MIT License - Video Walrus 2026
