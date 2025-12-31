# Quick Start Guide

Get your TSL Tally Light up and running in minutes.

## What You Need

- ESP32-S3 tally light (assembled with W5500 Ethernet module)
- Ethernet cable OR WiFi network
- Computer or phone with web browser
- TSL 3.1 compatible video switcher (ATEM, vMix, etc.)

## Step 1: Power On

Connect USB-C power to the ESP32-S3. The LED will show startup indicators:

| LED Pattern | Meaning |
|-------------|---------|
| Green blink | Looking for Ethernet |
| Purple blink | Connecting to WiFi |
| Cyan pulse | AP mode (no network found) |
| R-G-B cycle | Connected and ready |

## Step 2: Connect to the Device

### Option A: Ethernet (Recommended)

1. Plug in an Ethernet cable
2. The device gets an IP via DHCP
3. Find the IP in your router's DHCP client list, or use:
   - `http://Tally-XXXXXX.local` (where XXXXXX is shown on serial output)

### Option B: WiFi Setup Mode

If no Ethernet is connected and WiFi isn't configured:

1. The device creates a WiFi hotspot:
   - **SSID:** `Tally-XXXXXX-Setup`
   - **Password:** `tallytally`
2. Connect your phone/computer to this network
3. A configuration page should open automatically
4. If not, go to `http://192.168.4.1`

## Step 3: Configure Settings

Open the web interface and set:

### TSL Settings
- **TSL Address:** Match your switcher's tally output (0-126)
- **Multicast Address:** Usually `239.1.2.3` (check your switcher)
- **TSL Port:** Usually `8901`
- **Max Brightness:** Start with 50, increase if needed

### Network Settings
- **Hostname:** Give it a memorable name like `CAM1` or `Guest`
- **WiFi:** Enable and enter credentials if you want WiFi fallback

Click **Save Settings**. The device will reboot.

## Step 4: Configure Your Switcher

Set up your video switcher to send TSL 3.1 tally data:

### Blackmagic ATEM
1. ATEM Software Control → Preferences → Network
2. Enable "Tally Output"
3. Set multicast address to `239.1.2.3`

### vMix
1. Settings → Tally
2. Enable TSL 3.1 UDP
3. Set destination to multicast `239.1.2.3:8901`

### OBS (with plugin)
Use a TSL tally plugin to send multicast tally data.

## Step 5: Test It

On the web interface, use the test buttons:
- Hold **GREEN** - Should show green (preview)
- Hold **RED** - Should show red (program/live)
- Hold **YELLOW** - Should show yellow (both)

Then test with your actual switcher by selecting the camera.

## Troubleshooting

### No LED response
- Check USB power connection
- Try a different USB cable/power source

### Can't find device on network
- Check Ethernet cable is connected
- Try the AP mode setup (disconnect Ethernet, reboot)
- Check your router's DHCP client list

### Tally not responding to switcher
- Verify TSL address matches switcher output
- Check multicast address and port match
- Ensure switcher and tally are on same network/VLAN
- Some networks block multicast - check with IT

### Wrong colors showing
- TSL address might be wrong - check switcher config
- Another device might have the same address

## Factory Reset

If you need to start over:

1. Power on the device
2. Hold the **BOOT** button (small button on ESP32)
3. Wait for red blinking (~3 seconds)
4. Release when LED turns blue
5. Device resets to defaults and reboots into AP mode

## Multiple Devices

Setting up multiple tally lights:

1. Each device has a unique hostname based on its MAC address
2. Give each a different TSL address (matching your switcher outputs)
3. Use the **Network Devices** section in the web UI to discover and manage all tallies
4. **Bulk control** buttons let you test all devices at once

## Network Tips

- **Use Ethernet** for production - more reliable than WiFi
- **Same subnet** - Tally and switcher must be on the same network
- **Multicast routing** - If using VLANs, ensure multicast is routed
- **Static IP** - Optional, but useful for fixed installations

---

Need help? Check the full [README.md](README.md) for detailed documentation.
