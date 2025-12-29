# ESP32-S3 TSL Tally Light

A TSL 3.1 protocol tally light with web-based configuration, built for ESP32-S3 with W5500 Ethernet.

**Video Walrus 2025**

## Features

- **TSL 3.1 Protocol Support** - Receives multicast UDP tally commands
- **Dual-Core Processing** - UDP listener runs on core 0 for reliable packet reception
- **Web Configuration Interface** - Configure all settings via browser
- **Multiple Network Options** - Ethernet (W5500), WiFi, or AP fallback mode
- **mDNS Support** - Access via hostname.local (e.g., `http://ESP-TSL-Tally.local`)
- **OTA Updates** - Over-the-air firmware updates
- **Persistent Settings** - Configuration stored in NVS flash
- **Factory Reset** - Hold BOOT button for 3 seconds to reset

## Hardware

### Components

| Component | Description |
|-----------|-------------|
| ESP32-S3 DevKitC-1 | Main microcontroller |
| W5500 Ethernet Module | SPI Ethernet PHY |
| WS2812B LED Strip | 7 addressable RGB LEDs |

### Pin Configuration

| Function | GPIO |
|----------|------|
| LED Data | 16 |
| ETH CS | 14 |
| ETH SCLK | 13 |
| ETH MISO | 12 |
| ETH MOSI | 11 |
| ETH RST | 9 |
| ETH Power | 5 |
| Factory Reset | 0 (BOOT button) |

### W5500 SPI Ethernet

The W5500 module connects via SPI. The ETH library uses these defines:
- `ETH_PHY_TYPE` = W5500
- `ETH_CLK_MODE` = GPIO_IN (avoids WiFi conflicts)

## Web Interface

Access the configuration page at the device IP or via mDNS (`http://hostname.local`).

### Status Display

- Current connection type (Ethernet/WiFi/AP)
- IP Address
- Tally State (Off/Green/Red/Yellow)
- TSL Text label
- MAC addresses

### Test Buttons

Manual tally control buttons for testing:
- **OFF** - Turn off LEDs
- **GREEN** - Preview/safe
- **RED** - Program/on-air
- **YELLOW** - Both tallies active

### TSL Settings

| Setting | Description | Default |
|---------|-------------|---------|
| TSL Address | Tally address 0-126 | 0 |
| Multicast Address | TSL multicast group | 239.1.2.3 |
| TSL Port | UDP port | 8901 |
| Max Brightness | LED brightness limit (1-255) | 50 |

TSL brightness levels (0-3) are mapped to 0 through max brightness.

### WiFi Settings

| Setting | Description |
|---------|-------------|
| WiFi Enable | Enable/disable WiFi client |
| SSID | WiFi network name |
| Password | WiFi password |

### Ethernet Settings

| Setting | Description | Default |
|---------|-------------|---------|
| Hostname | Device hostname for mDNS | ESP-TSL-Tally |
| IP Mode | DHCP or Static | DHCP |
| Static IP | IP address (if static) | 192.168.1.100 |
| Gateway | Gateway address | 192.168.1.1 |
| Subnet | Subnet mask | 255.255.255.0 |
| DNS | DNS server | 8.8.8.8 |

## Network Modes

### Priority Order

1. **Ethernet** - Preferred if cable connected
2. **WiFi** - Falls back if Ethernet unavailable
3. **AP Mode** - Creates access point if both fail

### AP Mode (Fallback)

When no network is available:
- SSID: `TSL-Tally-Setup`
- Password: `tallytally`
- IP: `192.168.4.1`
- LED indicator: Cyan pulse

## LED Indicators

| Pattern | Meaning |
|---------|---------|
| Green blink | Waiting for Ethernet |
| Purple blink | Connecting to WiFi |
| Cyan blink (3x) | AP mode started |
| Dim cyan | AP mode active |
| Red blink | Factory reset in progress |
| Blue flash | Factory reset complete |
| R-G-B cycle | Network connected, ready |

### Tally Colors

| State | Color |
|-------|-------|
| 0 | Off (black) |
| 1 | Green (preview) |
| 2 | Red (program) |
| 3 | Yellow (both) |

## TSL 3.1 Protocol

The device listens for TSL 3.1 UMD protocol messages on the configured multicast address and port.

### Message Format

| Byte | Description |
|------|-------------|
| 0 | Address + 128 |
| 1 | Control byte (tally + brightness) |
| 2-17 | 16-character text label |

### Control Byte

| Bits | Description |
|------|-------------|
| 0-3 | Tally state (0=off, 1=green, 2=red, 3=yellow) |
| 4-5 | Brightness (0-3) |

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/` | GET | Configuration page |
| `/status` | GET | JSON status (tally, text, IP, connection) |
| `/test?state=N` | GET | Set tally state (0-3) |
| `/save` | POST | Save settings and reboot |

### Status Response

```json
{
  "tally": "Green",
  "text": "CAM 1",
  "ip": "192.168.1.100",
  "connection": "Ethernet"
}
```

## OTA Updates

OTA is enabled when connected via Ethernet or WiFi (not in AP mode).

- Hostname: Configured device hostname
- Password: `password`
- Port: Default (3232)

## Factory Reset

1. Power on the device
2. Hold the BOOT button (GPIO 0)
3. Wait for red blinking (~3 seconds)
4. Release when LED turns blue
5. All settings reset to defaults

## Building

### Requirements

- PlatformIO
- ESP32 Arduino framework

### Build Commands

```bash
# Build
pio run

# Upload
pio run -t upload

# Monitor serial
pio device monitor
```

### platformio.ini

```ini
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
```

## Architecture

### Dual-Core Design

- **Core 0**: UDP listener task - polls for TSL packets every 5ms
- **Core 1**: Main loop - web server, OTA, general operation

This separation ensures reliable multicast reception even when the web interface is active.

### Libraries Used

- FastLED - WS2812B LED control
- WebServer - HTTP server
- Preferences - NVS storage
- ESPmDNS - mDNS responder
- ArduinoOTA - Over-the-air updates
- NetworkUDP - UDP multicast

## License

MIT License - Video Walrus 2025
