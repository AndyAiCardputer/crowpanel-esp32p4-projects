# CrowPanel Advanced 10.1" ESP32-P4 -- Complete Knowledge Base

Everything we know about the Elecrow CrowPanel Advanced 10.1-inch ESP32-P4
HMI AI Display, collected from hands-on testing and debugging.

## Hardware Specs

| Parameter         | Value                                              |
|-------------------|----------------------------------------------------|
| Model             | DHE04310D                                          |
| Display           | 10.1" IPS, 1024x600, capacitive touch (GT911, 5pt)|
| Main Chip         | ESP32-P4 (RISC-V dual-core, up to 400 MHz)        |
| Flash             | 16 MB                                              |
| PSRAM             | 32 MB (256 Mbit, AP vendor, Gen4, X16, 200 MHz)   |
| Wireless          | ESP32-C6-MINI-1: Wi-Fi 6 (2.4 GHz), BLE 5.3       |
| Camera            | Optional 2 MP MIPI-CSI                             |
| Audio             | NS4168 codec, microphone, two speakers             |
| Power             | 5V / 2A via USB or terminal block                  |
| Display Interface | MIPI DSI (EK79007 controller)                      |
| Touch Controller  | GT911 (I2C)                                        |
| Backlight         | PWM on GPIO31                                      |
| Power Rails       | LDO3 (2.5V), LDO4 (3.3V) for display             |
| Chip Revision     | v1.3 (eFuse block revision v0.3)                   |
| MAC Address       | 30:ed:a0:e2:e6:a7                                  |

## USB Ports (Important!)

The board has **two USB-C ports** with very different functions:

| Port              | Chip   | Function                          | Notes                    |
|-------------------|--------|-----------------------------------|--------------------------|
| **UART0**         | CH341  | Serial console, legacy flashing   | BROKEN on macOS Tahoe!   |
| **USB 2.0 Device**| Native | Flashing, USB OTG (Host/Device)   | **USE THIS for everything** |

**Key insight:** The USB 2.0 Device port is the one to use for flashing AND
for USB Host mode (gamepad, keyboard, etc). The UART0 port with CH341 has
driver issues on macOS Tahoe 26.x.

## UART Pinout (from Eagle Schematic)

| Connector         | Type            | TX GPIO | RX GPIO | Notes                         |
|-------------------|-----------------|---------|---------|-------------------------------|
| UART0 (USB-C)     | CH341           | GPIO37  | GPIO38  | Default console, broken on macOS |
| **J2 (UART1)**    | HY2.0-4P Crowtail | GPIO47  | GPIO48  | **Best for debug logging**    |
| J10 (UART3-IN)    | XH2.54-4P       | GPIO34  | GPIO33  | 5V-safe (MOSFET level shifter)|

### J2 Connector Pin Order (UART1, Crowtail):
```
Pin 1: VCC (3.3V)
Pin 2: GPIO47 (TX)
Pin 3: GPIO48 (RX)
Pin 4: GND
```

## How to Flash

### Method: USB 2.0 Device Port (Recommended)

```bash
# 1. Activate ESP-IDF
source $IDF_PATH/export.sh

# 2. Put CrowPanel in download mode:
#    Hold BOOT -> Press RESET -> Release BOOT
#    Connect USB cable to "USB 2.0 Device" port (NOT UART0!)

# 3. Build and flash
cd your_project_folder
idf.py set-target esp32p4
idf.py build
idf.py flash

# 4. Press RESET to run
# 5. For serial monitor, use UART bridge (see below)
```

### Boot Modes

| Mode               | GPIO35 | GPIO36 | Method                          |
|--------------------|--------|--------|---------------------------------|
| SPI Boot (normal)  | 1      | -      | Run from Flash                  |
| Download Mode      | 0      | 1      | USB Serial/JTAG, USB 2.0, UART0|

Strapping pins: GPIO34, 35, 36, 37, 38 (read at reset, usable as GPIO after).

## The Black Screen Problem (Most Common Issue!)

If you flash code and the screen stays black, check these in order:

### 1. PSRAM Not Enabled (90% of cases)

The display framebuffer is ~2.4 MB (1024x600 x 2 bytes x 2 buffers).
This does NOT fit in internal RAM. PSRAM is mandatory.

**Add to sdkconfig.defaults:**
```ini
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y
```

**How to verify:** Check serial logs for:
```
I (xxx) esp_psram: Found 32MB PSRAM device
I (xxx) esp_psram: Speed: 200MHz
```
If you don't see this, PSRAM is not enabled.

### 2. LVGL Font Not Enabled

If using LVGL with custom font sizes, enable them:
```ini
CONFIG_LV_FONT_MONTSERRAT_42=y
CONFIG_LV_FONT_MONTSERRAT_20=y
```

### 3. Wrong ESP-IDF Version (API Changes)

Elecrow examples target ESP-IDF 5.4.2. If you use a newer version:

| Old API (5.4.x)                    | New API (5.5+ / 6.x)              |
|-------------------------------------|------------------------------------|
| `lcd_color_rgb_pixel_format_t`      | `lcd_color_format_t`               |
| `LCD_COLOR_PIXEL_FORMAT_RGB565`     | `LCD_COLOR_FMT_RGB565`             |
| `LCD_COLOR_PIXEL_FORMAT_RGB888`     | `LCD_COLOR_FMT_RGB888`             |
| `.pixel_format`                     | `.in_color_format`                 |

Also need to add to CMakeLists.txt REQUIRES:
```
esp_driver_ledc
esp_driver_gpio
```

### 4. USB DWC HAL Error (ESP-IDF 6.1 only)

```
error: 'USB_DWC_HAL_PORT_EVENT_REMOTE_WAKEUP' undeclared
```

**Fix:** Patch `managed_components/espressif__usb/CMakeLists.txt`, replace the
`REMOTE_WAKE_HAL_SUPPORTED` section at the end with:

```cmake
# PATCHED: Disable for IDF 6.1 compatibility
set(REMOTE_WAKE_HAL_SUPPORTED OFF)
```

### 5. Power Not Sufficient

The display draws significant power (8-10W at full brightness). A single USB
from PC may not be enough. Use a proper 5V/2A power supply or connect both
USB ports.

## ESP-IDF Configuration Template

Recommended `sdkconfig.defaults` for CrowPanel projects:

```ini
CONFIG_IDF_TARGET="esp32p4"

# PSRAM (CRITICAL for display!)
CONFIG_SPIRAM=y
CONFIG_SPIRAM_SPEED_200M=y

# Display
CONFIG_LV_FONT_MONTSERRAT_42=y

# USB Host (if needed)
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256
CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y
CONFIG_USB_HOST_DEBOUNCE_DELAY_MS=250

# USB Hub support (if using hub/dock)
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y

# UART1 console (redirect from broken UART0)
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
CONFIG_ESP_CONSOLE_UART_NUM=1
CONFIG_ESP_CONSOLE_UART_TX_GPIO=47
CONFIG_ESP_CONSOLE_UART_RX_GPIO=48
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200

# Task Watchdog (increase for slow display init)
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
```

## USB Host Mode

### The Power Problem

CrowPanel's USB 2.0 port does **NOT supply 5V** in Host mode. USB devices
(keyboards, gamepads) need external power.

**Solutions:**
1. Use a **powered USB hub** (e.g., Steam Deck Dock)
2. Use a USB hub with its own power supply
3. DIY: inject 5V into VBUS line externally

### USB Hub Support (Disabled by Default!)

ESP-IDF has USB hub enumeration but it's **off by default**. If your device is
behind any hub (dock station, powered hub, etc.), you MUST enable:

```ini
CONFIG_USB_HOST_HUBS_SUPPORTED=y
CONFIG_USB_HOST_HUB_MULTI_LEVEL=y
```

Without this, ESP32-P4 sees the hub but cannot reach devices behind it.

### Tested USB Devices

| Device                | VID:PID     | Type     | Connection       | Status |
|-----------------------|-------------|----------|------------------|--------|
| PS5 DualSense         | 054C:0CE6   | Gamepad  | Via Steam Deck Dock | Works |
| HP USB Keyboard       | 03F0:6941   | Keyboard | Direct USB-A     | Works  |

### USB Hub Architecture (Steam Deck Dock)

```
CrowPanel USB 2.0 (Host mode)
  └── Steam Deck Dock
        ├── USB2 Hub
        │     └── USB2.1 Hub
        │           └── DualSense Controller (054C:0CE6)
        └── USB3 Gen2 Hub
              └── USB 10/100/1000 LAN
```

## Serial Logging (UART Bridge)

Since CH341 UART0 doesn't work on macOS Tahoe, we built a UART bridge using
M5Stack Cardputer.

### How It Works

```
CrowPanel (ESP32-P4)          Cardputer (ESP32-S3)          Mac
  UART1 TX (GPIO47) ────────> GPIO1 (RX)
                               USB CDC ──────────────> Serial Monitor
  GND ──────────────────────> GND
```

### Wiring

```
CrowPanel J2 (UART1)     Cardputer Grove Port
  Pin 2: GPIO47 (TX) ──> G1 (GPIO1, RX)
  Pin 4: GND ──────────> GND
  (DO NOT connect VCC between boards!)
```

### CrowPanel sdkconfig

```ini
CONFIG_ESP_CONSOLE_UART_CUSTOM=y
CONFIG_ESP_CONSOLE_UART_NUM=1
CONFIG_ESP_CONSOLE_UART_TX_GPIO=47
CONFIG_ESP_CONSOLE_UART_RX_GPIO=48
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200
```

### Cardputer Bridge Sketch

Simple Arduino sketch: receives on GPIO1 (HardwareSerial), forwards to USB CDC.
Location: `cardputer/cardputer_adv/uart_bridge_crowpanel/`

## Display Initialization (EK79007 via MIPI DSI)

The display uses EK79007 controller over MIPI DSI interface.

### Init Sequence (from working test)

1. Initialize PSRAM (automatic with `CONFIG_SPIRAM=y`)
2. Configure LDO3 (2.5V) and LDO4 (3.3V) power rails
3. Initialize MIPI DSI bus
4. Send EK79007 initialization commands
5. Configure DPI (Display Pixel Interface) timing
6. Allocate framebuffer in PSRAM (~2.4 MB)
7. Turn on display and set backlight PWM

### Key Parameters

```
Resolution: 1024 x 600
Color format: RGB565 (16-bit) or RGB888 (24-bit)
Backlight: PWM on GPIO31
Double buffering: recommended (2x framebuffer)
```

## Elecrow Official Resources

| Resource           | URL                                                     |
|--------------------|---------------------------------------------------------|
| Wiki               | https://www.elecrow.com/wiki/CrowPanel_Advanced_10.1inch_ESP32-P4_HMI_AI_Display_1024x600_IPS.html |
| GitHub (10.1")     | https://github.com/Elecrow-RD/CrowPanel-Advanced-10.1inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen |
| GitHub (7")        | https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen |
| GitHub (all sizes) | https://github.com/Elecrow-RD/CrowPanel-Advance-HMI-ESP32-AI-Display |
| Product page       | https://www.elecrow.com/crowpanel-advanced-10-1inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-wifi-6.html |
| Eagle Schematic    | Included in GitHub repo: `Eagle_SCH&PCB/1.0/` |

### Elecrow Lessons (ESP-IDF 5.4.2)

| Lesson | Topic                  | Notes                       |
|--------|------------------------|-----------------------------|
| 01     | Hello World            | Basic serial print          |
| 02     | LED                    | GPIO control                |
| 04     | Serial Port            | UART communication          |
| 06     | USB 2.0                | USB device mode             |
| **07** | **Turn on the screen** | **Start here!**             |
| 08     | SD Card                | SD card read/write          |
| 10     | Temp/Humidity (DHT20)  | I2C sensor                  |
| 12     | Music from SD          | Audio playback              |
| 13     | Camera                 | MIPI-CSI camera             |
| 14     | SX1262 (LoRa)          | LoRa TX/RX                  |
| 15     | nRF2401                | nRF TX/RX                   |
| 16     | Weather via Wi-Fi      | ESP32-C6 Wi-Fi + HTTP       |

**Important:** Lessons are locked to ESP-IDF 5.4.2. See "ESP-IDF Compatibility"
section above for fixes needed on newer versions.

## Our Test Projects

Located in `esp32p4_elcrown/tests_crown/`:

| Test                  | Status | Description                                  |
|-----------------------|--------|----------------------------------------------|
| `test_display_colors` | PASS   | Color cycle + "Hello Andy+Ai" (LVGL)        |
| `test_usb_gamepad`    | PASS   | PS5 DualSense via dock, on-screen log        |

## Troubleshooting Checklist

If something doesn't work, go through this list:

- [ ] Is PSRAM enabled? (`CONFIG_SPIRAM=y`)
- [ ] Are you using the USB 2.0 Device port (not UART0)?
- [ ] Did you enter download mode? (BOOT + RESET sequence)
- [ ] Is power sufficient? (5V/2A recommended)
- [ ] Do serial logs show "Found 32MB PSRAM device"?
- [ ] If using USB Host: is the device externally powered?
- [ ] If using USB hub: is `CONFIG_USB_HOST_HUBS_SUPPORTED=y` enabled?
- [ ] If ESP-IDF 5.5+: did you fix the LCD API changes?
- [ ] If ESP-IDF 6.1: did you patch `REMOTE_WAKE_HAL_SUPPORTED`?
- [ ] If LVGL: are the required fonts enabled in sdkconfig?

## ESP32-P4 Quick Reference

| Feature              | Value                                    |
|----------------------|------------------------------------------|
| Architecture         | RISC-V dual-core HP + single-core LP     |
| HP Cores             | Up to 400 MHz (360 MHz default)          |
| LP Core              | Up to 40 MHz                             |
| Internal RAM         | 768 KB L2MEM + 32 KB LP SRAM + 8 KB SPM |
| GPIO                 | 55                                       |
| UART                 | 5                                        |
| SPI                  | 4                                        |
| I2C                  | 2                                        |
| USB                  | USB 2.0 HS/FS OTG + USB Serial/JTAG     |
| Display              | MIPI DSI                                 |
| Camera               | MIPI CSI                                 |
| Hardware Codecs      | JPEG encoder/decoder, H.264 encoder      |
| Crypto               | AES, SHA, RSA, ECC, HMAC                 |

---

*Created: Mar 7, 2026 by Andy + AI*
*Based on hands-on testing sessions: Feb 11-18, Mar 1, Mar 3, 2026*
